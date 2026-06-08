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
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip6_addr.h"

#include "NetworkSm.h"
#include "config.h"
#include "mem_util.h"
#include "network_ethernet.h"
#include "network_priv.h"
#include "network_wifi.h"
#include "sdkconfig.h"
#include "uc_events.h"

static const char *const TAG = "NET";

static bool sntp_enabled = false;      // set by network_start()
static bool sntp_initialized = false;  // one-shot start guard

EventGroupHandle_t eth_event_group = nullptr;
static const int   ETH_LINK_UP_BIT = BIT0;
static const int   ETH_GOT_IP_BIT = BIT1;

static QueueHandle_t network_queue;
static TaskHandle_t  task_network_manager;

static NetworkSm networkSm;

static void network_task(void *pvParameters);
static void queue_sm_event(NetworkSm::EventId event);
static void apply_custom_dns_if_any(esp_netif_t *netif);

#ifdef __cplusplus
extern "C" {
#endif

// Manually declare the private ESP-IDF function signature
void esp_netif_sntp_renew_servers(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

#ifdef __cplusplus
}
#endif

#if CONFIG_LWIP_IPV6
static void create_ipv6_linklocal(esp_netif_t *netif, const char *if_name) {
    if (!netif) {
        ESP_LOGW(TAG, "Cannot create IPv6 link-local address for %s: netif is NULL", if_name);
        return;
    }

    esp_err_t err = esp_netif_create_ip6_linklocal(netif);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Creating IPv6 link-local address for %s", if_name);
    } else if (err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to create IPv6 link-local address for %s: %s", if_name, esp_err_to_name(err));
    }
}
#endif

esp_err_t network_start(bool enable_sntp) {
    if (network_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    sntp_enabled = enable_sntp;

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

            // Free parameters from the message as they are now copied or no longer needed
            FREE_AND_NULL(msg.ssid);
            FREE_AND_NULL(msg.password);
            FREE_AND_NULL(msg.sta_disconnected_event);

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

#if CONFIG_LWIP_IPV6
            create_ipv6_linklocal(esp_netif_get_handle_from_ifkey("ETH_DEF"), "ETH");
#endif

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

            if (event->esp_netif) {
                apply_custom_dns_if_any(event->esp_netif);
            }

            if (eth_event_group && event_id == IP_EVENT_ETH_GOT_IP) {
                xEventGroupSetBits(eth_event_group, ETH_GOT_IP_BIT);
            }

            if (sntp_enabled && !sntp_initialized) {
                sntp_initialized = true;
                esp_netif_sntp_start();
                ESP_LOGI(TAG, "SNTP started on first IP event (%s)", event_id == IP_EVENT_ETH_GOT_IP ? "ETH" : "WiFi");
            }
        } break;
        case IP_EVENT_STA_LOST_IP:
            ESP_LOGI(TAG, "IP_EVENT_STA_LOST_IP");
            break;
        case IP_EVENT_AP_STAIPASSIGNED:
            ESP_LOGI(TAG, "IP_EVENT_AP_STAIPASSIGNED");
            break;
#if CONFIG_LWIP_IPV6
        case IP_EVENT_GOT_IP6: {
            ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
            const char         *if_key = "unknown";

            if (event && event->esp_netif) {
                const char *key = esp_netif_get_ifkey(event->esp_netif);
                if (key) {
                    if_key = key;
                }
            }

            ESP_LOGI(TAG, "Got IPv6 address on interface %s: %s", if_key,
                     ip6addr_ntoa((ip6_addr_t *)&event->ip6_info.ip));
            break;
        }
#endif
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

/**
 * @brief Initialize SNTP.
 *
 * Server selection logic:
 *   - No custom servers configured: DHCP option 42 at slot 0, "pool.ntp.org" at slot 1
 *     (fallback). Requires CONFIG_LWIP_SNTP_MAX_SERVERS >= 2.
 *   - One custom server configured: slot 0 = server1. DHCP and fallback disabled.
 *   - Two custom servers configured: slot 0 = server1, slot 1 = server2. DHCP and
 *     fallback disabled. Requires CONFIG_LWIP_SNTP_MAX_SERVERS >= 2.
 *
 * @return ESP_OK or error code
 */
esp_err_t init_sntp() {
    Config           &cfg = Config::instance();
    const std::string ntp1 = cfg.getNtpServer1();
    const std::string ntp2 = cfg.getNtpServer2();
    const bool        use_custom = !ntp1.empty();

    esp_sntp_config_t sntp_config;

    if (use_custom) {
        // --- Custom server(s): no DHCP, no fallback ---
        // Servers are placed starting at slot 0; DHCP slot reservation not needed.
        sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntp1.c_str());
        sntp_config.server_from_dhcp = false;
        sntp_config.renew_servers_after_new_IP = false;
        sntp_config.index_of_first_server = 0;

        if (!ntp2.empty()) {
#if CONFIG_LWIP_SNTP_MAX_SERVERS >= 2
            sntp_config.num_of_servers = 2;
            sntp_config.servers[1] = ntp2.c_str();
            ESP_LOGI(TAG, "SNTP: custom servers %s (slot0), %s (slot1)", ntp1.c_str(), ntp2.c_str());
#else
            ESP_LOGW(TAG, "Second NTP server '%s' ignored: CONFIG_LWIP_SNTP_MAX_SERVERS < 2", ntp2.c_str());
            ESP_LOGI(TAG, "SNTP: custom server %s (slot0)", ntp1.c_str());
#endif
        } else {
            ESP_LOGI(TAG, "SNTP: custom server %s (slot0)", ntp1.c_str());
        }
    } else {
        // --- No custom servers: DHCP (slot0) + pool.ntp.org fallback (slot1) ---
        sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        sntp_config.server_from_dhcp = true;
        sntp_config.renew_servers_after_new_IP = true;
        sntp_config.index_of_first_server = 1;  // reserve slot 0 for DHCP
        ESP_LOGI(TAG, "SNTP: DHCP (slot0), pool.ntp.org fallback (slot1)");
    }

    sntp_config.start = false;
    sntp_config.sync_cb = on_got_time;

    // ip_event_to_renew accepts only a single event id (not a bitmask).
    // Ethernet has priority; a second handler registered below covers WiFi.
    // Only meaningful when server_from_dhcp = true, but harmless otherwise.
    sntp_config.ip_event_to_renew = IP_EVENT_ETH_GOT_IP;

    esp_err_t ret = esp_netif_sntp_init(&sntp_config);
    if (ret != ESP_OK) {
        return ret;
    }

    // Second renew handler for WiFi path (covers IP_EVENT_STA_GOT_IP).
    // Only truly needed when server_from_dhcp = true, but registering it
    // unconditionally is harmless — renew_servers is a no-op when the
    // static server list hasn't changed.
    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, esp_netif_sntp_renew_servers, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register SNTP WiFi renew handler: %s", esp_err_to_name(ret));
    }
    return ret;
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
    return (xEventGroupGetBits(eth_event_group) & (ETH_LINK_UP_BIT | ETH_GOT_IP_BIT)) ==
           (ETH_LINK_UP_BIT | ETH_GOT_IP_BIT);
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

