// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Inspired by https://github.com/sle118/squeezelite-esp32/tree/SqueezeAmp.32.1681.master-v4.3/components/wifi-manager

#include "network.h"

#include <time.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "NetworkSm.h"
#include "config.h"
#include "mem_util.h"
#include "network_ethernet.h"
#include "network_priv.h"
#include "network_wifi.h"
#include "sdkconfig.h"
#include "uc_events.h"

static const char *const TAG = "NET";

static bool enable_sntp = true;

EventGroupHandle_t eth_event_group = nullptr;
static const int   ETH_LINK_UP_BIT = BIT0;
static const int   ETH_GOT_IP_BIT = BIT1;

static QueueHandle_t network_queue;
static TaskHandle_t  task_network_manager;

static NetworkSm networkSm;

static void network_task(void *pvParameters);
static void queue_sm_event(NetworkSm::EventId event);

esp_err_t network_start() {
    if (network_queue) {
        return ESP_ERR_INVALID_STATE;
    }

    eth_event_group = xEventGroupCreate();
    xEventGroupClearBits(eth_event_group, ETH_LINK_UP_BIT | ETH_GOT_IP_BIT);

    ESP_LOGD(TAG, "Creating message queue");
    network_queue = xQueueCreate(3, sizeof(queue_message));

    ESP_LOGD(TAG, "Creating network task");
    ESP_RETURN_ON_FALSE(
        xTaskCreate(&network_task, "network", 4096, NULL, CONFIG_NETWORK_MANAGER_TASK_PRIORITY, &task_network_manager),
        ESP_FAIL, TAG, "Failed to create network task");

    return ESP_OK;
}

/// @brief Network state machine processing task.
/// @param pvParameters
static void network_task(void *pvParameters) {
    queue_message msg;
    BaseType_t    xStatus;

    networkSm.start();

    trigger_start_event();

    for (;;) {
        xStatus = xQueueReceive(network_queue, &msg, portMAX_DELAY);

        if (xStatus == pdTRUE) {
            auto oldState = networkSm.stateId;
            // dispatch event to the synchronous SM
            if (msg.event >= NetworkSm::EventIdCount) {
                ESP_LOGE(TAG, "Invalid event: %u", msg.event);
                FREE_AND_NULL(msg.ssid);
                FREE_AND_NULL(msg.password);
                FREE_AND_NULL(msg.sta_disconnected_event);
                continue;
            }
            NetworkSm::EventId eventId = static_cast<NetworkSm::EventId>(msg.event);
            ESP_LOGI(TAG, "Dispatching event: %s => %s", NetworkSm::eventIdToString(eventId),
                     NetworkSm::stateIdToString(networkSm.stateId));
            networkSm.setEventParameters(&msg);
            networkSm.dispatchEvent(eventId);
            auto newState = networkSm.stateId;
            ESP_LOGI(TAG, "SM transition: %s -> %s", NetworkSm::stateIdToString(oldState),
                     NetworkSm::stateIdToString(newState));
        }
    }

    vTaskDelete(NULL);
}

static void queue_sm_event(NetworkSm::EventId event) {
    queue_message msg;
    memset(&msg, 0, sizeof(msg));

    ESP_LOGI(TAG, "Posting event: %s", NetworkSm::eventIdToString(event));
    msg.event = static_cast<uint8_t>(event);
    xQueueSendToBack(network_queue, &msg, portMAX_DELAY);
}

void trigger_start_event() {
    queue_sm_event(NetworkSm::EventId::START);
}

void trigger_init_fail_event() {
    queue_sm_event(NetworkSm::EventId::NET_INIT_FAIL);
}

void trigger_init_success_event() {
    queue_sm_event(NetworkSm::EventId::NET_INIT_SUCCESS);
}

void trigger_connected_event() {
    queue_sm_event(NetworkSm::EventId::CONNECTED);
}

void trigger_link_up_event() {
    queue_sm_event(NetworkSm::EventId::ETH_LINK_UP);
}

