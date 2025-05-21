// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Inspired by https://github.com/sle118/squeezelite-esp32/tree/SqueezeAmp.32.1681.master-v4.3/components/wifi-manager
// - State machine rewritten for StateSmith
// - Using a base-class for called functions & accessed variables in the StateSmith SM
// - Adapted to our use-case

#include "NetworkSmBase.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/ip.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "NetworkSm.h"
#include "mem_util.h"
#include "network_ethernet.h"
#include "network_wifi.h"
#include "sdkconfig.h"
#include "uc_events.h"
#include "wifi_provisioning.h"

static const char *const TAG = "SM";

static void network_timer_cb(TimerHandle_t timer_id) {
    const char *timer_tag = (const char *)pvTimerGetTimerID(timer_id);

    ESP_LOGI(TAG, "Timer expired: %s", timer_tag ? timer_tag : "-");
    trigger_timer_event();
}

// ----------------------------------------------------------------------------

NetworkBase::NetworkBase() {
    // TODO put wifi preference into Config
    wifi_preferred_ = false;
    sta_duration_ms_ = CONFIG_NETWORK_MANAGER_STA_POLLING_MIN * 1000;

    total_connected_time_ = 0;
    last_connected_ = 0;
    num_disconnect_ = 0;
    retries_ = 0;

    wifi_connected_ = false;
    eth_handle_ = nullptr;
    eth_netif_ = nullptr;
    wifi_netif_ = nullptr;
    state_timer_ = nullptr;
    timer_tag_ = nullptr;
    event_parameters_ = nullptr;

    ESP_LOGI(
        TAG,
        "Network configuration: polling max %us, polling min %us, sta delay %lums, dhcp timeout %u, eth timeout %u",
        CONFIG_NETWORK_MANAGER_STA_POLLING_MAX, CONFIG_NETWORK_MANAGER_STA_POLLING_MIN, sta_duration_ms_,
        CONFIG_NETWORK_MANAGER_DHCP_TIMEOUT, CONFIG_NETWORK_MANAGER_ETH_LINK_DOWN_REBOOT_TIMEOUT);
}

void NetworkBase::setEventParameters(queue_message *parameters) {
    assert(parameters);

    // Warning: most queue_message fields contain allocated memory, which must be freed in the SM!
    // TODO think about a better design to make sure memory is always freed.
    // As a first step cloning the queue_message struct might help in freeing the old parameters.

    ESP_LOGI(TAG, "setEventParameters, ssid=%s, pwd=%s", parameters->ssid ? parameters->ssid : "<null>",
             parameters->password ? "****" : "<null>");

    event_parameters_ = parameters;
}

bool NetworkBase::isWifiPreferred() {
    return hasWifiConfig() && wifi_preferred_;
}

bool NetworkBase::hasWifiConfig() {
    Config &config = Config::instance();

    bool hasCfg = !config.getWifiSsid().empty();
    ESP_LOGI(TAG, "Has WiFi configuration: %d", hasCfg);
    return hasCfg;
}

void NetworkBase::initNetwork() {
    ESP_LOGI(TAG, "init_network");

    // the following functions should never fail, otherwise initialization has been called multiple times!
    // Initialize TCP/IP network interface aka the esp-netif (should be called only once in application)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &network_ip_event_handler, NULL));

    init_improv();
}

