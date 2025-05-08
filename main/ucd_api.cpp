// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ucd_api.h"

#include <stdio.h>
#include <time.h>

#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lwip/sockets.h"

#include "WebServer.h"
#include "config.h"
#include "led_pattern.h"
#include "network.h"
#include "service_ir.h"
#include "uc_events.h"

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
    cJSON_AddNumberToObject(root, "led_brightness", cfg.getLedBrightness());
#if defined(ETH_LED_PWM)
    cJSON_AddNumberToObject(root, "eth_led_brightness", cfg.getEthLedBrightness());
#endif
    cJSON_AddBoolToObject(root, "ir_learning", InfraredService::getInstance().isIrLearning());
    cJSON_AddBoolToObject(root, "ethernet", is_eth_link_up());
    cJSON_AddBoolToObject(root, "wifi", is_wifi_up());
    cJSON_AddStringToObject(root, "ssid", cfg.getWifiSsid().c_str());
    cJSON_AddNumberToObject(root, "volume", cfg.getVolume());
    cJSON_AddStringToObject(root, "uptime", get_uptime().c_str());
    cJSON_AddBoolToObject(root, "sntp", cfg.isNtpEnabled());
    cJSON_AddStringToObject(root, "reset_reason", getResetReason());

    if (cfg.isNtpEnabled()) {
        time_t    now;
        char      strftime_buf[64];
        struct tm timeinfo;

        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%FT%T%z", &timeinfo);
        cJSON_AddStringToObject(root, "time", strftime_buf);
    }

    char buf[1 + 8 * sizeof(uint32_t)];
    utoa(heap_caps_get_free_size(MALLOC_CAP_INTERNAL), buf, 10);
    cJSON_AddStringToObject(root, "free_heap", buf);
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

