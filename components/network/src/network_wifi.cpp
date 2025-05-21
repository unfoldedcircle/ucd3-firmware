// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Inspired by:
// - https://github.com/Mair/esp32-course
// - https://github.com/sle118/squeezelite-esp32/tree/SqueezeAmp.32.1681.master-v4.3/components/wifi-manager

#include "network_wifi.h"

#include <netdb.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "config.h"
#include "network_priv.h"

static const char*  TAG = "WIFI";
static esp_netif_t* wifi_netif;
static bool         attempt_reconnect = false;

static void network_wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
const char* get_wifi_disconnection_str(wifi_err_reason_t wifi_err_reason);

static void network_wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base != WIFI_EVENT) {
        return;
    }

    switch (event_id) {
        case WIFI_EVENT_WIFI_READY:
            ESP_LOGD(TAG, "WIFI_EVENT_WIFI_READY");
            break;

        case WIFI_EVENT_SCAN_DONE:
            ESP_LOGD(TAG, "WIFI_EVENT_SCAN_DONE");
            // trigger_scan_done_event();
            break;

        case WIFI_EVENT_STA_AUTHMODE_CHANGE:
            ESP_LOGD(TAG, "WIFI_EVENT_STA_AUTHMODE_CHANGE");
            break;

        case WIFI_EVENT_AP_START:
            ESP_LOGD(TAG, "WIFI_EVENT_AP_START");
            break;

        case WIFI_EVENT_AP_STOP:
            ESP_LOGD(TAG, "WIFI_EVENT_AP_STOP");
            break;

        case WIFI_EVENT_AP_PROBEREQRECVED: {
            wifi_event_ap_probe_req_rx_t* s = (wifi_event_ap_probe_req_rx_t*)event_data;
            ESP_LOGD(TAG, "WIFI_EVENT_AP_PROBEREQRECVED. RSSI: %d, MAC: %02x:%02x:%02x:%02x:%02x:%02x", s->rssi,
                     s->mac[0], s->mac[1], s->mac[2], s->mac[3], s->mac[4], s->mac[5]);
        } break;
        case WIFI_EVENT_STA_WPS_ER_SUCCESS:
            ESP_LOGD(TAG, "WIFI_EVENT_STA_WPS_ER_SUCCESS");
            break;
        case WIFI_EVENT_STA_WPS_ER_FAILED:
            ESP_LOGD(TAG, "WIFI_EVENT_STA_WPS_ER_FAILED");
            break;
        case WIFI_EVENT_STA_WPS_ER_TIMEOUT:
            ESP_LOGD(TAG, "WIFI_EVENT_STA_WPS_ER_TIMEOUT");
            break;
        case WIFI_EVENT_STA_WPS_ER_PIN:
            ESP_LOGD(TAG, "WIFI_EVENT_STA_WPS_ER_PIN");
            break;
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t* stac = (wifi_event_ap_staconnected_t*)event_data;
            ESP_LOGD(TAG, "WIFI_EVENT_AP_STACONNECTED. aid: %d, mac: %02x:%02x:%02x:%02x:%02x:%02x", stac->aid,
                     stac->mac[0], stac->mac[1], stac->mac[2], stac->mac[3], stac->mac[4], stac->mac[5]);
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGD(TAG, "WIFI_EVENT_AP_STADISCONNECTED");
            break;

        case WIFI_EVENT_STA_START:
            ESP_LOGD(TAG, "WIFI_EVENT_STA_START");
            break;

        case WIFI_EVENT_STA_STOP:
            ESP_LOGD(TAG, "WIFI_EVENT_STA_STOP");
            break;

        case WIFI_EVENT_STA_CONNECTED: {
            ESP_LOGD(TAG, "WIFI_EVENT_STA_CONNECTED. ");
            wifi_event_sta_connected_t* s = (wifi_event_sta_connected_t*)event_data;
            const char*                 ssid = (const char*)s->ssid;
            if (ssid) {
                ESP_LOGD(
                    TAG,
                    "WIFI_EVENT_STA_CONNECTED. Channel: %d, Access point: %s, BSSID: %02x:%02x:%02x:%02x:%02x:%02x",
                    s->channel, ssid, s->bssid[0], s->bssid[1], s->bssid[2], s->bssid[3], s->bssid[4], s->bssid[5]);
            }
            trigger_connected_event();

        } break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t* s = (wifi_event_sta_disconnected_t*)event_data;
            ESP_LOGW(TAG,
                     "WIFI_EVENT_STA_DISCONNECTED. From BSSID: %02x:%02x:%02x:%02x:%02x:%02x, reason code: %d (%s)",
                     s->bssid[0], s->bssid[1], s->bssid[2], s->bssid[3], s->bssid[4], s->bssid[5], s->reason,
                     get_wifi_disconnection_str(static_cast<wifi_err_reason_t>(s->reason)));
            if (s->reason == WIFI_REASON_ROAMING) {
                ESP_LOGI(TAG, "WiFi Roaming to new access point");
            } else {
                trigger_lost_connection_event((wifi_event_sta_disconnected_t*)event_data);
            }
        } break;

        default:
            break;
    }
}