void trigger_link_down_event() {
    queue_sm_event(NetworkSm::EventId::ETH_LINK_DOWN);
}

void trigger_wifi_got_ip_event() {
    queue_sm_event(NetworkSm::EventId::WIFI_GOT_IP);
}

void trigger_eth_got_ip_event() {
    queue_sm_event(NetworkSm::EventId::ETH_GOT_IP);
}

void trigger_eth_fallback_event() {
    queue_sm_event(NetworkSm::EventId::ETH_FALLBACK);
}

void trigger_configure_wifi_event() {
    queue_sm_event(NetworkSm::EventId::CONFIGURE_WIFI);
}

void trigger_delete_wifi_event() {
    queue_sm_event(NetworkSm::EventId::DELETE_WIFI);
}

void trigger_connect_to_ap_event(const char *ssid, const char *password) {
    queue_message msg;
    memset(&msg, 0, sizeof(msg));
    NetworkSm::EventId event = NetworkSm::EventId::CONNECT_TO_AP;

    ESP_LOGI(TAG, "Posting event %s (%s) pwd=%s", NetworkSm::eventIdToString(event), ssid ? ssid : "<null>",
             password ? "****" : "<null>");
    msg.event = static_cast<uint8_t>(event);
    // FIXME wifi command parameters should not be strings!
    msg.ssid = strdup_to_psram(ssid);
    if (password && strlen(password) > 0) {
        msg.password = strdup_to_psram(password);
    }

    xQueueSendToBack(network_queue, &msg, portMAX_DELAY);
}

void trigger_lost_connection_event(wifi_event_sta_disconnected_t *disconnected_event) {
    queue_message msg;
    memset(&msg, 0, sizeof(msg));
    NetworkSm::EventId event = NetworkSm::EventId::LOST_CONNECTION;
    ESP_LOGI(TAG, "Posting event: %s (%u)", NetworkSm::eventIdToString(event), disconnected_event->reason);
    msg.event = static_cast<uint8_t>(event);
    msg.sta_disconnected_event =
        (wifi_event_sta_disconnected_t *)clone_to_psram(disconnected_event, sizeof(wifi_event_sta_disconnected_t));
    if (msg.sta_disconnected_event) {
        xQueueSendToBack(network_queue, &msg, portMAX_DELAY);
    } else {
        ESP_LOGE(TAG, "Unable to post lost connection event due to failed memory allocation.");
    }
}

// ----------------------------------------------------------------------------
// Quick and dirty Improv integration
// ----------------------------------------------------------------------------

// TODO refactor, only use UC_EVENT_BUTTON_CLICK
void trigger_button_press_event() {
    queue_sm_event(NetworkSm::EventId::BUTTON_PRESS);
}

void trigger_reboot_event() {
    queue_sm_event(NetworkSm::EventId::REBOOT);
}

void trigger_timer_event() {
    queue_sm_event(NetworkSm::EventId::TIMER);
}

void trigger_improv_authorized_timeout_event() {
    queue_sm_event(NetworkSm::EventId::IMPROV_AUTHORIZED_TIMEOUT);
}

void trigger_improv_ble_connect_event() {
    queue_sm_event(NetworkSm::EventId::IMPROV_BLE_CONNECT);
}

void trigger_improv_ble_disconnect_event() {
    queue_sm_event(NetworkSm::EventId::IMPROV_BLE_DISCONNECT);
}

// ----------------------------------------------------------------------------