void NetworkBase::initEthernet() {
    esp_err_t                   ret = ESP_OK;
    esp_eth_netif_glue_handle_t eth_netif_glues;
    // Use ESP_NETIF_DEFAULT_ETH when just one Ethernet interface is used and you don't need to modify
    // default esp-netif configuration parameters.
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();

    ESP_LOGI(TAG, "initEthernet");

    ESP_GOTO_ON_ERROR(eth_init(&eth_handle_), err, TAG, "Failed to initialize network");

    // Create instance of esp-netif for Ethernet
    eth_netif_ = esp_netif_new(&netif_cfg);
    ESP_GOTO_ON_FALSE(eth_netif_ != NULL, ESP_ERR_INVALID_ARG, err, TAG, "Failed to create netif instance");
    eth_netif_glues = esp_eth_new_netif_glue(eth_handle_);
    ESP_GOTO_ON_FALSE(eth_netif_glues != NULL, ESP_ERR_NO_MEM, err, TAG, "Failed to create netif_glue instance");
    // Attach Ethernet driver to TCP/IP stack
    ESP_GOTO_ON_ERROR(esp_netif_attach(eth_netif_, eth_netif_glues), err, TAG,
                      "Failed to attach eth driver to tcp/ip stack");

    // Register user defined event handers
    ESP_GOTO_ON_ERROR(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL), err, TAG,
                      "Failed to register eth event handler");

    // Start Ethernet driver state machine
    ESP_GOTO_ON_ERROR(esp_eth_start(eth_handle_), err, TAG, "Failed to start eth driver");

    if (Config::instance().isNtpEnabled()) {
        ret = init_sntp();
        // don't abort, SNTP is not required for the dock to function
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize SNTP (%d): %s", ret, esp_err_to_name(ret));
        }
    }

    return;
err:
    eth_handle_ = nullptr;
    ESP_LOGE(TAG, "Failed to initialize Ethernet: %s", esp_err_to_name(ret));

    trigger_init_fail_event();
}

bool NetworkBase::initWifi() {
    ESP_LOGI(TAG, "initWifi");

    if (!wifi_netif_) {
        wifi_netif_ = network_wifi_start();
    }
    if (wifi_netif_ == nullptr) {
        trigger_init_fail_event();
        return false;
    }

    Config &config = Config::instance();
    auto    ssid = config.getWifiSsid();

    if (!ssid.empty() && !config.getWifiPassword().empty()) {
        ESP_LOGI(TAG, "Existing WiFi config found. Attempting to connect to: %s", ssid.c_str());
        return true;
    }

    // no WiFi saved: start WiFi provisioning. This usually happens at first run without ETH connection.
    ESP_LOGW(TAG, "No saved WiFi. Starting WiFi provisioning mode.");
    return false;
}

void NetworkBase::startEthLinkTimer() {
    setTimer(CONFIG_NETWORK_MANAGER_ETH_LINK_UP_TIMEOUT * 1000, "No ethernet link detected");
}

void NetworkBase::startEthLinkDownTimer() {
    setTimer(CONFIG_NETWORK_MANAGER_ETH_LINK_DOWN_REBOOT_TIMEOUT * 1000, "Ethernet link is down");
}

void NetworkBase::startDhcpTimer() {
    setTimer(CONFIG_NETWORK_MANAGER_DHCP_TIMEOUT * 1000, "DHCP timeout");
}

void NetworkBase::startWifiConnectedTimer() {
    uint32_t timeout = CONFIG_NETWORK_MANAGER_WIFI_CONNECTED_TIMEOUT * 1000;
    setTimer(timeout, "WiFi Connected timeout");
}

void NetworkBase::startWifiPollingTimer() {
    if (retries_ == 0) {
        sta_duration_ms_ = CONFIG_NETWORK_MANAGER_STA_POLLING_MIN * 1000;
    }
    ESP_LOGI(TAG, "Starting WiFi polling timer, timeout=%lums", sta_duration_ms_);
    setTimer(sta_duration_ms_, "WiFi Polling timeout");
}

void NetworkBase::setTimer(uint32_t timeout_ms, const char *tag) {
    ESP_LOGI(TAG, "Setting timer tag to %s", tag);
    FREE_AND_NULL(timer_tag_);
    timer_tag_ = strdup_to_psram(tag);

    if (!state_timer_) {
        ESP_LOGI(TAG, "Starting %s timer with period of %lums.", timer_tag_, timeout_ms);
        state_timer_ = xTimerCreate("network", pdMS_TO_TICKS(timeout_ms), pdFALSE, timer_tag_, network_timer_cb);
    } else {
        ESP_LOGI(TAG, "Changing %s timer period to %lums.", timer_tag_, timeout_ms);
        xTimerChangePeriod(state_timer_, pdMS_TO_TICKS(timeout_ms), portMAX_DELAY);
    }
    xTimerStart(state_timer_, portMAX_DELAY);
}

