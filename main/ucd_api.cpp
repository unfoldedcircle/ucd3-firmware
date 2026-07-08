// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ucd_api.h"

#include <stdio.h>
#include <time.h>

#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip6_addr.h"
#include "lwip/sockets.h"

#include "WebServer.h"
#include "config.h"
#include "led_pattern.h"
#include "log_router.h"
#include "network.h"
#include "sdkconfig.h"
#include "service_ir.h"
#include "string_util.h"
#include "system_stats.h"
#include "uc_events.h"

// Auto-close unauthenticated WebSocket connections after n seconds.
static const int UNAUTHENTICATED_TIMEOUT_SEC = 30;
// WebSocket client authentication check timer period in ms.
static const int AUTH_TIMER_CHECK_PERIOD_MS = 5000;
// Maximum number of WebSocket connections to close per timer call.
// Limited because of silently dropping queue work: https://github.com/espressif/esp-idf/issues/8440
static const int MAX_WS_CLOSE_COUNT = 2;

// Maximum number of WebSocket client log stream subscriptions
static const int MAX_WS_LOG_SUBS = 2;
// Throttle log message sending to avoid dropping WebSocket messages (or other responses).
// Note: with default IDF CONFIG_LWIP_UDP_RECVMBOX_SIZE=6, the delay should be set to 250 ms
// 100ms seems to be ok for IDF CONFIG_LWIP_UDP_RECVMBOX_SIZE=12
static const int WS_LOG_DELAY_MS = 100;

// Feature flag: do not send a WebSocket response for ir_send when an IR sequence is extended.
static const int API_FEATURE_FLAG_IR_REPEAT_NO_RESPONSE = BIT0;
// Feature flag: `ir_send` command supports the `hold` parameter to send ir command for x milliseconds.
static const int API_FEATURE_FLAG_IR_SEND_HOLD = BIT1;

// Available features.
static const int API_FEATURE_FLAGS = API_FEATURE_FLAG_IR_REPEAT_NO_RESPONSE | API_FEATURE_FLAG_IR_SEND_HOLD;

static const size_t SERIAL_BUFFER_SIZE_DEFAULT = 512;
static const size_t SERIAL_BUFFER_SIZE_MAX = 16384;

static const char *const TAG = "API";

static const char *msgType = "type";
static const char *msgTypeDock = "dock";
static const char *msgId = "id";
static const char *msgReqId = "req_id";
static const char *msgCommand = "command";
static const char *msgMsg = "msg";
static const char *msgCode = "code";
static const char *msgError = "error";
static const char *msgToken = "token";
static const char *msgWifiPwd = "wifi_password";
/// @brief The time in ticks to wait for the unauthenticated_fds_mutex_ to become available.
static TickType_t AUTH_MUTEX_BLOCK_TIME = pdMS_TO_TICKS(200);

std::string get_uptime(void) {
    char     timestring[20];
    uint32_t seconds = static_cast<uint32_t>(esp_timer_get_time() / 1000 / 1000);
    uint16_t days = static_cast<uint16_t>(seconds / (24 * 3600));
    seconds = seconds % (24 * 3600);
    uint8_t hours = seconds / 3600;
    seconds = seconds % 3600;
    uint8_t minutes = seconds / 60;
    seconds = seconds % 60;
    snprintf(timestring, sizeof(timestring), "%u days %02u:%02u:%02lu", days, hours, minutes, seconds);

    return timestring;
}

void fill_sysinfo_to_json(cJSON *root) {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    Config &cfg = Config::instance();

    cJSON_AddStringToObject(root, "name", cfg.getFriendlyName().c_str());
    cJSON_AddStringToObject(root, "hostname", cfg.getHostName());
    cJSON_AddStringToObject(root, "version", cfg.getSoftwareVersion().c_str());
    cJSON_AddStringToObject(root, "serial", cfg.getSerial());
    cJSON_AddStringToObject(root, "model", cfg.getModel());
    cJSON_AddStringToObject(root, "revision", cfg.getRevision());
    cJSON_AddNumberToObject(root, "features", API_FEATURE_FLAGS);
    cJSON_AddNumberToObject(root, "led_brightness", cfg.getLedBrightness());
#if defined(ETH_LED_PWM)
    cJSON_AddNumberToObject(root, "eth_led_brightness", cfg.getEthLedBrightness());
#endif
    cJSON_AddBoolToObject(root, "ir_learning", InfraredService::getInstance().isIrLearning());
    cJSON_AddBoolToObject(root, "ethernet", is_eth_connected());
    cJSON_AddBoolToObject(root, "wifi", is_wifi_up());
    cJSON_AddStringToObject(root, "ssid", cfg.getWifiSsid().c_str());
    cJSON_AddNumberToObject(root, "volume", cfg.getVolume());
    cJSON_AddStringToObject(root, "uptime", get_uptime().c_str());
    cJSON_AddBoolToObject(root, "ntp", cfg.isNtpEnabled());
    cJSON_AddStringToObject(root, "reset_reason", getResetReason());

    if (cfg.isNtpEnabled()) {
        time_t    now;
        char      strftime_buf[64];
        struct tm timeinfo = {};

        time(&now);
        if (localtime_r(&now, &timeinfo) && timeinfo.tm_year >= (2020 - 1900)) {
            strftime(strftime_buf, sizeof(strftime_buf), "%FT%T%z", &timeinfo);
            cJSON_AddStringToObject(root, "time", strftime_buf);
        }
    }

    char buf[1 + 8 * sizeof(uint32_t)];
    utoa(heap_caps_get_free_size(MALLOC_CAP_INTERNAL), buf, 10);
    cJSON_AddStringToObject(root, "free_heap", buf);

    if (board_get_poe_switch_pin() != GPIO_NUM_NC) {
        cJSON_AddNumberToObject(root, "poe_mode", cfg.getPoeVoltageMode());
    }
}

char *get_sysinfo_json(void) {
    cJSON *root = cJSON_CreateObject();

    fill_sysinfo_to_json(root);

    char *sys_info = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return sys_info;
}

static void restart(void *arg) {
    WebServer *web = static_cast<WebServer *>(arg);
    if (web) {
        web->disconnectAll();
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, UC_EVENT_REBOOT, NULL, 0, pdMS_TO_TICKS(500)));

    vTaskDelay(200 / portTICK_PERIOD_MS);
    esp_restart();
}

static void factory_reset(void *arg) {
    WebServer *web = static_cast<WebServer *>(arg);
    if (web) {
        web->disconnectAll();
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, UC_ACTION_RESET, NULL, 0, pdMS_TO_TICKS(500)));
}

void schedule_restart(WebServer *web, uint16_t delay_ms, bool reset = false) {
    const esp_timer_create_args_t timer_args = {
        .callback = reset ? &factory_reset : &restart,
        .arg = web,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "restart",
        .skip_unhandled_events = false,
    };

    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_once(periodic_timer, delay_ms * 1000));
}

static void disconnect_clients(void *arg) {
    WebServer *web = static_cast<WebServer *>(arg);
    if (web) {
        web->disconnectAll();
    }
}

void schedule_disconnect_all(WebServer *web, uint16_t delay_ms) {
    const esp_timer_create_args_t timer_args = {
        .callback = &disconnect_clients,
        .arg = web,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "disconnect_all",
        .skip_unhandled_events = false,
    };

    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_once(timer, delay_ms * 1000));
}

/// @brief cJSON helper function to read an integer
/// @param root json object
/// @param field field name
/// @param ok optional parameter to set if retrieval was successful
/// @return integer value of the field, or 0 if it is not a number. Use the ok parameter if 0 is a valid number of the
/// field.
int cjson_get_int(const cJSON *root, const char *field, bool *ok = NULL) {
    cJSON *item = cJSON_GetObjectItem(root, field);
    if (cJSON_IsNumber(item)) {
        if (ok) {
            *ok = true;
        }
        return item->valueint;
    }
    if (ok) {
        *ok = false;
    }
    return 0;
}

const char *cjson_get_string(const cJSON *root, const char *field, const char *defval = NULL) {
    cJSON      *item = cJSON_GetObjectItem(root, field);
    const char *val = cJSON_GetStringValue(item);

    return val ? val : defval;
}

bool cjson_get_bool(const cJSON *root, const char *field, bool *ok = NULL) {
    cJSON *item = cJSON_GetObjectItem(root, field);
    if (cJSON_IsBool(item)) {
        if (ok) {
            *ok = true;
        }
        return cJSON_IsTrue(item);
    }
    if (ok) {
        *ok = false;
    }
    return false;
}

static bool parse_ipv4_addr(const char *value, ip4_addr_t *addr) {
    if (!value || !addr) {
        return false;
    }

    ip4_addr_t parsed = {};
    if (ip4addr_aton(value, &parsed) == 0) {
        return false;
    }

    *addr = parsed;
    return true;
}

static bool parse_required_ipv4_addr(const cJSON *root, const char *field, ip4_addr_t *addr, cJSON *responseDoc) {
    const char *value = cjson_get_string(root, field, nullptr);
    if (!value || strlen(value) == 0) {
        cJSON_AddStringToObject(responseDoc, msgError, "Missing IPv4 address field");
        return false;
    }

    if (!parse_ipv4_addr(value, addr) || addr->addr == IPADDR_ANY || addr->addr == IPADDR_NONE) {
        cJSON_AddStringToObject(responseDoc, msgError, "Invalid IPv4 address");
        return false;
    }

    return true;
}

static bool parse_optional_ipv4_addr(const cJSON *root, const char *field, ip4_addr_t *addr, cJSON *responseDoc) {
    const char *value = cjson_get_string(root, field, nullptr);
    if (!value || strlen(value) == 0) {
        addr->addr = IPADDR_ANY;
        return true;
    }

    if (!parse_ipv4_addr(value, addr) || addr->addr == IPADDR_NONE) {
        cJSON_AddStringToObject(responseDoc, msgError, "Invalid IPv4 address");
        return false;
    }

    return true;
}