esp_err_t network_wifi_set_sta_mode() {
    if (!wifi_netif) {
        ESP_LOGE(TAG, "Wifi not initialized. Cannot set sta mode");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Set Mode to STA");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Error setting mode to STA");

    ESP_LOGI(TAG, "Starting wifi");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Error starting WiFi");

    return ESP_OK;
}

esp_netif_t* network_wifi_start() {
    ESP_LOGD(TAG, "Starting wifi interface as STA mode");
    if (!wifi_netif) {
        ESP_LOGD(TAG, "Init STA mode - creating default interface. ");
        wifi_netif = esp_netif_create_default_wifi_sta();  // aborts in case of error! No need to check return value.
        ESP_LOGD(TAG, "Initializing Wifi. ");
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_init(&cfg));
        ESP_LOGD(TAG, "Registering wifi Handlers");
        // network_wifi_register_handlers();
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &network_wifi_event_handler, NULL, NULL));
        ESP_LOGD(TAG, "Setting up wifi Storage");
        // TODO decide on storage option: RAM or FLASH
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    }
    ESP_LOGI(TAG, "Setting up wifi mode as STA");
    network_wifi_set_sta_mode();
    ESP_LOGD(TAG, "Setting hostname");
    network_set_hostname(wifi_netif);
    ESP_LOGD(TAG, "Done starting wifi interface");
    return wifi_netif;
}

bool is_wifi_up() {
    return wifi_netif != NULL;
}

esp_err_t network_wifi_connect(const char* ssid, const char* password) {
    esp_err_t     err = ESP_OK;
    wifi_config_t config;
    memset(&config, 0, sizeof(config));

    ESP_LOGD(TAG, "network_wifi_connect");

    if (!is_wifi_up()) {
        return ESP_FAIL;
    }
    // TODO support open networks without password?
    ESP_LOGI(TAG, "network_wifi_connect, ssid=%s, pwd=%s", ssid ? ssid : "<null>", password ? "****" : "<null>");
    if (!ssid || !password || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "Cannot connect wifi. wifi config is null!");
        return ESP_ERR_INVALID_ARG;
    }

    wifi_mode_t wifi_mode;
    err = esp_wifi_get_mode(&wifi_mode);
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "Wifi not initialized. Attempting to start sta mode");
        network_wifi_start();
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not retrieve wifi mode : %s", esp_err_to_name(err));
    } else if (wifi_mode != WIFI_MODE_STA && wifi_mode != WIFI_MODE_APSTA) {
        ESP_LOGD(TAG, "Changing wifi mode to STA");
        err = network_wifi_set_sta_mode();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Could not set mode to STA.  Cannot connect to SSID %s", ssid);
            return err;
        }
    }

    // copy configuration and connect
    strlcpy((char*)config.sta.ssid, ssid, sizeof(config.sta.ssid));
    if (password) {
        strlcpy((char*)config.sta.password, password, sizeof(config.sta.password));
    }

    // First Disconnect
    esp_wifi_disconnect();

    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    if ((err = esp_wifi_set_config(WIFI_IF_STA, &config)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA configuration. Error %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Wifi Connecting to %s...", ssid);
        if ((err = esp_wifi_connect()) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initiate wifi connection. Error %s", esp_err_to_name(err));
        }
    }
    return err;
}