void NetworkBase::stopTimer() {
    if (state_timer_) {
        ESP_LOGI(TAG, "Stopping timer: %s", timer_tag_ ? timer_tag_ : "-");
        xTimerStop(state_timer_, portMAX_DELAY);
        FREE_AND_NULL(timer_tag_);
    } else {
        ESP_LOGW(TAG, "No state timer found to stop");
    }
}

bool NetworkBase::isInterfaceConnected(esp_netif_t *netif) {
    esp_netif_ip_info_t ipInfo;

    if (!netif) {
        return false;
    }

    esp_err_t err = esp_netif_get_ip_info(netif, &ipInfo);

    return ((err == ESP_OK) && (ipInfo.ip.addr != IPADDR_ANY));
}

void NetworkBase::connectActiveSsid() {
    ESP_LOGI(TAG, "connectActiveSsid");

    Config &config = Config::instance();

    auto ssid = config.getWifiSsid();
    auto password = config.getWifiPassword();

    if (network_wifi_connect(ssid.c_str(), password.c_str()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi STA connection");
        wifi_connected_ = false;
        ESP_LOGD(TAG, "Checking if ethernet interface is connected");
        if (isInterfaceConnected(eth_netif_)) {
            ESP_LOGI(TAG, "Ethernet connection is found.  Try to fallback there");
            trigger_eth_fallback_event();
        } else {
            // start WiFi provisioning
            sta_duration_ms_ = CONFIG_NETWORK_MANAGER_STA_POLLING_MIN * 1000;
            ESP_LOGI(TAG, "No ethernet and no WiFi configured. Starting WiFi provisioning");
            trigger_configure_wifi_event();
        }
    }
}

bool NetworkBase::isWifiErrReason(uint8_t reason) {
    uint8_t wifi_reason = (event_parameters_ && event_parameters_->sta_disconnected_event)
                              ? event_parameters_->sta_disconnected_event->reason
                              : 0;
    return wifi_reason == reason;
}

void NetworkBase::connectWifi() {
    esp_err_t ret = ESP_OK;
    ESP_LOGI(TAG, "connectWifi");

    if (event_parameters_) {
        ret = network_wifi_connect(event_parameters_->ssid, event_parameters_->password);
        FREE_AND_NULL(event_parameters_->ssid);
        FREE_AND_NULL(event_parameters_->password);
    } else {
        ESP_LOGE(TAG, "Cannot connect to WiFi: missing AP parameters!");
        ret = ESP_ERR_INVALID_ARG;
    }

    if (ret != ESP_OK) {
        wifi_event_sta_disconnected_t event = {};
        event.reason = WIFI_REASON_UNSPECIFIED;
        trigger_lost_connection_event(&event);
    }
}

void NetworkBase::saveActiveWifiConfig() {
    last_connected_ = esp_timer_get_time();

    network_wifi_save_config();

    // TODO network_interface_coexistence();

    retries_ = 0;
    wifi_connected_ = true;
}

void NetworkBase::clearWifiConfig() {
    ESP_LOGI(TAG, "WiFi disconnected by user");
    network_wifi_clear_config();
}

void NetworkBase::clearEventParameters() {
    ESP_LOGI(TAG, "clearEventParameters");
    if (event_parameters_) {
        FREE_AND_NULL(event_parameters_->sta_disconnected_event);
    }
}

bool NetworkBase::shouldRetryActiveWifiConnection() {
    return retries_ < CONFIG_NETWORK_MANAGER_MAX_RETRY;
}

void NetworkBase::retryActiveWiFiConnection() {
    statusUpdate(UPDATE_LOST_CONNECTION);

    // TODO reset IP address in network status
    // network_status_safe_reset_sta_ip_string();
    if (last_connected_ > 0) {
        total_connected_time_ += ((esp_timer_get_time() - last_connected_) / (1000 * 1000));
    }
    last_connected_ = 0;
    num_disconnect_++;
    ESP_LOGW(TAG, "WiFi disconnected. Number of disconnects: %u, Average time connected: %li", num_disconnect_,
             num_disconnect_ > 0 ? (total_connected_time_ / num_disconnect_) : 0);

    if (retries_ == 0) {
        sta_duration_ms_ = CONFIG_NETWORK_MANAGER_STA_POLLING_MIN * 1000;
    } else if (sta_duration_ms_ < CONFIG_NETWORK_MANAGER_STA_POLLING_MAX * 1000) {
        sta_duration_ms_ = sta_duration_ms_ * CONFIG_NETWORK_MANAGER_RETRY_BACKOFF / 100;
    }

    retries_++;
    ESP_LOGI(TAG, "Retrying WiFi connection (%d/%d) in %lums", retries_, CONFIG_NETWORK_MANAGER_MAX_RETRY,
             sta_duration_ms_);

    // Note: SM will start the wifi polling timer -> reconnect delay, timer will call WIFI_CONNECTING_STATE
}

static void getWiFiInfo(uc_event_network_state_t *net_state) {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        memcpy(net_state->ssid, ap_info.ssid, sizeof(net_state->ssid));
        net_state->rssi = ap_info.rssi;
    } else {
        ESP_LOGE(TAG, "Failed to get WiFi IP info");
    }
}