static const char *ipv4_addr_to_string(const ip4_addr_t *addr, char *buf, size_t buf_len) {
    if (!addr || !buf || buf_len == 0 || addr->addr == IPADDR_ANY || addr->addr == IPADDR_NONE) {
        return nullptr;
    }

    return ip4addr_ntoa_r(addr, buf, buf_len);
}

static bool parse_dns_addr(const char *value, esp_ip_addr_t *addr) {
    if (!value || !addr || strlen(value) == 0) {
        return false;
    }

    memset(addr, 0, sizeof(*addr));

    ip4_addr_t ip4 = {};
    if (ip4addr_aton(value, &ip4) != 0 && ip4.addr != IPADDR_ANY && ip4.addr != IPADDR_NONE) {
        addr->type = IPADDR_TYPE_V4;
        addr->u_addr.ip4.addr = ip4.addr;
        return true;
    }

#if CONFIG_LWIP_IPV6
    ip6_addr_t ip6 = {};
    if (ip6addr_aton(value, &ip6) != 0 && !ip6_addr_isany_val(ip6)) {
        addr->type = IPADDR_TYPE_V6;
        memcpy(&addr->u_addr.ip6, &ip6, sizeof(addr->u_addr.ip6));
        return true;
    }
#endif

    return false;
}

