// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdlib.h>

#include <map>
#include <set>
#include <string>

#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log_level.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "WebServer.h"
#include "board.h"
#include "cJSON.h"
#include "config.h"
#include "external_port.h"
#include "serial_bridge.h"
#include "serial_port_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

std::string get_uptime(void);

void  fill_sysinfo_to_json(cJSON* root);
char* get_sysinfo_json(void);

#ifdef __cplusplus
}
#endif

class DockApi {
 public:
    explicit DockApi(Config* config, WebServer* web, port_map_t ports);
    virtual ~DockApi();

    esp_err_t init();

 protected:
    /// @brief Callback for received WebSocket text messages.
    /// @param req HTTP request
    /// @param sockfd socket connection handle
    /// @param buf received text message, zero terminated
    /// @param len length of buf
    /// @param authenticated if or not the connection has been authenticated or not.
    /// @return ESP_OK if the message was successfully handled, otherwise ESP_ERR_## to close the WebSocket.
    esp_err_t processRequest(httpd_req_t* req, int sockfd, const char* text, size_t len, bool authenticated);

    uint16_t processGetPortModes(cJSON* responseDoc);
    uint16_t processGetPortMode(const cJSON* root, cJSON* responseDoc);
    void     fillPortMode(const std::shared_ptr<ExternalPort>& extPort, cJSON* responseDoc);
    uint16_t processSetPortMode(const cJSON* root);
    uint16_t processGetPortTrigger(const cJSON* root, cJSON* responseDoc);
    uint16_t processSetPortTrigger(const cJSON* root);

    static void dockEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

 private:
    /// @brief Periodic timer to check for unauthenticated WebSocket connections.
    static void authTimeoutCallback(TimerHandle_t timer_id);
    void        checkAuthTimeouts();

    uint16_t processEnableSerialEvents(int sockfd, const cJSON* root);
    uint16_t processSendSerial(const cJSON* root);
    uint16_t processSetSerialConfig(const cJSON* root);
    uint16_t processGetSerialConfig(const cJSON* root, cJSON* responseDoc);

    static void serialRxCallback(uint8_t port_index, const uint8_t* data, size_t len, void* user_ctx);
    void        handleSerialRx(uint8_t port_index, const uint8_t* data, size_t len);
    void        flushSerialBuffer(uint8_t port_index, SerialPortBuffer& pbuf);
    void        sendSerialEvent(uint8_t port_index, const uint8_t* data, size_t len);

    void initSerialBuffers();
    void deinitSerialBuffers();
    void loadSerialBufferConfig(uint8_t port);

    // Log streaming support
    void             handleEnableLogEvents(int sockfd, const cJSON* root, cJSON* responseDoc);
    void             sendLogToSubscribers(const char* tag, esp_log_level_t level, const char* message, size_t len);
    static esp_err_t logCallback(const char* tag, esp_log_level_t level, const char* message, size_t len, void* ctx);

    Config*    config_;
    WebServer* web_;
    port_map_t ports_;
    int        sockfdSendIR_;

    std::map<int, uint64_t> unauthenticated_fds_;
    SemaphoreHandle_t       unauthenticated_fds_mutex_;
    TimerHandle_t           auth_timer_;

    serial_bridge_t* bridges_[EXTERNAL_PORT_COUNT] = {};
    SerialPortBuffer serial_buffers_[EXTERNAL_PORT_COUNT];

    // Per-client serial event subscriptions: sockfd -> port bitmask (bit 0 = port 1, bit 1 = port 2)
    std::map<int, uint8_t> serial_event_fds_;
    SemaphoreHandle_t      serial_event_mutex_;

    // Per-client log streaming subscriptions
    std::set<int>     log_subscribers_;
    SemaphoreHandle_t log_subscribers_mutex_;
    int               log_callback_id_;
};