void NetworkBase::statusUpdate(update_reason_code_t update_reason_code) {
    ESP_LOGI(TAG, "statusUpdate: %d -> post UC_EVENT for UI SM", update_reason_code);

    // send custom system event (used by display state-machine)
    esp_netif_ip_info_t      ip_info;
    uc_event_network_state_t net_state;
    memset(&net_state, 0, sizeof(net_state));
    net_state.connection = WIFI;
    net_state.eth_link = is_eth_link_up();
    net_state.ip.type = ESP_IPADDR_TYPE_ANY;

    int32_t event_id = 0;
    switch (update_reason_code) {
        case UPDATE_WIFI_CONNECTED: {
            event_id = UC_EVENT_CONNECTED;
            net_state.ip.type = ESP_IPADDR_TYPE_V4;

            esp_err_t ret = network_get_ip_info_for_netif(wifi_netif_, &ip_info);
            if (ret == ESP_OK) {
                net_state.ip.u_addr.ip4 = ip_info.ip;
            } else {
                ESP_LOGE(TAG, "Failed to get WiFi IP info");
            }
            getWiFiInfo(&net_state);
            break;
        }
        case UPDATE_LOST_CONNECTION: {
            event_id = UC_EVENT_DISCONNECTED;
            // use configured SSID
            Config     &config = Config::instance();
            std::string ssid = config.getWifiSsid();
            strncpy(reinterpret_cast<char *>(net_state.ssid), ssid.c_str(), sizeof(net_state.ssid));
            break;
        }
        case UPDATE_FAILED_ATTEMPT:
        case UPDATE_USER_DISCONNECT:
        case UPDATE_FAILED_ATTEMPT_AND_RESTORE: {
            // Use generic disconnected event, maybe use a dedicated event in the future, depending on UI screens
            event_id = UC_EVENT_DISCONNECTED;
            // Using the confiugred SSID might not be the expected information if a user wanted to connect to a new AP!
            // Since the UI screens are not yet finalized, this is good enough for now :-)
            Config     &config = Config::instance();
            std::string ssid = config.getWifiSsid();
            strncpy(reinterpret_cast<char *>(net_state.ssid), ssid.c_str(), sizeof(net_state.ssid));
            break;
        }
        case UPDATE_ETH_CONNECTED: {
            event_id = UC_EVENT_CONNECTED;
            net_state.connection = ETHERNET;
            net_state.ip.type = ESP_IPADDR_TYPE_V4;

            esp_err_t ret = network_get_ip_info_for_netif(eth_netif_, &ip_info);
            if (ret == ESP_OK) {
                net_state.ip.u_addr.ip4 = ip_info.ip;
            } else {
                ESP_LOGE(TAG, "Failed to get ETH IP info");
            }
            break;
        }
        case UPDATE_ETH_LINK_DOWN:
            event_id = UC_EVENT_DISCONNECTED;
            net_state.connection = ETHERNET;
            break;
        case UPDATE_ETH_LINK_UP:
            // event_id = UC_EVENT_ETH_LINK_UP;
            // net_state.connection = ETHERNET;
            // ignore
            return;
        case UPDATE_ETH_CONNECTING:
            event_id = UC_EVENT_CONNECTING;
            net_state.connection = ETHERNET;
            break;
        case UPDATE_WIFI_CONNECTING:
            event_id = UC_EVENT_CONNECTING;
            getWiFiInfo(&net_state);
            break;
        case UPDATE_WIFI_PROVISIONING:
            event_id = UC_EVENT_IMPROV_START;
            break;
    }

    assert(event_id);

    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_event_post(UC_DOCK_EVENTS, event_id, &net_state, sizeof(net_state), pdMS_TO_TICKS(1000)));
}