static const char *ip_addr_to_string(const esp_ip_addr_t *addr, char *buf, size_t buf_len) {
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

static bool normalize_dns_addr_string(const char *value, std::string *normalized) {
    if (!normalized) {
        return false;
    }

    esp_ip_addr_t addr = {};
    if (!parse_dns_addr(value, &addr)) {
        return false;
    }

    char        buf[48] = {};
    const char *str = ip_addr_to_string(&addr, buf, sizeof(buf));
    if (!str) {
        return false;
    }

    *normalized = str;
    return true;
}

#if CONFIG_LWIP_IPV6
static const char *ipv6_addr_type_to_string(esp_ip6_addr_type_t type) {
    switch (type) {
        case ESP_IP6_ADDR_IS_UNKNOWN:
            return "unknown";
        case ESP_IP6_ADDR_IS_GLOBAL:
            return "global";
        case ESP_IP6_ADDR_IS_LINK_LOCAL:
            return "link_local";
        case ESP_IP6_ADDR_IS_SITE_LOCAL:
            return "site_local";
        case ESP_IP6_ADDR_IS_UNIQUE_LOCAL:
            return "unique_local";
        case ESP_IP6_ADDR_IS_IPV4_MAPPED_IPV6:
            return "ipv4_mapped";
        default:
            return "unknown";
    }
}

static void add_ipv6_addresses_to_json(cJSON *root, esp_netif_t *netif) {
    if (!root || !netif) {
        return;
    }

    esp_ip6_addr_t addresses[CONFIG_LWIP_IPV6_NUM_ADDRESSES] = {};
    int            addr_count = esp_netif_get_all_ip6(netif, addresses);

    if (addr_count <= 0) {
        return;
    }

    cJSON *ipv6 = cJSON_CreateObject();
    cJSON *array = cJSON_AddArrayToObject(ipv6, "addresses");

    if (!array) {
        cJSON_Delete(ipv6);
        return;
    }

    for (int i = 0; i < addr_count; i++) {
        const char *addr = ip6addr_ntoa((ip6_addr_t *)&addresses[i]);
        if (!addr) {
            continue;
        }

        cJSON *item = cJSON_CreateObject();
        if (!item) {
            continue;
        }

        cJSON_AddStringToObject(item, "address", addr);
        cJSON_AddStringToObject(item, "type", ipv6_addr_type_to_string(esp_netif_ip6_get_addr_type(&addresses[i])));

        cJSON_AddItemToArray(array, item);
    }

    if (cJSON_GetArraySize(array) > 0) {
        cJSON_AddItemToObject(root, "ipv6", ipv6);
    } else {
        cJSON_Delete(ipv6);
    }
}
#endif  // CONFIG_LWIP_IPV6

DockApi::DockApi(Config *config, WebServer *web, port_map_t ports)
    : config_(config),
      web_(web),
      ports_(ports),
      sockfdSendIR_(-1),
      unauthenticated_fds_mutex_(xSemaphoreCreateMutex()),
      auth_timer_(nullptr),
      serial_event_mutex_(xSemaphoreCreateMutex()),
      log_subscribers_mutex_(xSemaphoreCreateMutex()),
      log_sender_task_handle_(nullptr),
      log_queue_(nullptr) {
    assert(config_);
    assert(web_);
    assert(unauthenticated_fds_mutex_);
    assert(serial_event_mutex_);
    assert(log_subscribers_mutex_);

    // Initialize log router
    log_router_init();

    web_->onWsEvent([this](httpd_req_t *req, int sockfd, WsTypeEnum type, uint8_t *payload, size_t length,
                           bool authenticated) -> esp_err_t {
        switch (type) {
            case WS_CONNECTED: {
#if CONFIG_LWIP_IPV6
                struct sockaddr_storage addr_storage = {};
#else
                struct sockaddr_in addr_in = {};
#endif
                char buf[48] = {0};
#if CONFIG_LWIP_IPV6
                if (WebServer::getRemoteIp(sockfd, reinterpret_cast<struct sockaddr_in6 *>(&addr_storage)) == ESP_OK) {
                    const char *ip = nullptr;
                    uint16_t    port = 0;

                    if (addr_storage.ss_family == AF_INET) {
                        auto *addr4 = reinterpret_cast<struct sockaddr_in *>(&addr_storage);
                        ip = inet_ntoa_r(addr4->sin_addr, buf, sizeof(buf));
                        port = ntohs(addr4->sin_port);
                        ESP_LOGI(TAG, "new WS client connection %d from: %s:%u", sockfd, ip, port);
                    } else if (addr_storage.ss_family == AF_INET6) {
                        auto *addr6 = reinterpret_cast<struct sockaddr_in6 *>(&addr_storage);
                        ip = inet6_ntoa_r(addr6->sin6_addr, buf, sizeof(buf));
                        port = ntohs(addr6->sin6_port);
                        ESP_LOGI(TAG, "new WS client connection %d from: [%s]:%u", sockfd, ip, port);
                    } else {
                        ESP_LOGW(TAG, "New WS client connection %d with unknown address family: %d", sockfd,
                                 addr_storage.ss_family);
                    }
                }
#else
                if (WebServer::getRemoteIp(sockfd, &addr_in) == ESP_OK) {
                    ESP_LOGI(TAG, "new WS client connection %d from: %s:%u", sockfd,
                             inet_ntoa_r(addr_in.sin_addr, buf, sizeof(buf)), ntohs(addr_in.sin_port));
                }
#endif

                // send auth request message
                if (authenticated) {
                    return ESP_OK;
                }

                if (xSemaphoreTake(unauthenticated_fds_mutex_, AUTH_MUTEX_BLOCK_TIME) != pdTRUE) {
                    ESP_LOGE(TAG, "Failed to lock FDs in connect");
                    return ESP_FAIL;
                }
                unauthenticated_fds_[sockfd] = esp_timer_get_time() / 1000 / 1000;
                xSemaphoreGive(unauthenticated_fds_mutex_);

                WebServer *server = static_cast<WebServer *>(req->user_ctx);
                cJSON     *response = cJSON_CreateObject();
                cJSON_AddStringToObject(response, msgType, "auth_required");
                cJSON_AddStringToObject(response, "model", config_->getModel());
                cJSON_AddStringToObject(response, "revision", config_->getRevision());
                cJSON_AddStringToObject(response, "version", config_->getSoftwareVersion().c_str());
                cJSON_AddNumberToObject(response, "features", API_FEATURE_FLAGS);
                // Attention: resp gets freed by WebServer!
                char     *resp = cJSON_PrintUnformatted(response);
                esp_err_t ret = server->sendWsTxt(sockfd, resp);
                cJSON_Delete(response);
                return ret;
            }
            case WS_DISCONNECTED: {
                ESP_LOGI(TAG, "WS client disconnected: %d", sockfd);
                // stop IR repeat if active.
                if (sockfdSendIR_ == sockfd) {
                    InfraredService::getInstance().stopSend();
                    sockfdSendIR_ = -1;
                }

                if (xSemaphoreTake(unauthenticated_fds_mutex_, AUTH_MUTEX_BLOCK_TIME) != pdTRUE) {
                    ESP_LOGE(TAG, "Failed to lock FDs in disconnect");
                } else {
                    unauthenticated_fds_.erase(sockfd);
                    xSemaphoreGive(unauthenticated_fds_mutex_);
                }

                // Remove serial event subscriptions for this client
                if (xSemaphoreTake(serial_event_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    serial_event_fds_.erase(sockfd);
                    xSemaphoreGive(serial_event_mutex_);
                }

                // Remove from log subscribers and stop the router if no one remains.
                if (xSemaphoreTake(log_subscribers_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    log_subscribers_.erase(sockfd);
                    size_t remaining = log_subscribers_.size();
                    xSemaphoreGive(log_subscribers_mutex_);
                    if (remaining == 0 && log_queue_ != nullptr) {
                        log_router_stop();
                        log_queue_ = nullptr;
                        // log_sender_task_handle_ is nulled by the task itself after self-deletion
                    }
                }

                return ESP_OK;
            }
            case WS_TEXT:
                return processRequest(req, sockfd, (const char *)payload, length, authenticated);
            case WS_BIN:
                ESP_LOGE(TAG, "Binary WebSocket message not supported");
                return ESP_ERR_NOT_SUPPORTED;
            default:
                // ignore
                return ESP_OK;
        }
    });
}

DockApi::~DockApi() {
    if (auth_timer_) {
        xTimerStop(auth_timer_, pdMS_TO_TICKS(1000));
    }
    vSemaphoreDelete(unauthenticated_fds_mutex_);
    vSemaphoreDelete(serial_event_mutex_);

    for (int i = 0; i < EXTERNAL_PORT_COUNT; i++) {
        if (bridges_[i]) {
            serial_bridge_destroy(bridges_[i]);
            bridges_[i] = nullptr;
        }
    }

    deinitSerialBuffers();

    if (log_queue_ != nullptr) {
        log_router_stop();
        log_queue_ = nullptr;
    }
    // Give the task a moment to process the sentinel and self-delete.
    // If it does not, force-kill — acceptable in a full teardown.
    if (log_sender_task_handle_) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (log_sender_task_handle_) {  // re-check after delay
            vTaskDelete(log_sender_task_handle_);
            log_sender_task_handle_ = nullptr;
        }
    }
    vSemaphoreDelete(log_subscribers_mutex_);
}

esp_err_t DockApi::init() {
    // Register external-port-mode-change event
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(UC_DOCK_EVENTS, ESP_EVENT_ANY_ID, dockEventHandler, this, NULL), TAG,
        "Registering UC_DOCK_EVENTS failed");

    if (!auth_timer_) {
        auth_timer_ =
            xTimerCreate("auth_timeout", pdMS_TO_TICKS(AUTH_TIMER_CHECK_PERIOD_MS), pdTRUE, this, authTimeoutCallback);
        if (!auth_timer_) {
            ESP_LOGE(TAG, "Failed to create WS auth timer");
            return ESP_FAIL;
        }
        ESP_RETURN_ON_FALSE(xTimerStart(auth_timer_, pdMS_TO_TICKS(3000)), ESP_FAIL, TAG,
                            "Failed to start WS auth timer");
    }

    // Initialize serial buffer configurations from persistent storage
    initSerialBuffers();

    // Initialize serial bridge instances
    bool tcp_enabled = config_->isSerialTcpEnabled();

    for (auto const &[port, ext_port] : ports_) {
        if (port < 1 || port > EXTERNAL_PORT_COUNT) {
            continue;
        }

        serial_bridge_config_t br_cfg = {
            .port_index = static_cast<uint8_t>(port),
            .uart_num = ext_port->getUartPort(),
            .uart_event_queue = ext_port->getUartEventQueue(),
            .tcp_port = static_cast<uint16_t>(4998 + port),
            .tcp_enabled = tcp_enabled,
        };
        bridges_[port - 1] = serial_bridge_create(&br_cfg);

        if (bridges_[port - 1]) {
            serial_bridge_set_rx_callback(bridges_[port - 1], serialRxCallback, this);

            if (ext_port->getMode() == RS232) {
                serial_bridge_start(bridges_[port - 1]);
            }
        }
    }

    return ESP_OK;
}

esp_err_t DockApi::processRequest(httpd_req_t *req, int sockfd, const char *text, size_t len, bool authenticated) {
    WebServer *web = static_cast<WebServer *>(req->user_ctx);
    assert(web);

    ESP_LOGD(TAG, "-> %s", text);

    cJSON *root = cJSON_ParseWithLength(text, len);
    if (root == NULL) {
        ESP_LOGW(TAG, "Error deserializing JSON");
        web->sendWsTxt(sockfd, "{\"code\": 500}");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *responseDoc = cJSON_CreateObject();

    cJSON      *item = cJSON_GetObjectItem(root, msgType);
    std::string type;
    const char *value = cJSON_GetStringValue(item);
    if (value) {
        type = value;
    }

    item = cJSON_GetObjectItem(root, msgId);
    if (item) {
        cJSON_AddItemReferenceToObject(responseDoc, msgReqId, item);
    }

    item = cJSON_GetObjectItem(root, msgCommand);
    std::string command;
    value = cJSON_GetStringValue(item);
    if (value) {
        command = value;
    }

    item = cJSON_GetObjectItem(root, msgMsg);
    std::string msg;
    value = cJSON_GetStringValue(item);
    if (value) {
        msg = value;
    }

    esp_err_t ret = ESP_FAIL;
    // default response code
    uint16_t code = 200;

    // AUTHENTICATION TO THE API
    if (type == "auth") {
        std::string message;
        cJSON_AddStringToObject(responseDoc, msgType, "authentication");

        code = 401;
        item = cJSON_GetObjectItem(root, msgToken);
        value = cJSON_GetStringValue(item);

        if (value && config_->getToken() == value) {
            // add client to authorized clients
            if (web->setAuthenticated(sockfd) == ESP_OK) {
                if (xSemaphoreTake(unauthenticated_fds_mutex_, AUTH_MUTEX_BLOCK_TIME) != pdTRUE) {
                    ESP_LOGE(TAG, "Failed to lock FDs");
                    return ESP_FAIL;
                }
                unauthenticated_fds_.erase(sockfd);
                xSemaphoreGive(unauthenticated_fds_mutex_);
                // token ok
                code = 200;
                ret = ESP_OK;
            }
        } else {
            // invalid token
            cJSON_AddStringToObject(responseDoc, msgError, "Invalid token");
            // don't disconnect, otherwise response is not sent back
            ret = ESP_OK;
        }

        goto send_response;
    }

    if (!type.empty()) {
        cJSON_AddStringToObject(responseDoc, msgType, type.c_str());
    }
    if (!command.empty()) {
        cJSON_AddStringToObject(responseDoc, msgMsg, command.c_str());
    }

    // Allowed non-authorized commands to the dock
    if (type == "dock") {
        // Get system information
        if (command == "get_sysinfo") {
            fill_sysinfo_to_json(responseDoc);
            processGetPortModes(responseDoc);
            ret = ESP_OK;
            goto send_response;
        }
        if (command == "get_stats") {
#if defined(CONFIG_FREERTOS_USE_TRACE_FACILITY) && defined(CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS) && \
    defined(CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)
            get_current_stats(responseDoc);
#else
            code = 503;
#endif
            ret = ESP_OK;
            goto send_response;
        }
    }

    // Authorized COMMANDS TO THE DOCK
    if (!authenticated) {
        ESP_LOGI(TAG, "Cannot execute command: WS connection not authorized");
        code = 401;
        goto send_response;
    }

    ret = ESP_OK;

    if (type != msgTypeDock) {
        ESP_LOGI(TAG, "Ignoring message with missing or invalid type field");
        code = 400;
    } else if (command.empty() && msg == "ping") {
        ESP_LOGD(TAG, "Sending heartbeat");
        cJSON_DeleteItemFromObject(responseDoc, msgCode);
        cJSON_AddStringToObject(responseDoc, msgMsg, "pong");
    } else if (command == "set_config") {
        bool field = false;
        bool ok = false;

        item = cJSON_GetObjectItem(root, "friendly_name");
        if (item) {
            field = true;
            value = cJSON_GetStringValue(item);
            if (value) {
                config_->setFriendlyName(value);
                // retrieve from config again, since it could be adjusted
                // TODO MdnsService.addFriendlyName(config_->getFriendlyName());
                ok = true;
            }
        }
        item = cJSON_GetObjectItem(root, msgToken);
        if (item) {
            field = true;
            value = cJSON_GetStringValue(item);
            if (value) {
                std::string token = value;
                if (token.empty() || token.length() > 40) {
                    cJSON_AddStringToObject(responseDoc, msgError, "Token length must be 4..40");
                } else {
                    ok = config_->setToken(token);
                    if (ok) {
                        schedule_disconnect_all(web, 1000);
                    }
                }
            }
        }
        if (!(field && !ok) && (cJSON_HasObjectItem(root, "ssid") || cJSON_HasObjectItem(root, msgWifiPwd))) {
            auto ssid = cjson_get_string(root, "ssid", "");
            auto pwd = cjson_get_string(root, msgWifiPwd, "");

            if (config_->setWifi(ssid, pwd)) {
                ESP_LOGD(TAG, "Saving SSID: %s", ssid);

                cJSON_AddBoolToObject(responseDoc, "reboot", true);
                ok = true;

                schedule_restart(web, 2000);
            } else {
                cJSON_AddStringToObject(responseDoc, msgError, "Invalid SSID or password");
            }
        }

        if (!ok) {
            code = 400;
        }
    } else if (command == "set_brightness") {
        bool ok = false;
        if (cJSON_HasObjectItem(root, "status_led")) {
            int brightness = cjson_get_int(root, "status_led", &ok);
            if (ok) {
                // TODO m_state->setState(States::LED_SETUP);
                ESP_LOGD(TAG, "Set LED brightness: %d", brightness);
                // set new value
                set_led_brightness(brightness);
                // persist value
                config_->setLedBrightness(brightness);
            }
        }
        if (cJSON_HasObjectItem(root, "eth_led")) {
            int brightness = cjson_get_int(root, "eth_led", &ok);
            if (ok) {
                ESP_LOGD(TAG, "Set ETH brightness: %d", brightness);
                // persist value
                config_->setEthLedBrightness(brightness);
                // set new value if ethernet link is up
                if (is_eth_link_up()) {
                    set_eth_led_brightness(config_->getEthLedBrightness());
                }
            }
        }
        if (!ok) {
            code = 400;
        }
    } else if (command == "set_volume") {
        bool ok = false;
        int  volume = cjson_get_int(root, "volume", &ok);
        if (ok && volume >= 0 && volume <= 100) {
            config_->setVolume(volume);
        } else {
            code = 400;
        }
    } else if (command == "ir_send") {
        std::string ir_code = cjson_get_string(root, "code", "");
        std::string format = cjson_get_string(root, "format", "");
        uint16_t    response = 400;

        sockfdSendIR_ = -1;

        // make sure there are no leading or trailing spaces that could interfere with PRONTO parsing
        trim(ir_code);

        ESP_LOGD(TAG, "IR Send, format=%s, code=%s", format.c_str(), ir_code.c_str());

        if (!ir_code.empty() && !format.empty()) {
            uint16_t repeat = cjson_get_int(root, "repeat");
            uint16_t hold = cjson_get_int(root, "hold");
            bool     intSide = cjson_get_bool(root, "int_side");
            bool     intTop = cjson_get_bool(root, "int_top");
            bool     ext1 = cjson_get_bool(root, "ext1");
            bool     ext2 = cjson_get_bool(root, "ext2");
            int      feature = cjson_get_int(root, "f");

            int reqId = cjson_get_int(root, msgId);
            response = InfraredService::getInstance().send(sockfd, reqId, ir_code, format, repeat, hold, intSide,
                                                           intTop, ext1, ext2);
            if (response == 0 || (response == 202 && (feature & API_FEATURE_FLAG_IR_REPEAT_NO_RESPONSE))) {
                // asynchronous reply
                if (repeat > 1) {
                    // save client socket to stop IR repeat on WebSocket disconnect
                    sockfdSendIR_ = sockfd;
                }
                cJSON_Delete(responseDoc);
                cJSON_Delete(root);
                return ESP_OK;
            }
        }
        code = response;
    } else if (command == "ir_stop") {
        InfraredService::getInstance().stopSend();
        sockfdSendIR_ = -1;
        code = 200;
    } else if (command == "ir_receive_on") {
        IRFormat irFormat = cjson_get_bool(root, "raw") ? IRFormat::RAW : IRFormat::UNFOLDED_CIRCLE;

        InfraredService::getInstance().startIrLearn(irFormat);
        ESP_LOGD(TAG, "IR Receive on");
    } else if (command == "ir_receive_off") {
        InfraredService::getInstance().stopIrLearn();
        ESP_LOGD(TAG, "IR Receive off");
    } else if (command == "remote_charged") {
        // TODO m_state->setState(States::NORMAL_FULLYCHARGED);
    } else if (command == "remote_lowbattery") {
        // TODO  m_state->setState(States::NORMAL_LOWBATTERY);
    } else if (command == "remote_normal") {
        // TODO m_state->setState(States::NORMAL);
    } else if (command == "identify") {
        led_pattern(LED_IMPROV_IDENTIFY);
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, UC_ACTION_IDENTIFY, NULL, 0, pdMS_TO_TICKS(200)));
    } else if (command == "set_logging") {
        code = 501;  // not yet implemented
    } else if (command == "set_ntp") {
        bool changed = false;
        if (cJSON_HasObjectItem(root, "ntp1") || cJSON_HasObjectItem(root, "ntp2")) {
            std::string old_server1 = config_->getNtpServer1();
            std::string old_server2 = config_->getNtpServer2();
            std::string server1 = cjson_get_string(root, "ntp1", "");
            std::string server2 = cjson_get_string(root, "ntp2", "");
            if (server1.empty() && !server2.empty()) {
                server1 = server2;
                server2 = "";
            }
            changed = (server1 != old_server1) || (server2 != old_server2);
            if (!config_->setNtpServer(server1, server2)) {
                code = 400;
                goto send_response;
            }
        }
        item = cJSON_GetObjectItem(root, "ntp_enabled");
        if (item) {
            bool old_enabled = config_->isNtpEnabled();
            bool enabled = cJSON_IsTrue(item);
            changed = changed || (enabled != old_enabled);
            if (!config_->enableNtp(enabled)) {
                code = 400;
                goto send_response;
            }
        }

        if (changed) {
            cJSON_AddBoolToObject(responseDoc, "reboot", true);
            schedule_restart(web, 2000);
        }
    } else if (command == "set_network") {
        // interface: "eth" or "wifi"
        const char *iface_str = cjson_get_string(root, "interface", nullptr);
        if (!iface_str) {
            code = 400;
            cJSON_AddStringToObject(responseDoc, msgError, "Missing interface");
            goto send_response;
        }

        enum { IFACE_ETH, IFACE_WIFI } iface;
        if (strcmp(iface_str, "eth") == 0) {
            iface = IFACE_ETH;
        } else if (strcmp(iface_str, "wifi") == 0) {
            iface = IFACE_WIFI;
        } else {
            code = 400;
            cJSON_AddStringToObject(responseDoc, msgError, "Invalid interface");
            goto send_response;
        }

        const char *mode_str = cjson_get_string(root, "mode", nullptr);
        if (!mode_str) {
            code = 400;
            cJSON_AddStringToObject(responseDoc, msgError, "Missing mode");
            goto send_response;
        }

        bool dhcp;
        if (strcmp(mode_str, "dhcp") == 0) {
            dhcp = true;
        } else if (strcmp(mode_str, "static") == 0) {
            dhcp = false;
        } else {
            code = 400;
            cJSON_AddStringToObject(responseDoc, msgError, "Invalid mode");
            goto send_response;
        }

        network_cfg_t    netcfg = config_->getNetwork();
        iface_net_cfg_t *cfg = (iface == IFACE_ETH) ? &netcfg.eth : &netcfg.wifi;
        bool             changed = (cfg->dhcp != dhcp);

        cfg->dhcp = dhcp;

        if (!dhcp) {
            // Static mode: ip/mask required, gw optional
            ip4_addr_t ip = {};
            ip4_addr_t mask = {};
            ip4_addr_t gw = {};

            if (!parse_required_ipv4_addr(root, "ip", &ip, responseDoc) ||
                !parse_required_ipv4_addr(root, "mask", &mask, responseDoc) ||
                !parse_optional_ipv4_addr(root, "gw", &gw, responseDoc)) {
                code = 400;
                goto send_response;
            }

            changed = changed || (cfg->ip.ip.addr != ip.addr) || (cfg->ip.netmask.addr != mask.addr) ||
                      (cfg->ip.gw.addr != gw.addr);
            cfg->ip.ip.addr = ip.addr;
            cfg->ip.netmask.addr = mask.addr;
            cfg->ip.gw.addr = gw.addr;
        }

        if (!config_->setNetwork(netcfg)) {
            code = 500;
            cJSON_AddStringToObject(responseDoc, msgError, "Failed to save network config");
            goto send_response;
        }

        if (changed) {
            cJSON_AddBoolToObject(responseDoc, "reboot", true);
            schedule_restart(web, 2000);
        }
    } else if (command == "get_network") {
        char          ip_str[48];
        network_cfg_t netcfg = config_->getNetwork();

        // Helper lambda to serialize one interface
        auto add_iface = [](cJSON *root, const char *name, const iface_net_cfg_t &cfg) {
            char   ip_str[16];
            cJSON *obj = cJSON_CreateObject();
            cJSON_AddItemToObject(root, name, obj);

            cJSON_AddStringToObject(obj, "mode", cfg.dhcp ? "dhcp" : "static");

            if (!cfg.dhcp) {
                const char *address = ipv4_addr_to_string((const ip4_addr_t *)&cfg.ip.ip, ip_str, sizeof(ip_str));
                if (address) {
                    cJSON_AddStringToObject(obj, "ip", address);
                }

                address = ipv4_addr_to_string((const ip4_addr_t *)&cfg.ip.netmask, ip_str, sizeof(ip_str));
                if (address) {
                    cJSON_AddStringToObject(obj, "mask", address);
                }

                address = ipv4_addr_to_string((const ip4_addr_t *)&cfg.ip.gw, ip_str, sizeof(ip_str));
                if (address) {
                    cJSON_AddStringToObject(obj, "gw", address);
                }
            }
        };

        add_iface(responseDoc, "eth", netcfg.eth);
        add_iface(responseDoc, "wifi", netcfg.wifi);

        // DNS settings are global and not per-interface
        std::string dns_server = config_->getDnsServer1();
        if (!dns_server.empty()) {
            cJSON_AddStringToObject(responseDoc, "dns1", dns_server.c_str());
        }

        dns_server = config_->getDnsServer2();
        if (!dns_server.empty()) {
            cJSON_AddStringToObject(responseDoc, "dns2", dns_server.c_str());
        }

        cJSON_AddBoolToObject(responseDoc, "ntp_enabled", config_->isNtpEnabled());
        std::string server = config_->getNtpServer1();
        if (!server.empty()) {
            cJSON_AddStringToObject(responseDoc, "ntp1", server.c_str());
        }
        server = config_->getNtpServer2();
        if (!server.empty()) {
            cJSON_AddStringToObject(responseDoc, "ntp2", server.c_str());
        }

        esp_netif_t *active_netif = nullptr;
        const char  *active_if = "none";

        if (is_eth_connected()) {
            active_netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
            active_if = "eth";
        } else if (is_wifi_up()) {
            active_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            active_if = "wifi";
        }

        cJSON *active_net = cJSON_CreateObject();
        cJSON_AddItemToObject(responseDoc, "active", active_net);
        cJSON_AddStringToObject(active_net, "interface", active_if);

        if (active_netif) {
            esp_netif_ip_info_t ip4;
            if (esp_netif_get_ip_info(active_netif, &ip4) == ESP_OK && ip4.ip.addr != 0 && ip4.ip.addr != IPADDR_NONE) {
                cJSON_AddStringToObject(active_net, "ip",
                                        ip4addr_ntoa_r((ip4_addr_t *)&ip4.ip, ip_str, sizeof(ip_str)));
                cJSON_AddStringToObject(active_net, "mask",
                                        ip4addr_ntoa_r((ip4_addr_t *)&ip4.netmask, ip_str, sizeof(ip_str)));
                cJSON_AddStringToObject(active_net, "gw",
                                        ip4addr_ntoa_r((ip4_addr_t *)&ip4.gw, ip_str, sizeof(ip_str)));

                esp_netif_dns_info_t dns;
                if (esp_netif_get_dns_info(active_netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK) {
                    const char *dns_str = ip_addr_to_string(&dns.ip, ip_str, sizeof(ip_str));
                    if (dns_str) {
                        cJSON_AddStringToObject(active_net, "dns1", dns_str);
                    }
                }

                if (esp_netif_get_dns_info(active_netif, ESP_NETIF_DNS_BACKUP, &dns) == ESP_OK) {
                    const char *dns_str = ip_addr_to_string(&dns.ip, ip_str, sizeof(ip_str));
                    if (dns_str) {
                        cJSON_AddStringToObject(active_net, "dns2", dns_str);
                    }
                }

                if (esp_netif_get_dns_info(active_netif, ESP_NETIF_DNS_FALLBACK, &dns) == ESP_OK) {
                    const char *dns_str = ip_addr_to_string(&dns.ip, ip_str, sizeof(ip_str));
                    if (dns_str) {
                        cJSON_AddStringToObject(active_net, "dns3", dns_str);
                    }
                }
            }

            if (config_->isNtpEnabled()) {
                for (uint8_t i = 0; i < CONFIG_LWIP_SNTP_MAX_SERVERS; i++) {
                    char key[8];
                    snprintf(key, sizeof(key), "ntp%u", i + 1);
                    const char *server_name = esp_sntp_getservername(i);
                    if (server_name) {
                        cJSON_AddStringToObject(active_net, key, server_name);
                    } else {
                        // IPv4 or IPv6 address
                        char             buf[48];
                        ip_addr_t const *ip = esp_sntp_getserver(i);
                        if (ip) {
                            server_name = ipaddr_ntoa_r(ip, buf, sizeof(buf));
                            if (server_name != NULL) {
                                cJSON_AddStringToObject(active_net, key, server_name);
                            }
                        }
                    }
                }
            }

#if CONFIG_LWIP_IPV6
            add_ipv6_addresses_to_json(active_net, active_netif);
#endif
        }

        code = 200;
    } else if (command == "set_dns") {
        bool        ok = true;
        bool        removed_dns = false;
        std::string dns1 = config_->getDnsServer1();
        std::string dns2 = config_->getDnsServer2();

        if (cJSON_HasObjectItem(root, "dns1")) {
            const char *server = cjson_get_string(root, "dns1", "");
            if (strlen(server) == 0) {
                removed_dns = !dns1.empty();
                dns1.clear();
            } else if (!normalize_dns_addr_string(server, &dns1)) {
                code = 400;
#if CONFIG_LWIP_IPV6
                cJSON_AddStringToObject(responseDoc, msgError, "Invalid dns1 address");
#else
                cJSON_AddStringToObject(responseDoc, msgError, "Invalid dns1 address or IPv6 DNS not supported");
#endif
                goto send_response;
            }
        }

        if (cJSON_HasObjectItem(root, "dns2")) {
            const char *server = cjson_get_string(root, "dns2", "");
            if (strlen(server) == 0) {
                removed_dns = removed_dns || !dns2.empty();
                dns2.clear();
            } else if (!normalize_dns_addr_string(server, &dns2)) {
                code = 400;
#if CONFIG_LWIP_IPV6
                cJSON_AddStringToObject(responseDoc, msgError, "Invalid dns2 address");
#else
                cJSON_AddStringToObject(responseDoc, msgError, "Invalid dns2 address or IPv6 DNS not supported");
#endif
                goto send_response;
            }
        }

        ok = config_->setDnsServer(dns1, dns2);
        if (ok) {
            apply_custom_dns();
            if (removed_dns) {
                cJSON_AddBoolToObject(responseDoc, "reboot", true);
                schedule_restart(web, 2000);
            }
        }
        code = ok ? 200 : 400;
    } else if (command == "get_port_modes") {
        code = processGetPortModes(responseDoc);
    } else if (command == "get_port_mode") {
        code = processGetPortMode(root, responseDoc);
    } else if (command == "set_port_mode") {
        code = processSetPortMode(root, responseDoc);
    } else if (command == "get_port_trigger") {
        code = processGetPortTrigger(root, responseDoc);
    } else if (command == "set_port_trigger") {
        code = processSetPortTrigger(root);
    } else if (command == "enable_serial_events") {
        code = processEnableSerialEvents(sockfd, root);
    } else if (command == "send_serial") {
        code = processSendSerial(root);
    } else if (command == "set_serial_config") {
        code = processSetSerialConfig(root);
    } else if (command == "get_serial_config") {
        code = processGetSerialConfig(root, responseDoc);
    } else if (command == "enable_log_events") {
        code = handleEnableLogEvents(sockfd, root, responseDoc);
    } else if (command == "reboot") {
        ESP_LOGW(TAG, "Rebooting");
        cJSON_AddBoolToObject(responseDoc, "reboot", true);
        schedule_restart(web, 2000);
    } else if (command == "reset") {
        ESP_LOGW(TAG, "Reset");
        cJSON_AddBoolToObject(responseDoc, "reboot", true);
        schedule_restart(web, 2000, true);
    } else if (command == "set_ir_config") {
        bool ok = true;

        if (cJSON_HasObjectItem(root, "irlearn_core")) {
            uint16_t value = static_cast<uint16_t>(cjson_get_int(root, "irlearn_core", &ok));
            if (ok && !config_->setIrLearnCore(value)) {
                ok = false;
            }
        }
        if (cJSON_HasObjectItem(root, "irlearn_prio")) {
            uint16_t value = static_cast<uint16_t>(cjson_get_int(root, "irlearn_prio", &ok));
            if (!config_->setIrLearnPriority(value)) {
                ok = false;
            }
            InfraredService::getInstance().setIrLearnPriority(value);
        }
        if (cJSON_HasObjectItem(root, "irsend_core")) {
            uint16_t value = static_cast<uint16_t>(cjson_get_int(root, "irsend_core", &ok));
            if (!config_->setIrSendCore(value)) {
                ok = false;
            }
        }
        if (cJSON_HasObjectItem(root, "irsend_prio")) {
            uint16_t value = static_cast<uint16_t>(cjson_get_int(root, "irsend_prio", &ok));
            if (!config_->setIrSendPriority(value)) {
                ok = false;
            }
            InfraredService::getInstance().setIrSendPriority(value);
        }
        if (cJSON_HasObjectItem(root, "itach_emulation")) {
            bool old = config_->isGcServerEnabled();
            bool enabled = cjson_get_bool(root, "itach_emulation");
            if (!config_->enableGcServer(enabled)) {
                ok = false;
            } else if (old != enabled) {
                cJSON_AddBoolToObject(responseDoc, "reboot", true);
                schedule_restart(web, 2000);
            }
        }
        if (cJSON_HasObjectItem(root, "itach_beacon")) {
            bool old = config_->isGcServerBeaconEnabled();
            bool enabled = cjson_get_bool(root, "itach_beacon");
            if (!config_->enableGcServerBeacon(enabled)) {
                ok = false;
            } else if (old != enabled && !cJSON_HasObjectItem(responseDoc, "reboot")) {
                cJSON_AddBoolToObject(responseDoc, "reboot", true);
                schedule_restart(web, 2000);
            }
        }
        code = ok ? 200 : 500;
    } else if (command == "get_ir_config") {
        cJSON_AddNumberToObject(responseDoc, "irlearn_core", config_->getIrLearnCore());
        cJSON_AddNumberToObject(responseDoc, "irlearn_prio", config_->getIrLearnPriority());
        cJSON_AddNumberToObject(responseDoc, "irsend_core", config_->getIrSendCore());
        cJSON_AddNumberToObject(responseDoc, "irsend_prio", config_->getIrSendPriority());
        cJSON_AddBoolToObject(responseDoc, "itach_emulation", config_->isGcServerEnabled());
        cJSON_AddBoolToObject(responseDoc, "itach_beacon", config_->isGcServerBeaconEnabled());
    } else if (command == "get_serial_tcp") {
        cJSON_AddBoolToObject(responseDoc, "serial_tcp", config_->isSerialTcpEnabled());
    } else if (command == "set_serial_tcp") {
        bool ok = false;
        bool enable = cjson_get_bool(root, "enable", &ok);
        if (!ok) {
            code = 400;
        } else if (config_->enableSerialTcp(enable)) {
            // Restart all active bridges with new TCP setting
            for (uint8_t i = 0; i < EXTERNAL_PORT_COUNT; i++) {
                if (bridges_[i]) {
                    bool was_running = serial_bridge_is_running(bridges_[i]);
                    if (was_running) {
                        serial_bridge_stop(bridges_[i]);
                    }
                    serial_bridge_set_tcp_enabled(bridges_[i], enable);
                    if (was_running) {
                        serial_bridge_start(bridges_[i]);
                    }
                }
            }
            code = 200;
        } else {
            code = 500;
        }
    } else if (command == "set_poe") {
        if (!config_->hasPoeFeature() || board_get_poe_switch_pin() == GPIO_NUM_NC) {
            code = 400;
        } else {
            bool ok = false;
            int  mode = cjson_get_int(root, "mode", &ok);
            if (ok && mode >= 0 && mode <= 1) {
                int old_mode = config_->getPoeVoltageMode();
                config_->setPoeVoltageMode(mode);
                if (old_mode != mode) {
                    ESP_LOGW(TAG, "Rebooting");
                    cJSON_AddBoolToObject(responseDoc, "reboot", true);
                    schedule_restart(web, 2000);
                }
            } else {
                code = 400;
            }
        }
    } else {
        code = 400;
        cJSON_AddStringToObject(responseDoc, msgError,
                                command.empty() ? "Missing command field" : "Unsupported command");
    }

send_response:
    // default response code
    cJSON_AddNumberToObject(responseDoc, msgCode, code);
    // Attention: resp gets freed by WebServer!
    char *resp = cJSON_PrintUnformatted(responseDoc);
    web->sendWsTxt(sockfd, resp);
    cJSON_Delete(responseDoc);
    cJSON_Delete(root);
    return ret;
}

uint16_t DockApi::processEnableSerialEvents(int sockfd, const cJSON *root) {
    bool    ok = false;
    uint8_t port = static_cast<uint8_t>(cjson_get_int(root, "port", &ok));
    if (!ok || port == 0 || port > EXTERNAL_PORT_COUNT) {
        return 400;
    }

    bool enable = cjson_get_bool(root, "enable", &ok);
    if (!ok) {
        return 400;
    }

    if (xSemaphoreTake(serial_event_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to lock serial event mutex");
        return 500;
    }

    uint8_t port_mask = 1 << (port - 1);

    if (enable) {
        serial_event_fds_[sockfd] |= port_mask;
        ESP_LOGD(TAG, "WS client %d: serial events enabled for port %d", sockfd, port);
    } else {
        if (serial_event_fds_.contains(sockfd)) {
            serial_event_fds_[sockfd] &= ~port_mask;
            if (serial_event_fds_[sockfd] == 0) {
                serial_event_fds_.erase(sockfd);
            }
        }
        ESP_LOGD(TAG, "WS client %d: serial events disabled for port %d", sockfd, port);
    }

    xSemaphoreGive(serial_event_mutex_);
    return 200;
}

uint16_t DockApi::processSendSerial(const cJSON *root) {
    bool    ok = false;
    uint8_t port = static_cast<uint8_t>(cjson_get_int(root, "port", &ok));
    if (!ok || port == 0 || port > EXTERNAL_PORT_COUNT) {
        return 400;
    }

    if (!ports_.contains(port)) {
        return 400;
    }

    if (ports_[port]->getMode() != RS232) {
        return 409;
    }

    const char *data = cjson_get_string(root, "data");
    if (!data || strlen(data) == 0) {
        return 400;
    }

    serial_bridge_t *br = bridges_[port - 1];
    if (!br) {
        return 409;
    }

    esp_err_t ret = serial_bridge_send_to_uart(br, reinterpret_cast<const uint8_t *>(data), strlen(data));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "send_serial port %d failed: %s", port, esp_err_to_name(ret));
        return 500;
    }

    return 200;
}

