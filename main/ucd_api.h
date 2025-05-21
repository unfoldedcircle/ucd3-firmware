// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdlib.h>

#include <string>

#include "esp_http_server.h"

#include "WebServer.h"
#include "cJSON.h"
#include "config.h"
#include "external_port.h"

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
    virtual ~DockApi() {}

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
    Config*    config_;
    WebServer* web_;
    port_map_t ports_;
};