static bool parse_dns_addr(const std::string &value, esp_ip_addr_t *addr) {
    if (value.empty() || !addr) {
        return false;
    }

    memset(addr, 0, sizeof(*addr));

    ip4_addr_t ip4 = {};
    if (ip4addr_aton(value.c_str(), &ip4) != 0 && ip4.addr != IPADDR_ANY && ip4.addr != IPADDR_NONE) {
        addr->type = IPADDR_TYPE_V4;
        addr->u_addr.ip4.addr = ip4.addr;
        return true;
    }

#if CONFIG_LWIP_IPV6
    ip6_addr_t ip6 = {};
    if (ip6addr_aton(value.c_str(), &ip6) != 0 && !ip6_addr_isany_val(ip6)) {
        addr->type = IPADDR_TYPE_V6;
        memcpy(&addr->u_addr.ip6, &ip6, sizeof(addr->u_addr.ip6));
        return true;
    }
#endif

    return false;
}

static const char *dns_addr_to_string(const esp_ip_addr_t *addr, char *buf, size_t buf_len) {
    if (!addr || !buf || buf_len == 0) {
        return nullptr;
    }

    switch (addr->type) {
        case IPADDR_TYPE_V4:
            if (addr->u_addr.ip4.addr == IPADDR_ANY || addr->u_addr.ip4.addr == IPADDR_NONE) {
                return nullptr;
            }
            return ip4addr_ntoa_r((const ip4_addr_t *)&addr->u_addr.ip4, buf, buf_len);

#if CONFIG_LWIP_IPV6
        case IPADDR_TYPE_V6:
            if (ip6_addr_isany((const ip6_addr_t *)&addr->u_addr.ip6)) {
                return nullptr;
            }
            return ip6addr_ntoa_r((const ip6_addr_t *)&addr->u_addr.ip6, buf, buf_len);
#endif

        default:
            return nullptr;
    }
}

