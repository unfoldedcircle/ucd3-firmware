// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdlib.h>

#include <functional>
#include <string>

#include "esp_http_server.h"

// TODO plain and simple callbacks exposing esp_http_server internals.
//      See PsychicHttp for abstraction with Request, Endpoint, Handler classes.

typedef enum {
    /// not yet implemented
    WS_ERROR,
    /// not yet implemented
    WS_DISCONNECTED,
    WS_CONNECTED,
    WS_TEXT,
    WS_BIN,
} WsTypeEnum;

/**
 * Handler to call for WebSocket events.
 *
 * @param req native httpd request.
 * @param type WebSocket event type.
 * @param payload message buffer for binary or text messages, nullptr for other events. This buffer is automatically
 * freed after returning from the callback.
 * @param length length of payload buffer. For text messages, the buffer is 0-terminated but the termination character
 * is not included in length!
 * @param authenticated flag indicating if the WebSocket session has been authenticated or not.
 * @return If this function does not return ESP_OK, the underlying socket will be closed.
 */
typedef std::function<esp_err_t(httpd_req_t *req, int sockfd, WsTypeEnum type, uint8_t *payload, size_t length,
                                bool authenticated)>
    WebSocketServerEvent;

typedef std::function<esp_err_t(httpd_req_t *req)> RestCallback;

/// TODO Quick and dirty class wrapper to be able to start using cpp
///
/// This WebServer is tailored to this project and not a generic WebServer. Uses static routes.
/// Use another library like https://github.com/hoeken/PsychicHttp for a generic server using IDF esp_http_server
/// internally.
class WebServer {
 public:
    WebServer();
    virtual ~WebServer();

    /// @brief Initialize web server.
    ///
    /// The server is automatically started with the IP_EVENT_STA_GOT_IP event and stopped when disconnected.
    /// @param port server listening port
    /// @param base_path base path for serving files from an SPIFFS partition.
    /// @return - ESP_OK: Success
    ///  - ESP_ERR_NO_MEM: Cannot allocate memory for the handler
    ///  - Others: Fail
    esp_err_t init(uint16_t port, const char *base_path);

    /// @brief WebSocket callback event handler for static /ws route.
    /// @param handler callback function.
    void onWsEvent(WebSocketServerEvent handler);

    /// @brief REST API callback handler for static /api/pub/info route.
    /// @param handler callback function.
    void setRestHandler(RestCallback handler);

    /// @brief OTA callback handler for static /update route.
    /// @param handler callback function.
    void setOtaHandler(RestCallback handler);

    /// @brief Disconnect a client. This is normally used for a WebSocket client.
    /// @param id int client identifier
    void disconnect(int id);

    /// @brief Disconnect all clients. This is normally used for WebSocket clients.
    void disconnectAll();

    /// @brief Mark a WebSocket connection as authenticated. The authentication flag is passed to the
    /// WebSocketTextCallback handler.
    /// @param id client identifier
    /// @param authenticated true if the connection is considered authenticated
    /// @return ESP_OK if successful
    esp_err_t setAuthenticated(int id, bool authenticated = true);

    /// @brief Send text message to a WebSocket client
    /// @param id client identifier
    /// @param msg text message
    /// @return ESP_OK if successful
    esp_err_t sendWsTxt(int id, std::string &msg);
    esp_err_t sendWsTxt(int id, const char *msg);
    esp_err_t sendWsTxt(int id, char *msg);

    /// @brief Send a message to all authenticated WebSocket clients
    /// @param msg text message
    void broadcastWsTxt(std::string &msg);

    static esp_err_t getRemoteIp(int id, struct sockaddr_in6 *addr_in);

 private:
    esp_err_t startWebServer();
    esp_err_t stopWebServer();

    static esp_err_t ws_handler(httpd_req_t *req);
    static esp_err_t api_handler(httpd_req_t *req);
    static esp_err_t ota_handler(httpd_req_t *req);

    static void connect_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
    static void disconnect_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
    static void onClientConnectionEvent(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

 protected:
    httpd_handle_t server_;
    httpd_config_t config_;
    void          *context_;

    WebSocketServerEvent wsHandler_;
    RestCallback         restHandler_;
    RestCallback         otaHandler_;
};

/**
 * @brief Patched IDF httpd_resp_send_err method to send a custom json response, instead a hard-coded text/html
 * content-type.
 *
 * - The http status code is returned in field `code`.
 * - The `msg` parameter is returned in field `msg`.
 *
 * @note
 *  - This API is supposed to be called only from the context of
 *    a URI handler where httpd_req_t* request pointer is valid.
 *  - Once this API is called, all request headers are purged, so
 *    request headers need be copied into separate buffers if
 *    they are required later.
 *  - If you wish to send additional data in the body of the
 *    response, please use the lower-level functions directly.
 *
 * @param[in] req     Pointer to the HTTP request for which the response needs to be sent
 * @param[in] error   Error type to send, translated to http status code and json field `code`.
 * @param[in] msg     Error message string for json field `msg`.
 *
 * @return
 *  - ESP_OK : On successfully sending the response packet
 *  - ESP_ERR_INVALID_ARG : Null arguments
 *  - ESP_ERR_HTTPD_RESP_SEND   : Error in raw send
 *  - ESP_ERR_HTTPD_INVALID_REQ : Invalid request pointer
 */
esp_err_t httpd_resp_send_json_err(httpd_req_t *req, httpd_err_code_t error, const char *msg);