void network_start_stop_dhcp_client(esp_netif_t *netif, bool start) {
    esp_netif_dhcp_status_t status;
    esp_err_t               err = ESP_OK;
    ESP_LOGD(TAG, "Checking if DHCP client for STA interface is running");
    if (!netif) {
        ESP_LOGE(TAG, "Invalid adapter. Cannot start/stop dhcp. ");
        return;
    }
    if ((err = esp_netif_dhcpc_get_status(netif, &status)) != ESP_OK) {
        ESP_LOGE(TAG, "Error retrieving dhcp status : %s", esp_err_to_name(err));
        return;
    }
    switch (status) {
        case ESP_NETIF_DHCP_STARTED:
            if (start) {
                ESP_LOGD(TAG, "DHCP client already started");
            } else {
                ESP_LOGI(TAG, "Stopping DHCP client");
                err = esp_netif_dhcpc_stop(netif);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Error stopping DHCP Client : %s", esp_err_to_name(err));
                }
            }
            break;
        case ESP_NETIF_DHCP_STOPPED:
            if (start) {
                ESP_LOGI(TAG, "Starting DHCP client");
                err = esp_netif_dhcpc_start(netif);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Error stopping DHCP Client : %s", esp_err_to_name(err));
                }
            } else {
                ESP_LOGI(TAG, "DHCP client already started");
            }
            break;
        case ESP_NETIF_DHCP_INIT:
            if (start) {
                ESP_LOGI(TAG, "Starting DHCP client");
                err = esp_netif_dhcpc_start(netif);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Error stopping DHCP Client : %s", esp_err_to_name(err));
                }
            } else {
                ESP_LOGI(TAG, "Stopping DHCP client");
                err = esp_netif_dhcpc_stop(netif);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Error stopping DHCP Client : %s", esp_err_to_name(err));
                }
            }
            break;

        default:
            ESP_LOGW(TAG, "Unknown DHCP status");
            break;
    }
}

/// Event handler for Ethernet events
void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    uint8_t mac_addr[6] = {0};
    // we can get the ethernet driver handle from event data
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
        case ETHERNET_EVENT_CONNECTED: {
            esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
            ESP_LOGI(TAG, "Ethernet Link Up, HW Addr %02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0], mac_addr[1],
                     mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
            wifi_disconnect();
            set_eth_led_brightness(Config::instance().getEthLedBrightness());
            if (eth_event_group) {
                xEventGroupSetBits(eth_event_group, ETH_LINK_UP_BIT);
            }
            trigger_link_up_event();
            break;
        }
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Ethernet Link Down");
            set_eth_led_brightness(0);
            if (eth_event_group) {
                xEventGroupClearBits(eth_event_group, ETH_LINK_UP_BIT | ETH_GOT_IP_BIT);
            }
            trigger_link_down_event();
            break;
        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "Ethernet Started");
            trigger_init_success_event();
            break;
        case ETHERNET_EVENT_STOP:
            ESP_LOGI(TAG, "Ethernet Stopped");
            set_eth_led_brightness(0);
            break;
        default:
            break;
    }
}

/// IP event handler
void network_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    switch (event_id) {
        case IP_EVENT_ETH_GOT_IP:
        case IP_EVENT_STA_GOT_IP: {
            ip_event_got_ip_t         *event = (ip_event_got_ip_t *)event_data;
            const esp_netif_ip_info_t *ip_info = &event->ip_info;

            ESP_LOGI(TAG, "Got an IP address from interface %s. IP=" IPSTR ", Gateway=" IPSTR ", NetMask=" IPSTR ", %s",
                     event_id == IP_EVENT_ETH_GOT_IP   ? "ETH"
                     : event_id == IP_EVENT_STA_GOT_IP ? "WiFi"
                                                       : "Unknown",
                     IP2STR(&ip_info->ip), IP2STR(&ip_info->gw), IP2STR(&ip_info->netmask),
                     event->ip_changed ? "Address was changed" : "Address unchanged");

            event_id == IP_EVENT_ETH_GOT_IP ? trigger_eth_got_ip_event() : trigger_wifi_got_ip_event();

            if (eth_event_group) {
                xEventGroupSetBits(eth_event_group, ETH_GOT_IP_BIT);
            }

            // FIXME put in state machine!
            if (enable_sntp) {
                // TODO are further disconnect & connect network events handled internally by SNTP?
                enable_sntp = false;
                esp_netif_sntp_start();
            }

            break;
        }
        case IP_EVENT_STA_LOST_IP:
            ESP_LOGI(TAG, "IP_EVENT_STA_LOST_IP");
            break;
        case IP_EVENT_AP_STAIPASSIGNED:
            ESP_LOGI(TAG, "IP_EVENT_AP_STAIPASSIGNED");
            break;
        case IP_EVENT_GOT_IP6:
            ESP_LOGI(TAG, "IP_EVENT_GOT_IP6");
            break;
        default:
            break;
    }
}