void network_wifi_clear_config() {
    Config::instance().setWifi("", "");

    esp_err_t err = ESP_OK;
    if ((err = esp_wifi_disconnect()) != ESP_OK) {
        ESP_LOGW(TAG, "Could not disconnect from deleted network : %s", esp_err_to_name(err));
    }
}

void network_wifi_save_config() {
    ESP_LOGD(TAG, "Checking if WiFi config changed.");

    esp_err_t     err = ESP_OK;
    wifi_config_t config;
    memset(&config, 0, sizeof(config));

    if ((err = esp_wifi_get_config(WIFI_IF_STA, &config)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read STA configuration. Error %s", esp_err_to_name(err));
    } else {
        Config& cfg = Config::instance();
        auto    oldSsid = cfg.getWifiSsid();
        auto    oldPwd = cfg.getWifiPassword();

        const char* ssid = (const char*)config.sta.ssid;
        const char* password = (const char*)config.sta.password;
        if (config.sta.ssid[0] && (oldSsid != ssid || oldPwd != password)) {
            ESP_LOGI(TAG, "Saving changed WiFi config, ssid=%s", ssid);
            cfg.setWifi(ssid, password);
        }
    }
}

esp_err_t wifi_connect_configured_sta() {
    wifi_disconnect();
    attempt_reconnect = true;
    if (wifi_netif) {
        // TODO research esp-netif handling, when to destroy it when dealing with AP / STA switching.
        ESP_LOGD(TAG, "wifi_connect_configured_sta: destroying old netif");
        esp_netif_destroy(wifi_netif);
    }
    ESP_LOGD(TAG, "wifi_connect_configured_sta: creating new netif");
    wifi_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Failed to set WiFi mode");

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start WiFi");

    return ESP_OK;
}

esp_err_t wifi_connect_sta(uint8_t ssid[32], uint8_t password[64]) {
    wifi_disconnect();
    attempt_reconnect = true;
    if (wifi_netif) {
        // TODO research esp-netif handling, when to destroy it when dealing with AP / STA switching.
        ESP_LOGD(TAG, "wifi_connect_sta: destroying old netif");
        esp_netif_destroy(wifi_netif);
    }
    ESP_LOGD(TAG, "wifi_connect_sta: creating new netif");
    wifi_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Failed to set WiFi mode");

    wifi_config_t wifi_config = {};
    memcpy(wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "Failed to set WiFi config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start WiFi");

    return ESP_OK;
}

esp_err_t wifi_connect_ap(uint8_t ssid[32], uint8_t password[64]) {
    wifi_disconnect();
    if (wifi_netif) {
        // TODO research esp-netif handling, when to destroy it when dealing with AP / STA switching.
        ESP_LOGD(TAG, "wifi_connect_ap: destroying old netif");
        esp_netif_destroy(wifi_netif);
    }
    wifi_netif = esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    wifi_config_t wifi_config = {};
    memcpy(wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid));
    memcpy(wifi_config.ap.password, password, sizeof(wifi_config.ap.password));
    // TODO make authmode configurable, or do a scan before connect to determine authentication
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_WPA3_PSK;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.beacon_interval = 100;
    wifi_config.ap.channel = 1;

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wifi_config), TAG, "Failed to set AP WiFi configuration");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start WiFi AP");

    return ESP_OK;
}