void NetworkBase::startWifiDhcpClient() {
    ESP_LOGI(TAG, "startWifiDhcpClient");
    network_start_stop_dhcp_client(wifi_netif_, true);
}

void host_task(void *param) {
    nimble_port_run();
}

void NetworkBase::startImprovWifi() {
    esp_err_t ret = ESP_OK;

    if (!improv_init_) {
        ESP_LOGI(TAG, "starting BLE for improv-wifi");
        improv_init_ = true;
        ESP_GOTO_ON_ERROR(nimble_port_init(), err, TAG, "BLE init failed");
        ESP_GOTO_ON_ERROR(start_improv(), err, TAG, "Failed to start Improv");
        nimble_port_freertos_init(host_task);
    }

    trigger_init_success_event();
    return;
err:
    trigger_init_fail_event();
}

void NetworkBase::setImprovStopped() {
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, UC_EVENT_IMPROV_START, NULL, 0, pdMS_TO_TICKS(1000)));
}

void NetworkBase::setImprovAuthRequired() {
    ESP_LOGI(TAG, "setImprovAuthRequired");
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_event_post(UC_DOCK_EVENTS, UC_EVENT_IMPROV_AUTH_REQUIRED, NULL, 0, pdMS_TO_TICKS(1000)));
}

void NetworkBase::setImprovAuthorized() {
    ESP_LOGI(TAG, "setImprovAuthorized");
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_event_post(UC_DOCK_EVENTS, UC_EVENT_IMPROV_AUTHORIZED, NULL, 0, pdMS_TO_TICKS(1000)));
    improv_set_authorized();
}

void NetworkBase::onImprovConnectTimeout() {
    ESP_LOGI(TAG, "onImprovConnectTimeout");
    on_wifi_connect_timeout();
}

void NetworkBase::setImprovProvisioning() {
    ESP_LOGI(TAG, "setImprovProvisioning");

    uc_event_network_state_t net_state;
    memset(&net_state, 0, sizeof(net_state));
    net_state.connection = WIFI;
    net_state.eth_link = is_eth_link_up();
    net_state.ip.type = ESP_IPADDR_TYPE_ANY;

    if (event_parameters_) {
        strncpy(reinterpret_cast<char *>(net_state.ssid), event_parameters_->ssid, sizeof(net_state.ssid));
    } else {
        ESP_LOGE(TAG, "UC_EVENT_IMPROV_PROVISIONING: missing AP parameters!");
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, UC_EVENT_IMPROV_PROVISIONING, &net_state,
                                                 sizeof(net_state), pdMS_TO_TICKS(1000)));
}

void NetworkBase::startImprovTimer() {
    ESP_LOGI(TAG, "startImprovTimer");
    sta_duration_ms_ = 30 * 1000;
    ESP_LOGI(TAG, "Starting Improv timer, timeout=%lums", sta_duration_ms_);
    setTimer(sta_duration_ms_, "Improv timeout");
}

void NetworkBase::setImprovWifiProvisioned() {
    ESP_LOGI(TAG, "setImprovWifiProvisioned");
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, UC_EVENT_IMPROV_END, NULL, 0, pdMS_TO_TICKS(1000)));
    improv_set_provisioned();
}

void NetworkBase::reboot() {
    ESP_LOGW(TAG, "Restarting!");
    // TODO centralized restart function to allow closing any open file / network resources!
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, UC_EVENT_REBOOT, NULL, 0, pdMS_TO_TICKS(1000)));
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}