void on_got_time(struct timeval *tv) {
    struct tm *timeinfo = localtime(&tv->tv_sec);

    char buffer[50];
    strftime(buffer, sizeof(buffer), "%c", timeinfo);
    auto server = esp_sntp_getservername(0) ? esp_sntp_getservername(0) : ipaddr_ntoa(esp_sntp_getserver(0));
    ESP_LOGI(TAG, "SNTP update %s: %s", server ? server : "", buffer);
}

/// @brief Initialize SNTP, use DHCP provided NTP server (option 042) with pool.ntp.org as fallback.
/// @details This requires at least 2 SNTP servers (CONFIG_LWIP_SNTP_MAX_SERVERS=2) and CONFIG_LWIP_DHCP_GET_NTP_SRV=y
/// @see
/// https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/network/esp_netif.html#use-both-static-and-dynamic-servers
/// @return ESP_OK or ESP_FAIL
esp_err_t init_sntp() {
    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp_config.start = false;
    sntp_config.sync_cb = on_got_time;
    sntp_config.server_from_dhcp = true;
    sntp_config.renew_servers_after_new_IP = true;
    sntp_config.index_of_first_server = 1;
    sntp_config.ip_event_to_renew = IP_EVENT_STA_GOT_IP;  // TODO OR-mask? what about IP_EVENT_ETH_GOT_IP?
    return esp_netif_sntp_init(&sntp_config);
}

bool is_eth_link_up() {
    if (!eth_event_group) {
        return false;
    }
    return (xEventGroupGetBits(eth_event_group) & (ETH_LINK_UP_BIT | ETH_GOT_IP_BIT)) != 0;
}

bool is_eth_connected(void) {
    if (!eth_event_group) {
        return false;
    }
    return (xEventGroupGetBits(eth_event_group) & ETH_LINK_UP_BIT) != 0;
}

void network_set_hostname(esp_netif_t *interface) {
    auto hostname = Config::instance().getHostName();

    ESP_LOGD(TAG, "Setting host name to : %s", hostname);
    esp_err_t err = esp_netif_set_hostname(interface, hostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to set host name. Error: %s", esp_err_to_name(err));
    }
}

bool network_is_interface_connected(esp_netif_t *interface) {
    esp_err_t           err = ESP_OK;
    esp_netif_ip_info_t ipInfo;

    if (!interface) {
        return false;
    }

    if (!esp_netif_is_netif_up(interface)) {
        return false;
    }

    err = esp_netif_get_ip_info(interface, &ipInfo);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "esp_netif_get_ip_info returned %s", esp_err_to_name(err));
    }

    return ((err == ESP_OK) && (ipInfo.ip.addr != IPADDR_ANY));
}

esp_err_t network_get_ip_info_for_netif(esp_netif_t *netif, esp_netif_ip_info_t *ipInfo) {
    esp_netif_ip_info_t loc_ip_info;

    if (!ipInfo) {
        ESP_LOGE(TAG, "Invalid pointer for ipInfo");
        return ESP_ERR_INVALID_ARG;
    }
    if (!netif) {
        ESP_LOGE(TAG, "Invalid pointer for netif");
        return ESP_ERR_INVALID_ARG;
    }

    memset(ipInfo, 0, sizeof(esp_netif_ip_info_t));
    esp_err_t err = esp_netif_get_ip_info(netif, &loc_ip_info);
    if (err == ESP_OK) {
        ip4_addr_set(&(ipInfo->ip), &loc_ip_info.ip);
        ip4_addr_set(&(ipInfo->netmask), &loc_ip_info.netmask);
        ip4_addr_set(&(ipInfo->gw), &loc_ip_info.gw);
    }
    return err;
}