static esp_err_t set_dns_server(esp_netif_t *netif, const std::string &server, esp_netif_dns_type_t type) {
    if (server.empty()) {
        return ESP_OK;
    }

    esp_netif_dns_info_t dns = {};
    if (!parse_dns_addr(server, &dns.ip)) {
        ESP_LOGW(TAG, "Ignoring invalid DNS server address: %s", server.c_str());
        return ESP_OK;
    }

    char        addr_str[48] = {};
    const char *formatted = dns_addr_to_string(&dns.ip, addr_str, sizeof(addr_str));
    ESP_LOGI(TAG, "Setting DNS server: %s", formatted ? formatted : server.c_str());

    return esp_netif_set_dns_info(netif, type, &dns);
}

void apply_custom_dns() {
    esp_netif_t *netif = nullptr;
    esp_netif_t *eth_netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    esp_netif_t *wifi_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    if (is_eth_connected() && network_is_interface_connected(eth_netif)) {
        netif = eth_netif;
    } else if (network_is_interface_connected(wifi_netif)) {
        netif = wifi_netif;
    }

    if (netif) {
        apply_custom_dns_if_any(netif);
    } else {
        ESP_LOGW(TAG, "No active network interface found to apply custom DNS settings");
    }
}

static void apply_custom_dns_if_any(esp_netif_t *netif) {
    if (!netif) {
        ESP_LOGW(TAG, "apply_custom_dns_if_any: netif is NULL");
        return;
    }

    Config &cfg = Config::instance();

    std::string dns1 = cfg.getDnsServer1();
    std::string dns2 = cfg.getDnsServer2();

    if (!dns1.empty()) {
        esp_err_t err = set_dns_server(netif, dns1, ESP_NETIF_DNS_MAIN);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set main DNS server: %s", esp_err_to_name(err));
        }
    }

    if (!dns2.empty()) {
        esp_err_t err = set_dns_server(netif, dns2, ESP_NETIF_DNS_BACKUP);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set backup DNS server: %s", esp_err_to_name(err));
        }
    }
}

esp_err_t set_static_ip(esp_netif_t *netif, esp_netif_ip_info_t ip) {
    esp_err_t ret = ESP_OK;

    ret = esp_netif_dhcpc_stop(netif);
    if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGE(TAG, "Failed to stop dhcp client: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(netif, &ip), TAG, "Failed to set ip info");

    apply_custom_dns_if_any(netif);

    return ESP_OK;
}

esp_err_t apply_eth_ipv4_config(esp_netif_t *netif) {
    if (!netif) {
        ESP_LOGE(TAG, "apply_eth_ipv4_config: null netif");
        return ESP_ERR_INVALID_ARG;
    }

    network_cfg_t    nc = Config::instance().getNetwork();
    iface_net_cfg_t *eth = &nc.eth;

    if (eth->dhcp) {
        ESP_LOGI(TAG, "ETH using DHCP");
        network_start_stop_dhcp_client(netif, true);

        // Initial attempt; DHCP may overwrite this, so re-apply after GOT_IP too.
        apply_custom_dns_if_any(netif);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "ETH using static IPv4");
    esp_err_t ret = set_static_ip(netif, eth->ip);
    if (ret == ESP_OK && sntp_enabled && !sntp_initialized) {
        sntp_initialized = true;
        esp_netif_sntp_start();
        ESP_LOGI(TAG, "SNTP started after static ETH IP assignment");
    }
    return ret;
}

esp_err_t apply_wifi_ipv4_config(esp_netif_t *netif) {
    if (!netif) {
        ESP_LOGE(TAG, "apply_wifi_ipv4_config: null netif");
        return ESP_ERR_INVALID_ARG;
    }

    network_cfg_t    nc = Config::instance().getNetwork();
    iface_net_cfg_t *wifi = &nc.wifi;

    if (wifi->dhcp) {
        ESP_LOGI(TAG, "WiFi using DHCP");
        network_start_stop_dhcp_client(netif, true);

        // Initial attempt; DHCP may overwrite this, so re-apply after GOT_IP too.
        apply_custom_dns_if_any(netif);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "WiFi using static IPv4");
    esp_err_t ret = set_static_ip(netif, wifi->ip);
    if (ret == ESP_OK && sntp_enabled && !sntp_initialized) {
        sntp_initialized = true;
        esp_netif_sntp_start();
        ESP_LOGI(TAG, "SNTP started after static WiFi IP assignment");
    }
    return ret;
}