void wifi_disconnect(void) {
    attempt_reconnect = false;
    esp_wifi_stop();
    // don't call esp_netif_destroy here! Data is still accessed by IDF
}

const char* get_wifi_disconnection_str(wifi_err_reason_t wifi_err_reason) {
    switch (wifi_err_reason) {
        case WIFI_REASON_UNSPECIFIED:
            return "WIFI_REASON_UNSPECIFIED";
        case WIFI_REASON_AUTH_EXPIRE:
            return "WIFI_REASON_AUTH_EXPIRE";
        case WIFI_REASON_AUTH_LEAVE:
            return "WIFI_REASON_AUTH_LEAVE";
        case WIFI_REASON_ASSOC_EXPIRE:
            return "WIFI_REASON_ASSOC_EXPIRE";
        case WIFI_REASON_ASSOC_TOOMANY:
            return "WIFI_REASON_ASSOC_TOOMANY";
        case WIFI_REASON_NOT_AUTHED:
            return "WIFI_REASON_NOT_AUTHED";
        case WIFI_REASON_NOT_ASSOCED:
            return "WIFI_REASON_NOT_ASSOCED";
        case WIFI_REASON_ASSOC_LEAVE:
            return "WIFI_REASON_ASSOC_LEAVE";
        case WIFI_REASON_ASSOC_NOT_AUTHED:
            return "WIFI_REASON_ASSOC_NOT_AUTHED";
        case WIFI_REASON_DISASSOC_PWRCAP_BAD:
            return "WIFI_REASON_DISASSOC_PWRCAP_BAD";
        case WIFI_REASON_DISASSOC_SUPCHAN_BAD:
            return "WIFI_REASON_DISASSOC_SUPCHAN_BAD";
        case WIFI_REASON_BSS_TRANSITION_DISASSOC:
            return "WIFI_REASON_BSS_TRANSITION_DISASSOC";
        case WIFI_REASON_IE_INVALID:
            return "WIFI_REASON_IE_INVALID";
        case WIFI_REASON_MIC_FAILURE:
            return "WIFI_REASON_MIC_FAILURE";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            return "WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT";
        case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
            return "WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT";
        case WIFI_REASON_IE_IN_4WAY_DIFFERS:
            return "WIFI_REASON_IE_IN_4WAY_DIFFERS";
        case WIFI_REASON_GROUP_CIPHER_INVALID:
            return "WIFI_REASON_GROUP_CIPHER_INVALID";
        case WIFI_REASON_PAIRWISE_CIPHER_INVALID:
            return "WIFI_REASON_PAIRWISE_CIPHER_INVALID";
        case WIFI_REASON_AKMP_INVALID:
            return "WIFI_REASON_AKMP_INVALID";
        case WIFI_REASON_UNSUPP_RSN_IE_VERSION:
            return "WIFI_REASON_UNSUPP_RSN_IE_VERSION";
        case WIFI_REASON_INVALID_RSN_IE_CAP:
            return "WIFI_REASON_INVALID_RSN_IE_CAP";
        case WIFI_REASON_802_1X_AUTH_FAILED:
            return "WIFI_REASON_802_1X_AUTH_FAILED";
        case WIFI_REASON_CIPHER_SUITE_REJECTED:
            return "WIFI_REASON_CIPHER_SUITE_REJECTED";
        case WIFI_REASON_TDLS_PEER_UNREACHABLE:
            return "WIFI_REASON_TDLS_PEER_UNREACHABLE";
        case WIFI_REASON_TDLS_UNSPECIFIED:
            return "WIFI_REASON_TDLS_UNSPECIFIED";
        case WIFI_REASON_SSP_REQUESTED_DISASSOC:
            return "WIFI_REASON_SSP_REQUESTED_DISASSOC";
        case WIFI_REASON_NO_SSP_ROAMING_AGREEMENT:
            return "WIFI_REASON_NO_SSP_ROAMING_AGREEMENT";
        case WIFI_REASON_BAD_CIPHER_OR_AKM:
            return "WIFI_REASON_BAD_CIPHER_OR_AKM";
        case WIFI_REASON_NOT_AUTHORIZED_THIS_LOCATION:
            return "WIFI_REASON_NOT_AUTHORIZED_THIS_LOCATION";
        case WIFI_REASON_SERVICE_CHANGE_PERCLUDES_TS:
            return "WIFI_REASON_SERVICE_CHANGE_PERCLUDES_TS";
        case WIFI_REASON_UNSPECIFIED_QOS:
            return "WIFI_REASON_UNSPECIFIED_QOS";
        case WIFI_REASON_NOT_ENOUGH_BANDWIDTH:
            return "WIFI_REASON_NOT_ENOUGH_BANDWIDTH";
        case WIFI_REASON_MISSING_ACKS:
            return "WIFI_REASON_MISSING_ACKS";
        case WIFI_REASON_EXCEEDED_TXOP:
            return "WIFI_REASON_EXCEEDED_TXOP";
        case WIFI_REASON_STA_LEAVING:
            return "WIFI_REASON_STA_LEAVING";
        case WIFI_REASON_END_BA:
            return "WIFI_REASON_END_BA";
        case WIFI_REASON_UNKNOWN_BA:
            return "WIFI_REASON_UNKNOWN_BA";
        case WIFI_REASON_TIMEOUT:
            return "WIFI_REASON_TIMEOUT";
        case WIFI_REASON_PEER_INITIATED:
            return "WIFI_REASON_PEER_INITIATED";
        case WIFI_REASON_AP_INITIATED:
            return "WIFI_REASON_AP_INITIATED";
        case WIFI_REASON_INVALID_FT_ACTION_FRAME_COUNT:
            return "WIFI_REASON_INVALID_FT_ACTION_FRAME_COUNT";
        case WIFI_REASON_INVALID_PMKID:
            return "WIFI_REASON_INVALID_PMKID";
        case WIFI_REASON_INVALID_MDE:
            return "WIFI_REASON_INVALID_MDE";
        case WIFI_REASON_INVALID_FTE:
            return "WIFI_REASON_INVALID_FTE";
        case WIFI_REASON_TRANSMISSION_LINK_ESTABLISH_FAILED:
            return "WIFI_REASON_TRANSMISSION_LINK_ESTABLISH_FAILED";
        case WIFI_REASON_ALTERATIVE_CHANNEL_OCCUPIED:
            return "WIFI_REASON_ALTERATIVE_CHANNEL_OCCUPIED";
        case WIFI_REASON_BEACON_TIMEOUT:
            return "WIFI_REASON_BEACON_TIMEOUT";
        case WIFI_REASON_NO_AP_FOUND:
            return "WIFI_REASON_NO_AP_FOUND";
        case WIFI_REASON_AUTH_FAIL:
            return "WIFI_REASON_AUTH_FAIL";
        case WIFI_REASON_ASSOC_FAIL:
            return "WIFI_REASON_ASSOC_FAIL";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "WIFI_REASON_HANDSHAKE_TIMEOUT";
        case WIFI_REASON_CONNECTION_FAIL:
            return "WIFI_REASON_CONNECTION_FAIL";
        case WIFI_REASON_AP_TSF_RESET:
            return "WIFI_REASON_AP_TSF_RESET";
        case WIFI_REASON_ROAMING:
            return "WIFI_REASON_ROAMING";
        case WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG:
            return "WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG";
        case WIFI_REASON_SA_QUERY_TIMEOUT:
            return "WIFI_REASON_SA_QUERY_TIMEOUT";
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
            return "WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY";
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
            return "WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD";
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
            return "WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD";
    }
    return "UNKNOWN";
}