uint16_t DockApi::processSetSerialConfig(const cJSON *root) {
    bool    ok = false;
    uint8_t port = static_cast<uint8_t>(cjson_get_int(root, "port", &ok));
    if (!ok || port == 0 || port > EXTERNAL_PORT_COUNT) {
        return 400;
    }

    SerialPortBuffer &pbuf = serial_buffers_[port - 1];
    bool              needs_realloc = false;

    // buffering mode (optional)
    const char *mode_str = cjson_get_string(root, "buffering");
    if (mode_str) {
        if (strcmp(mode_str, "chunk") == 0) {
            pbuf.mode = SerialPortBuffer::CHUNK;
        } else if (strcmp(mode_str, "line") == 0) {
            pbuf.mode = SerialPortBuffer::LINE;
        } else {
            return 400;
        }
        config_->setSerialBuffering(port, pbuf.mode);
    }

    // terminator (optional)
    const char *term_str = cjson_get_string(root, "terminator");
    if (term_str && strlen(term_str) > 0) {
        pbuf.terminator = static_cast<uint8_t>(term_str[0]);
        config_->setSerialTerminatorChar(port, pbuf.terminator);
    }

    // buffer_size (optional)
    if (cJSON_HasObjectItem(root, "buffer_size")) {
        int size = cjson_get_int(root, "buffer_size", &ok);
        if (!ok || size < 1) {
            return 400;
        }
        if (static_cast<size_t>(size) > SERIAL_BUFFER_SIZE_MAX) {
            return 400;
        }
        if (static_cast<size_t>(size) != pbuf.buffer_size) {
            pbuf.buffer_size = static_cast<size_t>(size);
            needs_realloc = true;
        }
        config_->setSerialBufferSize(port, static_cast<uint16_t>(size));
    }

    // timeout_ms (optional)
    if (cJSON_HasObjectItem(root, "timeout_ms")) {
        int timeout = cjson_get_int(root, "timeout_ms", &ok);
        if (!ok || timeout < 0) {
            return 400;
        }
        pbuf.timeout_ms = static_cast<uint32_t>(timeout);
        config_->setSerialTimeout(port, static_cast<uint16_t>(timeout));
    }

    // Reallocate buffer if size changed
    if (needs_realloc) {
        if (xSemaphoreTake(pbuf.mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            // Flush any pending data before realloc
            if (pbuf.len > 0) {
                flushSerialBuffer(port, pbuf);
            }
            pbuf.deallocate();
            if (!pbuf.allocate()) {
                xSemaphoreGive(pbuf.mutex);
                return 500;
            }
            xSemaphoreGive(pbuf.mutex);
        } else {
            return 500;
        }
    }

    return 200;
}

uint16_t DockApi::processGetSerialConfig(const cJSON *root, cJSON *responseDoc) {
    bool    ok = false;
    uint8_t port = static_cast<uint8_t>(cjson_get_int(root, "port", &ok));
    if (!ok || port == 0 || port > EXTERNAL_PORT_COUNT) {
        return 400;
    }

    SerialPortBuffer &pbuf = serial_buffers_[port - 1];

    cJSON_AddNumberToObject(responseDoc, "port", port);
    cJSON_AddStringToObject(responseDoc, "buffering", pbuf.mode == SerialPortBuffer::LINE ? "line" : "chunk");

    // Represent terminator as the escaped character
    char term_buf[2] = {static_cast<char>(pbuf.terminator), '\0'};
    cJSON_AddStringToObject(responseDoc, "terminator", term_buf);

    cJSON_AddNumberToObject(responseDoc, "buffer_size", pbuf.buffer_size);
    cJSON_AddNumberToObject(responseDoc, "timeout_ms", pbuf.timeout_ms);

    addUartNode(port, responseDoc);

    return 200;
}

void DockApi::serialRxCallback(uint8_t port_index, const uint8_t *data, size_t len, void *user_ctx) {
    auto *that = static_cast<DockApi *>(user_ctx);
    that->handleSerialRx(port_index, data, len);
}

void DockApi::handleSerialRx(uint8_t port_index, const uint8_t *data, size_t len) {
    if (port_index < 1 || port_index > EXTERNAL_PORT_COUNT) {
        return;
    }

    SerialPortBuffer &pbuf = serial_buffers_[port_index - 1];

    // Fast path: check if anyone is subscribed
    // Mutex discipline:
    // - The pbuf.mutex is held in line mode while data is being written to the buffer.
    // - flushSerialBuffer is called within the mutex and then acquires the serial_event_mutex_ again.
    // - The lock order is always: pbuf.mutex → serial_event_mutex_.
    // As long as this remains consistent (never the other way around), there is no deadlock.
    if (xSemaphoreTake(serial_event_mutex_, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }
    bool    has_subscribers = false;
    uint8_t port_mask = 1 << (port_index - 1);
    for (auto const &[fd, mask] : serial_event_fds_) {
        if (mask & port_mask) {
            has_subscribers = true;
            break;
        }
    }
    xSemaphoreGive(serial_event_mutex_);

    if (!has_subscribers) {
        // No subscribers: discard any buffered data and skip processing
        if (pbuf.len > 0) {
            if (xSemaphoreTake(pbuf.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                pbuf.reset();
                xSemaphoreGive(pbuf.mutex);
            }
        }
        return;
    }

    // Idle tick (data == NULL): check timeout
    if (data == NULL || len == 0) {
        if (pbuf.mode == SerialPortBuffer::LINE && pbuf.len > 0 && pbuf.timeout_ms > 0) {
            if (xSemaphoreTake(pbuf.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                if (pbuf.len > 0 && pbuf.last_rx_us > 0) {
                    int64_t now_us = esp_timer_get_time();
                    int64_t elapsed_ms = (now_us - pbuf.last_rx_us) / 1000;
                    if (elapsed_ms >= pbuf.timeout_ms) {
                        flushSerialBuffer(port_index, pbuf);
                    }
                }
                xSemaphoreGive(pbuf.mutex);
            }
        }
        return;
    }

    // Data received
    if (pbuf.mode == SerialPortBuffer::CHUNK) {
        // Chunk mode: send immediately, no buffering
        sendSerialEvent(port_index, data, len);
        return;
    }

    // Line mode: buffer until terminator or buffer full
    if (xSemaphoreTake(pbuf.mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        if (pbuf.buf && pbuf.len < pbuf.buffer_size) {
            pbuf.buf[pbuf.len++] = data[i];
            pbuf.last_rx_us = esp_timer_get_time();
        }

        // Check for terminator
        if (data[i] == pbuf.terminator) {
            flushSerialBuffer(port_index, pbuf);
        }
        // Check for buffer full
        else if (pbuf.len >= pbuf.buffer_size) {
            flushSerialBuffer(port_index, pbuf);
        }
    }

    xSemaphoreGive(pbuf.mutex);
}

void DockApi::flushSerialBuffer(uint8_t port_index, SerialPortBuffer &pbuf) {
    if (pbuf.len == 0) {
        return;
    }

    sendSerialEvent(port_index, pbuf.buf, pbuf.len);
    pbuf.len = 0;
    pbuf.last_rx_us = 0;
}

void DockApi::sendSerialEvent(uint8_t port_index, const uint8_t *data, size_t len) {
    // Collect subscribed fds for this port
    // Note: using std::vector for simplicity, assuming there won't be many subscribers.
    // With multiple subscribers and high UART datarates, a static array of fixed size should be used.
    if (xSemaphoreTake(serial_event_mutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    uint8_t          port_mask = 1 << (port_index - 1);
    std::vector<int> subscribed_fds;
    subscribed_fds.reserve(serial_event_fds_.size());

    for (auto const &[fd, mask] : serial_event_fds_) {
        if (mask & port_mask) {
            subscribed_fds.push_back(fd);
        }
    }

    xSemaphoreGive(serial_event_mutex_);

    if (subscribed_fds.empty()) {
        return;
    }

    // Convert Latin-1 to UTF-8
    // Worst case: every byte becomes 2-byte UTF-8
    size_t utf8_buf_size = len * 2 + 1;
    char  *utf8_buf = static_cast<char *>(malloc(utf8_buf_size));
    if (!utf8_buf) {
        return;
    }
    latin1_to_utf8(data, len, utf8_buf, utf8_buf_size);

    // Build JSON event
    cJSON *event = cJSON_CreateObject();
    if (!event) {
        free(utf8_buf);
        return;
    }

    cJSON_AddStringToObject(event, "type", "event");
    cJSON_AddStringToObject(event, "msg", "serial_data");
    cJSON_AddNumberToObject(event, "port", port_index);
    cJSON_AddStringToObject(event, "data", utf8_buf);

    free(utf8_buf);

    char *msg = cJSON_PrintUnformatted(event);
    cJSON_Delete(event);

    if (!msg) {
        return;
    }

    for (int fd : subscribed_fds) {
        // Important: needs to be const char* to avoid freeing the buffer in sendWsTxt!
        web_->sendWsTxt(fd, (const char *)msg);
    }

    cJSON_free(msg);
}

void DockApi::initSerialBuffers() {
    for (uint8_t i = 0; i < EXTERNAL_PORT_COUNT; i++) {
        loadSerialBufferConfig(i + 1);
        serial_buffers_[i].allocate();
    }
}

void DockApi::deinitSerialBuffers() {
    for (uint8_t i = 0; i < EXTERNAL_PORT_COUNT; i++) {
        if (serial_buffers_[i].mutex) {
            vSemaphoreDelete(serial_buffers_[i].mutex);
            serial_buffers_[i].mutex = nullptr;
        }
        serial_buffers_[i].deallocate();
    }
}

void DockApi::loadSerialBufferConfig(uint8_t port) {
    if (port < 1 || port > EXTERNAL_PORT_COUNT) return;

    SerialPortBuffer &pbuf = serial_buffers_[port - 1];

    // Load from persistent config
    uint8_t mode = config_->getSerialBuffering(port);
    if (mode == 1) {
        pbuf.mode = SerialPortBuffer::CHUNK;
    } else {
        pbuf.mode = SerialPortBuffer::LINE;
    }

    uint8_t term = config_->getSerialTerminatorChar(port);
    if (term != 0) {
        pbuf.terminator = term;
    } else {
        pbuf.terminator = '\n';
    }

    uint16_t buf_size = config_->getSerialBufferSize(port);
    if (buf_size == 0) {
        pbuf.buffer_size = SERIAL_BUFFER_SIZE_DEFAULT;
    } else if (buf_size > SERIAL_BUFFER_SIZE_MAX) {
        pbuf.buffer_size = SERIAL_BUFFER_SIZE_MAX;
    } else {
        pbuf.buffer_size = buf_size;
    }

    uint16_t timeout = config_->getSerialTimeout(port);
    pbuf.timeout_ms = (timeout == 0) ? 100 : timeout;
}

uint16_t DockApi::handleEnableLogEvents(int sockfd, const cJSON *root, cJSON *responseDoc) {
    bool ok = false;
    bool enable = cjson_get_bool(root, "enable", &ok);

    if (!ok) {
        cJSON_AddStringToObject(responseDoc, msgError, "Invalid or missing 'enable' field");
        return 400;
    }

    if (xSemaphoreTake(log_subscribers_mutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire log_subscribers_mutex");
        cJSON_AddStringToObject(responseDoc, msgError, "Internal error");
        return 500;
    }

    if (enable) {
        if (log_subscribers_.size() >= MAX_WS_LOG_SUBS) {
            xSemaphoreGive(log_subscribers_mutex_);
            cJSON_AddStringToObject(responseDoc, msgError, "Max subscriptions reached");
            return 503;
        }
        log_subscribers_.insert(sockfd);
        ESP_LOGI(TAG, "Client %d subscribed to log streaming (%zu/%d)", sockfd, log_subscribers_.size(),
                 MAX_WS_LOG_SUBS);
    } else {
        log_subscribers_.erase(sockfd);
        ESP_LOGD(TAG, "Client %d unsubscribed from log streaming (%zu/%d)", sockfd, log_subscribers_.size(),
                 MAX_WS_LOG_SUBS);
    }

    size_t subscriber_count = log_subscribers_.size();
    xSemaphoreGive(log_subscribers_mutex_);

    // Start or stop the router based on whether any subscriber remains.
    if (subscriber_count > 0 && log_queue_ == nullptr) {
        // First subscriber: start router and create the sender task.
        // log_router_start is idempotent; calling it again just returns the existing queue.
        log_router_start(&log_queue_);
        xTaskCreate(logSenderTask, "log_sender", 3072, this, tskIDLE_PRIORITY + 1, &log_sender_task_handle_);
        assert(log_sender_task_handle_);
    } else if (subscriber_count == 0 && log_queue_ != nullptr) {
        log_router_stop();
        log_queue_ = nullptr;
    }

    return 200;
}

void DockApi::sendLogToSubscribers(const char *tag, esp_log_level_t level, const char *message, size_t len) {
    // Snapshot subscribers under lock — fast, no I/O
    int    fds[MAX_WS_LOG_SUBS];
    size_t count = 0;
    if (xSemaphoreTake(log_subscribers_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
        for (int fd : log_subscribers_) {
            if (count < MAX_WS_LOG_SUBS) {
                fds[count++] = fd;
            }
        }
        xSemaphoreGive(log_subscribers_mutex_);  // release BEFORE building JSON
    } else {
        return;
    }
    if (count == 0) {
        return;
    }

    // Build JSON log message using cJSON
    cJSON *event = cJSON_CreateObject();
    if (!event) {
        return;
    }

    cJSON_AddStringToObject(event, "type", "event");
    cJSON_AddStringToObject(event, "msg", "log");

    const char *level_str = "I";
    switch (level) {
        case ESP_LOG_ERROR:
            level_str = "E";
            break;
        case ESP_LOG_WARN:
            level_str = "W";
            break;
        case ESP_LOG_INFO:
            level_str = "I";
            break;
        case ESP_LOG_DEBUG:
            level_str = "D";
            break;
        case ESP_LOG_VERBOSE:
            level_str = "V";
            break;
        default:
            break;
    }
    cJSON_AddStringToObject(event, "level", level_str);
    cJSON_AddStringToObject(event, "tag", tag);

    // Parse message to extract timestamp and clean text
    // Format: "<LEVEL> (<TS>) <TAG>: <TEXT>"
    uint32_t    timestamp = 0;
    const char *text_start = nullptr;

    // Find timestamp in brackets
    const char *ts_start = strchr(message, '(');
    const char *ts_end = strchr(message, ')');
    if (ts_start && ts_end && ts_end > ts_start) {
        timestamp = strtoul(ts_start + 1, nullptr, 10);
        cJSON_AddNumberToObject(event, "ts", timestamp);

        // Find text after "tag: "
        text_start = strchr(ts_end, ':');
        if (text_start) {
            text_start++;                             // skip ':'
            while (*text_start == ' ') text_start++;  // skip leading spaces
        }
    }

    // If we couldn't parse the text, use the original message
    if (!text_start) {
        text_start = message;
    }

    // Trim leading whitespace
    while (*text_start == ' ' || *text_start == '\t' || *text_start == '\r' || *text_start == '\n') {
        text_start++;
    }

    // Calculate length and trim trailing whitespace
    size_t msg_len = strlen(text_start);
    while (msg_len > 0) {
        char c = text_start[msg_len - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            msg_len--;
        } else {
            break;
        }
    }

    // Truncate message if too long (leave room for JSON overhead)
    if (msg_len > 500) {
        msg_len = 500;
    }

    // Avoid using malloc for another copy
    char saved = text_start[msg_len];
    ((char *)text_start)[msg_len] = '\0';  // safe: text_start points into message
    cJSON_AddStringToObject(event, "log", text_start);
    ((char *)text_start)[msg_len] = saved;

    char *json_str = cJSON_PrintUnformatted(event);
    cJSON_Delete(event);

    if (!json_str) {
        return;
    }

    // Send to all subscribers
    for (size_t i = 0; i < count; i++) {
        // Important: needs to be const char* to avoid freeing the buffer in sendWsTxt!
        web_->sendWsTxt(fds[i], (const char *)json_str);
        // Quick and dirty message throttling: otherwise the internal httpd work queue starts dropping messages!
        // Getting a "queue full" response from httpd_queue_work without blocking was only recently fixed in IDF:
        // https://github.com/espressif/esp-idf/commit/c911c781ae94e40d0df6596a25c53025c5f98fb5
        vTaskDelay(pdMS_TO_TICKS(WS_LOG_DELAY_MS));
    }

    cJSON_free(json_str);
}

void DockApi::logSenderTask(void *arg) {
    DockApi      *self = static_cast<DockApi *>(arg);
    QueueHandle_t q = self->log_queue_;
    log_entry_t  *entry = nullptr;

    while (xQueueReceive(q, &entry, portMAX_DELAY) == pdTRUE && entry) {
        if (entry->level == LOG_ENTRY_SENTINEL) {
            free(entry);
            break;  // clean exit
        }
        self->sendLogToSubscribers(entry->tag, static_cast<esp_log_level_t>(entry->level), entry->message, entry->len);
        free(entry);
        entry = nullptr;
    }

    // Drain any real entries that arrived between the sentinel and here.
    // This is a best-effort flush before shutdown — ensures no entry leaks.
    while (xQueueReceive(q, &entry, 0) == pdTRUE && entry) {
        free(entry);
    }

    self->log_sender_task_handle_ = nullptr;
    vTaskDelete(nullptr);
}

uint16_t DockApi::processGetPortModes(cJSON *responseDoc) {
    cJSON *ports = cJSON_AddArrayToObject(responseDoc, "ports");

    for (auto const &[port, val] : ports_) {
        cJSON *item = cJSON_CreateObject();

        fillPortMode(val, item);
        cJSON_AddItemToArray(ports, item);
    }

    return 200;
}

uint16_t DockApi::processGetPortMode(const cJSON *root, cJSON *responseDoc) {
    uint8_t port = cjson_get_int(root, "port");
    if (port == 0 || port > EXTERNAL_PORT_COUNT) {
        return 400;
    }
    if (!ports_.contains(port)) {
        return 503;
    }

    fillPortMode(ports_[port], responseDoc);
    return 200;
}

void DockApi::fillPortMode(const std::shared_ptr<ExternalPort> &extPort, cJSON *responseDoc) {
    uint8_t     port = extPort->getPortNumber();
    ExtPortMode mode = config_->getExternalPortMode(port);
    ExtPortMode active_mode = extPort->getMode();

    cJSON_AddNumberToObject(responseDoc, "port", port);
    cJSON_AddStringToObject(responseDoc, "mode", ExtPortMode_to_str(mode));
    if (mode != active_mode) {
        cJSON_AddStringToObject(responseDoc, "active_mode", ExtPortMode_to_str(active_mode));
    }

    cJSON *supported = cJSON_AddArrayToObject(responseDoc, "supported_modes");
    if (supported) {
        for (uint8_t mode = 0; mode < ExtPortMode::PORT_MODE_MAX; mode++) {
            if (extPort->isModeSupported(static_cast<ExtPortMode>(mode))) {
                cJSON_AddItemToArray(supported, cJSON_CreateString(ExtPortMode_to_str(static_cast<ExtPortMode>(mode))));
            }
        }
    }

    if (active_mode == RS232) {
        addUartNode(port, responseDoc);
    }
}

void DockApi::addUartNode(uint8_t port, cJSON *responseDoc) {
    std::string uart_cfg = config_->getExternalPortUart(port);

    auto cfg = UartConfig::fromString(uart_cfg.c_str());
    if (cfg == nullptr) {
        cfg = UartConfig::defaultCfg();
        ESP_LOGW(TAG, "Invalid UART configuration for port %d: '%s'. Using default", port, uart_cfg.c_str());
    }

    cJSON *uart = cJSON_AddObjectToObject(responseDoc, "uart");
    if (uart) {
        cJSON_AddNumberToObject(uart, "baud_rate", cfg->baud_rate);
        cJSON_AddNumberToObject(uart, "data_bits", cfg->dataBits());
        cJSON_AddStringToObject(uart, "parity", cfg->parityAsString());
        cJSON_AddStringToObject(uart, "stop_bits", cfg->stopBitsAsString());
    }
}

uint16_t DockApi::processSetPortMode(const cJSON *root, cJSON *responseDoc) {
    uint8_t     port = cjson_get_int(root, "port");
    std::string mode_str = cjson_get_string(root, "mode", "");
    ExtPortMode mode = ExtPortMode_from_str(mode_str.c_str());

    if (port == 0 || port > EXTERNAL_PORT_COUNT || mode == ExtPortMode::PORT_MODE_MAX) {
        cJSON_AddStringToObject(responseDoc, msgError, "Invalid port");
        return 400;
    }
    if (!ports_.contains(port)) {
        cJSON_AddStringToObject(responseDoc, msgError, "Port not available");
        return 503;
    }

    // Stop serial bridge before mode change (prevents race with UART deinit)
    if (ports_[port]->getMode() == RS232 && bridges_[port - 1]) {
        serial_bridge_stop(bridges_[port - 1]);
    }

    if (mode == RS232) {
        cJSON *uart = cJSON_GetObjectItem(root, "uart");
        if (!cJSON_IsObject(uart)) {
            cJSON_AddStringToObject(responseDoc, msgError, "uart obj missing");
            return 400;
        }

        int         baud_rate = cjson_get_int(uart, "baud_rate");
        uint8_t     data_bits = cjson_get_int(uart, "data_bits");
        std::string parity = cjson_get_string(uart, "parity", "none");
        std::string stop_bits = cjson_get_string(uart, "stop_bits", "1");

        auto uart_cfg = UartConfig::fromParams(baud_rate, data_bits, parity, stop_bits);
        if (uart_cfg == nullptr) {
            cJSON_AddStringToObject(responseDoc, msgError, "Invalid uart configuration");
            return 400;
        }
        std::string uart_str = uart_cfg->toString();
        if (ports_[port]->setUartConfig(std::move(uart_cfg)) != ESP_OK) {
            return 400;
        }
        if (!config_->setExternalPortUart(port, uart_str.c_str())) {
            return 500;
        }
    }

    esp_err_t ret = ports_[port]->changeMode(mode);
    switch (ret) {
        case ESP_OK:
            config_->setExternalPortMode(port, mode);
            // Start bridge if switched to RS232
            if (mode == RS232 && bridges_[port - 1]) {
                serial_bridge_set_uart_queue(bridges_[port - 1], ports_[port]->getUartEventQueue());
                serial_bridge_start(bridges_[port - 1]);
            }
            return 200;
        case ESP_ERR_NOT_SUPPORTED:
            cJSON_AddStringToObject(responseDoc, msgError, "mode not supported");
            return 400;
        case ESP_ERR_INVALID_STATE:
            cJSON_AddStringToObject(responseDoc, msgError, "invalid peripheral detected");
            return 409;  // conflict: invalid peripheral detected
        case ESP_ERR_NOT_FINISHED:
            return 501;  // not yet implemented
        default:
            return 400;
    }
}

uint16_t DockApi::processGetPortTrigger(const cJSON *root, cJSON *responseDoc) {
    uint8_t port = cjson_get_int(root, "port");

    if (port == 0 || port > EXTERNAL_PORT_COUNT) {
        return 400;
    }
    if (!ports_.contains(port)) {
        return 503;
    }

    if (ports_[port]->getMode() != TRIGGER_5V) {
        return 404;
    }

    bool trigger = ports_[port]->isTriggerOn();

    cJSON_AddNumberToObject(responseDoc, "port", port);
    cJSON_AddBoolToObject(responseDoc, "trigger", trigger);

    return 200;
}

uint16_t DockApi::processSetPortTrigger(const cJSON *root) {
    uint8_t  port = cjson_get_int(root, "port");
    bool     trigger = cjson_get_bool(root, "trigger");
    uint32_t duration = cjson_get_int(root, "duration");

    if (port == 0 || port > EXTERNAL_PORT_COUNT) {
        return 400;
    }
    if (!ports_.contains(port)) {
        return 503;
    }

    esp_err_t ret;
    if (trigger && duration > 0) {
        ret = ports_[port]->triggerImpulse(duration);
    } else {
        ret = ports_[port]->setTrigger(trigger);
    }

    switch (ret) {
        case ESP_OK:
            return 200;
        // not configured as trigger
        case ESP_ERR_NOT_SUPPORTED:
            return 404;
        // impulse is already running
        case ESP_ERR_INVALID_STATE:
            return 409;
        // port is locked, e.g. port is initializing
        case ESP_ERR_NOT_ALLOWED:
            return 423;
        default:
            return 500;
    }
}

void DockApi::dockEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    DockApi *that = static_cast<DockApi *>(arg);

    switch (event_id) {
        case UC_EVENT_IR_LEARNING_START: {
            uc_event_ir_start_t *start = static_cast<uc_event_ir_start_t *>(event_data);
            bool                 raw = start && start->irFormat == static_cast<uint8_t>(IRFormat::RAW);

            std::string msg = "{\"type\":\"event\",\"msg\":\"ir_receive_on\"";
            msg += raw ? ",\"raw\":true}" : "}";
            that->web_->broadcastWsTxt(msg);
            break;
        }
        case UC_EVENT_IR_LEARNING_STOP: {
            std::string msg = "{\"type\":\"event\",\"msg\":\"ir_receive_off\"}";
            that->web_->broadcastWsTxt(msg);
            break;
        }
        case UC_EVENT_EXT_PORT_MODE: {
            uc_event_ext_port_mode_t *mode = static_cast<uc_event_ext_port_mode_t *>(event_data);

            if (!mode || mode->port > EXTERNAL_PORT_COUNT || !that->ports_.contains(mode->port)) {
                ESP_LOGE(TAG, "%s:%ld: invalid port", event_base, event_id);
                return;
            }

            // Sync serial bridge state for external mode changes (e.g. auto-detect at boot)
            if (mode->port >= 1 && mode->port <= EXTERNAL_PORT_COUNT && mode->state == ESP_OK) {
                serial_bridge_t *br = that->bridges_[mode->port - 1];
                if (br) {
                    if (mode->active_mode == RS232) {
                        serial_bridge_set_uart_queue(br, that->ports_[mode->port]->getUartEventQueue());
                        serial_bridge_start(br);
                    } else {
                        serial_bridge_stop(br);
                    }
                }
            }

            cJSON *responseDoc = cJSON_CreateObject();
            cJSON_AddStringToObject(responseDoc, "type", "event");
            cJSON_AddStringToObject(responseDoc, "msg", "port_mode");

            that->fillPortMode(that->ports_[mode->port], responseDoc);

            // make sure the new mode is sent in the event: fillPortMode retrieves the stored configuration
            cJSON_ReplaceItemInObject(responseDoc, "mode", cJSON_CreateString(ExtPortMode_to_str(mode->mode)));

            char       *resp = cJSON_PrintUnformatted(responseDoc);
            std::string msg = resp;
            cJSON_free(resp);
            cJSON_Delete(responseDoc);

            that->web_->broadcastWsTxt(msg);
            break;
        }
        default:
            // ignore
            return;
    }
}

void DockApi::authTimeoutCallback(TimerHandle_t timer_id) {
    DockApi *that = static_cast<DockApi *>(pvTimerGetTimerID(timer_id));
    that->checkAuthTimeouts();
}

void DockApi::checkAuthTimeouts() {
    if (xSemaphoreTake(unauthenticated_fds_mutex_, AUTH_MUTEX_BLOCK_TIME) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to lock FDs in timer");
        return;
    }

    // for logging open http & ws socket counts at each timer interval
    // web_->wsClientCount();

    uint64_t         now = esp_timer_get_time() / 1000 / 1000;  // in seconds
    std::vector<int> disconnected;

    for (auto it = unauthenticated_fds_.begin(); it != unauthenticated_fds_.end();) {
        if (now - it->second > UNAUTHENTICATED_TIMEOUT_SEC) {
            if (disconnected.size() == MAX_WS_CLOSE_COUNT) {
                ESP_LOGI(TAG, "Max number of unauth sessions reached: splitting up");
                break;
            }
            disconnected.push_back(it->first);
            it = unauthenticated_fds_.erase(it);
        } else {
            ++it;
        }
    }

    xSemaphoreGive(unauthenticated_fds_mutex_);

    for (int fd : disconnected) {
        // disconnect is async with work queue
        ESP_LOGW(TAG, "Disconnecting unauthenticated WS client: %d", fd);
        // use 1008 Policy Violation, normally used for "Authentication failure"
        // https://websocket.org/reference/close-codes/#1008-policy-violation
        web_->forceCloseWs(fd, 1008);
    }

    if (disconnected.size() == MAX_WS_CLOSE_COUNT) {
        // Too many unauthorized connections: close remaining connections in 500ms
        xTimerChangePeriod(auth_timer_, pdMS_TO_TICKS(500), 100);
    } else {
        // Check again in regular interval
        xTimerChangePeriod(auth_timer_, pdMS_TO_TICKS(AUTH_TIMER_CHECK_PERIOD_MS), 100);
    }
}