DockApi::DockApi(Config *config, WebServer *web, port_map_t ports) : config_(config), web_(web), ports_(ports) {
    assert(config_);
    assert(web_);

    web_->onWsEvent([this](httpd_req_t *req, int sockfd, WsTypeEnum type, uint8_t *payload, size_t length,
                           bool authenticated) -> esp_err_t {
        switch (type) {
            case WS_CONNECTED: {
                struct sockaddr_in6 addr_in;
                char                buf[20] = {0};
                if (WebServer::getRemoteIp(sockfd, &addr_in) == ESP_OK) {
                    ESP_LOGI(TAG, "[%s:%d] new WS client connection: %d",
                             inet_ntoa_r(addr_in.sin6_addr.un.u32_addr[3], buf, sizeof(buf)), addr_in.sin6_port,
                             sockfd);
                }

                // send auth request message
                if (authenticated) {
                    return ESP_OK;
                }
                WebServer *server = static_cast<WebServer *>(req->user_ctx);
                cJSON     *response = cJSON_CreateObject();
                cJSON_AddStringToObject(response, msgType, "auth_required");
                cJSON_AddStringToObject(response, "model", config_->getModel());
                cJSON_AddStringToObject(response, "revision", config_->getRevision());
                cJSON_AddStringToObject(response, "version", config_->getSoftwareVersion().c_str());
                // Attention: resp gets freed by WebServer!
                char     *resp = cJSON_PrintUnformatted(response);
                esp_err_t ret = server->sendWsTxt(sockfd, resp);
                cJSON_Delete(response);
                return ret;
            }
            case WS_DISCONNECTED:
                ESP_LOGI(TAG, "WS client disconnected: %d", sockfd);
                return ESP_OK;
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

esp_err_t DockApi::init() {
    // Register external-port-mode-change event
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(UC_DOCK_EVENTS, ESP_EVENT_ANY_ID, dockEventHandler, this, NULL), TAG,
        "Registering UC_DOCK_EVENTS failed");

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

        if (value == config_->getToken()) {
            // add client to authorized clients
            if (web->setAuthenticated(sockfd) == ESP_OK) {
                // token ok
                code = 200;
                ret = ESP_OK;
            }
        } else {
            // invalid token: disconnect
            cJSON_AddStringToObject(responseDoc, msgError, "Invalid token");
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
                }
            }
        }
        if (!(field && !ok) && (cJSON_HasObjectItem(root, "ssid") || cJSON_HasObjectItem(root, msgWifiPwd))) {
            auto ssid = cjson_get_string(root, "ssid", "");
            auto pwd = cjson_get_string(root, msgWifiPwd, "");

            if (config_->setWifi(ssid, pwd)) {
                ESP_LOGD(TAG, "Saving SSID: %s", ssid);

                std::string message;
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
                // TODO m_ledControl->setLedMaxBrightness(brightness);
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

        ESP_LOGD(TAG, "IR Send, format=%s, code=%s", format.c_str(), ir_code.c_str());

        if (!ir_code.empty() && !format.empty()) {
            uint16_t repeat = cjson_get_int(root, "repeat");
            bool     intSide = cjson_get_bool(root, "int_side");
            bool     intTop = cjson_get_bool(root, "int_top");
            bool     ext1 = cjson_get_bool(root, "ext1");
            bool     ext2 = cjson_get_bool(root, "ext2");

            int reqId = cjson_get_int(root, msgId);
            response = InfraredService::getInstance().send(sockfd, reqId, ir_code, format, repeat, intSide, intTop,
                                                           ext1, ext2);
            if (response == 0) {
                // asynchronous reply
                return ESP_OK;
            }
        }
        code = response;
    } else if (command == "ir_stop") {
        InfraredService::getInstance().stopSend();
        code = 200;
    } else if (command == "ir_receive_on") {
        InfraredService::getInstance().startIrLearn();
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
    } else if (command == "set_sntp") {
        bool ok = true;
        if (cJSON_HasObjectItem(root, "sntp_server1") || cJSON_HasObjectItem(root, "sntp_server2")) {
            std::string server1 = cjson_get_string(root, "sntp_server1", "");
            std::string server2 = cjson_get_string(root, "sntp_server2", "");
            if (!config_->setNtpServer(server1, server2)) {
                ok = false;
            }
        }
        item = cJSON_GetObjectItem(root, "sntp_enabled");
        if (item) {
            bool enabled = cJSON_IsTrue(item);
            if (!config_->enableNtp(enabled)) {
                ok = false;
            }
        }
        code = ok ? 200 : 400;
    } else if (command == "set_network") {
        // ‼️ Work in progress: API not finalized & static ip configuration is not yet implemented!
        bool          ok = true;
        network_cfg_t net_cfg;
        net_cfg.dhcp = cjson_get_bool(root, "dhcp", &ok);
        if (ok) {
            std::string value = cjson_get_string(root, "ip", "");
            if (value.empty()) {
                ok = false;
            } else {
                net_cfg.ip.ip.addr = ipaddr_addr(value.c_str());
            }
            value = cjson_get_string(root, "mask", "255.255.255.0");
            if (value.empty()) {
                ok = false;
            } else {
                net_cfg.ip.netmask.addr = ipaddr_addr(value.c_str());
            }
            value = cjson_get_string(root, "gw", "");
            if (value.empty()) {
                ok = false;
            } else {
                net_cfg.ip.gw.addr = ipaddr_addr(value.c_str());
            }

            if (ok) {
                ok = config_->setNetwork(net_cfg);
            }
        }
        code = ok ? 200 : 400;
    } else if (command == "get_network") {
        // ‼️ Work in progress: API not finalized & static ip configuration is not yet implemented!
        network_cfg_t net_cfg = config_->getNetwork();

        cJSON_AddBoolToObject(responseDoc, "dhcp", net_cfg.dhcp);
        if (!net_cfg.dhcp && net_cfg.ip.ip.addr && (net_cfg.ip.ip.addr != IPADDR_NONE)) {
            cJSON_AddStringToObject(responseDoc, "ip", ip4addr_ntoa((ip4_addr_t *)&net_cfg.ip.ip));
            cJSON_AddStringToObject(responseDoc, "mask", ip4addr_ntoa((ip4_addr_t *)&net_cfg.ip.netmask));
            cJSON_AddStringToObject(responseDoc, "gw", ip4addr_ntoa((ip4_addr_t *)&net_cfg.ip.gw));
        }
        std::string server = config_->getDnsServer1();
        if (!server.empty()) {
            cJSON_AddStringToObject(responseDoc, "dns1", server.c_str());
        }
        server = config_->getDnsServer2();
        if (!server.empty()) {
            cJSON_AddStringToObject(responseDoc, "dns2", server.c_str());
        }
        code = 200;
    } else if (command == "set_dns") {
        // ‼️ Work in progress: API not finalized & static ip configuration is not yet implemented!
        bool ok = true;
        if (cJSON_HasObjectItem(root, "dns1") || cJSON_HasObjectItem(root, "dns2")) {
            std::string server1 = cjson_get_string(root, "dns1", "");
            std::string server2 = cjson_get_string(root, "dns2", "");
            ok = config_->setDnsServer(server1, server2);
        }
        code = ok ? 200 : 400;
    } else if (command == "get_port_modes") {
        code = processGetPortModes(responseDoc);
    } else if (command == "get_port_mode") {
        code = processGetPortMode(root, responseDoc);
    } else if (command == "set_port_mode") {
        code = processSetPortMode(root);
    } else if (command == "get_port_trigger") {
        code = processGetPortTrigger(root, responseDoc);
    } else if (command == "set_port_trigger") {
        code = processSetPortTrigger(root);
    } else if (command == "reboot") {
        ESP_LOGW(TAG, "Rebooting");
        std::string message;
        cJSON_AddBoolToObject(responseDoc, "reboot", true);
        schedule_restart(web, 2000);
    } else if (command == "reset") {
        ESP_LOGW(TAG, "Reset");
        std::string message;
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
}

uint16_t DockApi::processSetPortMode(const cJSON *root) {
    uint8_t     port = cjson_get_int(root, "port");
    std::string mode_str = cjson_get_string(root, "mode", "");
    ExtPortMode mode = ExtPortMode_from_str(mode_str.c_str());

    if (port == 0 || port > EXTERNAL_PORT_COUNT || mode == ExtPortMode::PORT_MODE_MAX) {
        return 400;
    }
    if (!ports_.contains(port)) {
        return 503;
    }

    if (mode == RS232) {
        cJSON *uart = cJSON_GetObjectItem(root, "uart");
        if (!cJSON_IsObject(uart)) {
            return 400;
        }

        int         baud_rate = cjson_get_int(uart, "baud_rate");
        uint8_t     data_bits = cjson_get_int(uart, "data_bits");
        std::string parity = cjson_get_string(uart, "parity", "none");
        std::string stop_bits = cjson_get_string(uart, "stop_bits", "1");

        auto uart_cfg = UartConfig::fromParams(baud_rate, data_bits, parity, stop_bits);
        if (uart_cfg == nullptr) {
            return 400;
        }
        std::string uart_str = uart_cfg->toString();
        if (ports_[port]->setUartConfig(std::move(uart_cfg)) != ESP_OK) {
            return 400;
        }
        config_->setExternalPortUart(port, uart_str.c_str());
    }

    esp_err_t ret = ports_[port]->changeMode(mode);
    switch (ret) {
        case ESP_OK:
            config_->setExternalPortMode(port, mode);
            return 200;
        case ESP_ERR_NOT_SUPPORTED:
            return 400;
        case ESP_ERR_INVALID_STATE:
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
            std::string msg = "{\"type\":\"event\",\"msg\":\"ir_receive_on\"}";
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

            cJSON *responseDoc = cJSON_CreateObject();
            cJSON_AddStringToObject(responseDoc, "type", "event");
            cJSON_AddStringToObject(responseDoc, "msg", "port_mode");

            that->fillPortMode(that->ports_[mode->port], responseDoc);

            char       *resp = cJSON_PrintUnformatted(responseDoc);
            std::string msg = resp;
            delete (resp);
            cJSON_Delete(responseDoc);

            that->web_->broadcastWsTxt(msg);
            break;
        }
        default:
            // ignore
            return;
    }
}
