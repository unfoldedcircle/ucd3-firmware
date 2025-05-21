// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

// based on IDF examples, license: Public Domain (or CC0 licensed, at your option.)
// - https://github.com/espressif/esp-idf/tree/master/examples/protocols/http_server/restful_server
// - https://github.com/espressif/esp-idf/tree/master/examples/protocols/http_server/ws_echo_server

#include "WebServer.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>

#include "esp_check.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_vfs.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"

static const char *TAG = "websrv";

WebServer::WebServer()
    : server_(nullptr), context_(nullptr), wsHandler_(nullptr), restHandler_(nullptr), otaHandler_(nullptr) {
    config_ = HTTPD_DEFAULT_CONFIG();
    // default httpd stack size of 4096 doesn't work with OTA: stack overflow in boot partition activation!
    config_.stack_size = CONFIG_UCD_WEB_TASK_STACKSIZE;
    config_.max_open_sockets = CONFIG_UCD_WEB_MAX_OPEN_SOCKETS;
}

WebServer::~WebServer() {
    if (context_) {
        free(context_);
    }
}

// ---- REST ----

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (10240)

typedef struct rest_server_context {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

static rest_server_context_t *rest_context;

/// Function to free context
static void session_free_func(void *ctx) {
    ESP_LOGI(TAG, "freeing WS session");
    free(ctx);
}

#define CHECK_FILE_EXTENSION(filename, ext) (strcasecmp(&filename[strlen(filename) - strlen(ext)], ext) == 0)

/// @brief Set HTTP response content-type according to file extension
/// @param req http request
/// @param filepath file to send back
/// @return ESP_OK : On success - ESP_ERR_HTTPD_INVALID_REQ : Invalid request pointer
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filepath) {
    const char *type = "text/plain";
    if (CHECK_FILE_EXTENSION(filepath, ".html")) {
        type = "text/html";
    } else if (CHECK_FILE_EXTENSION(filepath, ".js")) {
        type = "application/javascript";
    } else if (CHECK_FILE_EXTENSION(filepath, ".css")) {
        type = "text/css";
    } else if (CHECK_FILE_EXTENSION(filepath, ".png")) {
        type = "image/png";
    } else if (CHECK_FILE_EXTENSION(filepath, ".ico")) {
        type = "image/x-icon";
    } else if (CHECK_FILE_EXTENSION(filepath, ".svg")) {
        type = "image/svg+xml";
    }
    return httpd_resp_set_type(req, type);
}

/// @brief Set Connection: close header and CORS headers if enabled
/// @param req http request
/// @return ESP_OK: On successfully appending new header - ESP_ERR_HTTPD_RESP_HDR : Total additional headers exceed max
/// allowed - ESP_ERR_HTTPD_INVALID_REQ : Invalid request pointer
static esp_err_t set_common_headers(httpd_req_t *req) {
    // Force browsers to close connection, we only have limited resources.
    // If ESP_FAIL is returned the socket is automatically closed.
    esp_err_t ret = httpd_resp_set_hdr(req, "Connection", "close");

    if (CONFIG_UCD_WEB_ENABLE_CORS) {
        if (ret == ESP_OK) {
            ret = httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        }
        if (ret == ESP_OK) {
            ret = httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
        }
        if (ret == ESP_OK) {
            ret = httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
        }
    }

    return ret;
}

/// @brief Send HTTP response with the contents of the requested file
/// @param req http request
/// @return ESP_OK if successfully sent, ESP_FAIL if the url doesn't exist or the file couldn't be read
static esp_err_t rest_common_get_handler(httpd_req_t *req) {
    char filepath[FILE_PATH_MAX];

    rest_server_context_t *rest_context = (rest_server_context_t *)req->user_ctx;
    strlcpy(filepath, rest_context->base_path, sizeof(filepath));
    strlcat(filepath, req->uri, sizeof(filepath));
    if (req->uri[strlen(req->uri) - 1] == '/') {
        strlcat(filepath, "index.html", sizeof(filepath));
    }

    int fd = open(filepath, O_RDONLY, 0);
    if (fd == -1) {
        httpd_resp_set_type(req, "text/plain");
        if (errno == ENOENT) {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        } else {
            ESP_LOGE(TAG, "Failed to open file: %s (%d)", filepath, errno);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        }
        return ESP_FAIL;
    }

    set_common_headers(req);
    set_content_type_from_file(req, filepath);

    char   *chunk = rest_context->scratch;
    ssize_t read_bytes;
    do {
        // Read file in chunks into the scratch buffer
        read_bytes = read(fd, chunk, SCRATCH_BUFSIZE);
        if (read_bytes == -1) {
            ESP_LOGE(TAG, "Failed to read file : %s", filepath);
        } else if (read_bytes > 0) {
            // Send the buffer contents as HTTP response chunk
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                close(fd);
                ESP_LOGE(TAG, "File sending failed!");
                // Abort sending file
                httpd_resp_sendstr_chunk(req, NULL);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }
    } while (read_bytes > 0);
    close(fd);
    ESP_LOGD(TAG, "File sending complete");
    // Respond with an empty chunk to signal HTTP response completion
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/// @brief Simple API REST handler.
/// Currently only used for getting system information.
/// @param req http request
/// @return ESP_OK if successfully sent, ESP_FAIL if the url doesn't exist or the file couldn't be read
esp_err_t WebServer::api_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    set_common_headers(req);

    WebServer *server = static_cast<WebServer *>(req->user_ctx);
    if (server && server->restHandler_) {
        return server->restHandler_(req);
    }

    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"error\":\"500\",\"msg\":\"No handler configured\"}");
    return ESP_FAIL;
}

/// @brief OTA update handler.
/// @param req http request
/// @return ESP_OK if successfully processed, ESP_FAIL if the url doesn't exist or no OTA handler is installed
esp_err_t WebServer::ota_handler(httpd_req_t *req) {
    WebServer *server = static_cast<WebServer *>(req->user_ctx);
    if (server && server->otaHandler_) {
        return server->otaHandler_(req);
    }

    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "{\"error\":\"500\",\"msg\":\"No handler configured\"}");
    return ESP_FAIL;
}

// ---- WebSocket ----

/*
 * Structure holding server handle and internal socket fd in order to use out of request send
 *
 * Attention: the `payload` buffer will automatically be freed after send!
 */
struct async_resp_arg {
    httpd_handle_t hd;
    /// Socket file descriptor
    int fd;
    /// WebSocket frame type
    httpd_ws_type_t type;
    /// Pre-allocated data buffer.
    uint8_t *payload;
    /// Length of the WebSocket data
    size_t len;
};

/*
 * async send function, which we put into the httpd work queue
 */
static void ws_async_send(void *arg) {
    struct async_resp_arg *resp_arg = static_cast<async_resp_arg *>(arg);
    assert(resp_arg->payload);
    httpd_handle_t   hd = resp_arg->hd;
    int              fd = resp_arg->fd;
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = resp_arg->type;
    ws_pkt.payload = resp_arg->payload;
    ws_pkt.len = resp_arg->len;
    ws_pkt.len = (resp_arg->len == 0 && resp_arg->type == HTTPD_WS_TYPE_TEXT) ? strlen((const char *)resp_arg->payload)
                                                                              : resp_arg->len;

    ESP_LOGI(TAG, "ws_async_send: fd=%d, len=%d, msg=%s", fd, ws_pkt.len, (const char *)ws_pkt.payload);
    esp_err_t ret = httpd_ws_send_frame_async(hd, fd, &ws_pkt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send async: %d", ret);
    }
    free(resp_arg->payload);
    free(resp_arg);
}

/*
 * This handler echos back the received ws data
 * and triggers an async send if certain message received
 */
esp_err_t WebServer::ws_handler(httpd_req_t *req) {
    WebServer *server = static_cast<WebServer *>(req->user_ctx);

    if (!(server && server->wsHandler_)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (req->method == HTTP_GET) {
        auto fd = httpd_req_to_sockfd(req);
        bool authenticated = false;

        ESP_LOGD(TAG, "Handshake done, the new connection was opened");

        return server->wsHandler_(req, fd, WS_CONNECTED, nullptr, 0, authenticated);
    }

    // Create session's context for authentication flag if not already available
    if (!req->sess_ctx) {
        ESP_LOGI(TAG, "allocating new WS session");
        req->sess_ctx = malloc(sizeof(bool));
        ESP_RETURN_ON_FALSE(req->sess_ctx, ESP_ERR_NO_MEM, TAG, "Failed to allocate sess_ctx");
        req->free_ctx = session_free_func;
        *(bool *)req->sess_ctx = false;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t         *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    // Set max_len = 0 to get the frame len
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }

    WsTypeEnum ws_type = WS_ERROR;
    size_t     buf_len = ws_pkt.len;
    // only text & binary frames supported. Ping / pong handled by httpd server
    switch (ws_pkt.type) {
        case HTTPD_WS_TYPE_BINARY:
            ws_type = WS_BIN;
            break;
        case HTTPD_WS_TYPE_TEXT:
            ws_type = WS_TEXT;
            buf_len += 1;  // for NULL termination
            break;
        default:
            ESP_LOGW(TAG, "Received unsupported WS frame %d", ws_pkt.type);
            return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGD(TAG, "Packet type: %d, frame len:%d", ws_pkt.type, ws_pkt.len);
    if (!ws_pkt.len) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ws_pkt.len > CONFIG_UCD_WEB_MAX_WS_FRAME_SIZE) {
        ESP_LOGW(TAG, "WS frame too large: %d", ws_pkt.len);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Note: calloc initializes memory to 0
    buf = static_cast<uint8_t *>(calloc(1, buf_len));
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to calloc memory for buf");
        return ESP_ERR_NO_MEM;
    }
    ws_pkt.payload = buf;

    // Set max_len = ws_pkt.len to get the frame payload
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
        free(buf);
        return ret;
    }

    auto fd = httpd_req_to_sockfd(req);
    bool authenticated = *(bool *)req->sess_ctx;
    // TODO let client free buffer? Could be more efficient!
    ret = server->wsHandler_(req, fd, ws_type, buf, ws_pkt.len, authenticated);

    free(buf);
    return ret;
}

esp_err_t WebServer::startWebServer() {
    if (server_) {
        ESP_LOGW(TAG, "Web server is already running");
        return ESP_ERR_INVALID_STATE;
    }

    config_.uri_match_fn = httpd_uri_match_wildcard;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config_.server_port);
    esp_err_t ret = httpd_start(&server_, &config_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start server failed: %d", ret);
        return ret;
    }

    // Registering the ws handler
    ESP_LOGI(TAG, "Registering URI handlers");
    const httpd_uri_t ws = {.uri = "/ws",
                            .method = HTTP_GET,
                            .handler = ws_handler,
                            .user_ctx = this,
                            .is_websocket = true,
                            // let server handle PING and CLOSE frames
                            .handle_ws_control_frames = false,
                            .supported_subprotocol = NULL};
    httpd_register_uri_handler(server_, &ws);

    // URI handler for fetching system info
    httpd_uri_t system_info_get_uri = {.uri = "/api/pub/info",
                                       .method = HTTP_GET,
                                       .handler = api_handler,
                                       .user_ctx = this,
                                       .is_websocket = false,
                                       .handle_ws_control_frames = false,
                                       .supported_subprotocol = NULL};
    httpd_register_uri_handler(server_, &system_info_get_uri);

    // OTA update handler
    httpd_uri_t ota_post_uri = {.uri = "/update",
                                .method = HTTP_POST,
                                .handler = ota_handler,
                                .user_ctx = this,
                                .is_websocket = false,
                                .handle_ws_control_frames = false,
                                .supported_subprotocol = NULL};
    httpd_register_uri_handler(server_, &ota_post_uri);

    // URI handler for getting web server files
    httpd_uri_t common_get_uri = {.uri = "/*",
                                  .method = HTTP_GET,
                                  .handler = rest_common_get_handler,
                                  .user_ctx = rest_context,
                                  .is_websocket = false,
                                  .handle_ws_control_frames = false,
                                  .supported_subprotocol = NULL};
    httpd_register_uri_handler(server_, &common_get_uri);

    return ESP_OK;
}

esp_err_t WebServer::stopWebServer() {
    if (server_ == NULL) {
        return ESP_OK;
    }
    esp_err_t ret = httpd_stop(server_);
    server_ = NULL;
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop http server: %d", ret);
    }
    return ret;
}

// @deprecated not used. Web server is started once after getting an IP address and then just left running.
void WebServer::disconnect_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    WebServer *server = static_cast<WebServer *>(arg);
    assert(server);
    if (server->server_) {
        ESP_LOGI(TAG, "Stopping webserver");
        server->stopWebServer();
    }
}

void WebServer::connect_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    WebServer *server = static_cast<WebServer *>(arg);
    assert(server);
    if (server->server_ == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        server->startWebServer();
    }
}

void WebServer::onClientConnectionEvent(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    WebServer *server = static_cast<WebServer *>(arg);

    int *fd = (int *)event_data;
    switch (event_id) {
        case HTTP_SERVER_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "New connection: %d", *fd);
            break;
        case HTTP_SERVER_EVENT_DISCONNECTED:
            if (server && server->wsHandler_) {
                bool authenticated = false;
                server->wsHandler_(nullptr, *fd, WS_DISCONNECTED, nullptr, 0, authenticated);
            } else {
                ESP_LOGD(TAG, "Disconnected: %d", *fd);
            }
            break;
        default: {
        }
    }
}

esp_err_t WebServer::init(uint16_t port, const char *base_path) {
    if (context_) {
        return ESP_ERR_NOT_ALLOWED;
    }

    config_.server_port = port;

    rest_context = static_cast<rest_server_context_t *>(calloc(1, sizeof(rest_server_context_t)));
    if (rest_context == NULL) {
        return ESP_ERR_NO_MEM;
    }
    strlcpy(rest_context->base_path, base_path, sizeof(rest_context->base_path));

    context_ = rest_context;

    ESP_RETURN_ON_ERROR(esp_event_handler_register(ESP_HTTP_SERVER_EVENT, HTTP_SERVER_EVENT_ON_CONNECTED,
                                                   &WebServer::onClientConnectionEvent, this),
                        TAG, "Registering http event failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(ESP_HTTP_SERVER_EVENT, HTTP_SERVER_EVENT_DISCONNECTED,
                                                   &WebServer::onClientConnectionEvent, this),
                        TAG, "Registering http event failed");

    // start server once we get an IP address. Then leave it running.
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &WebServer::connect_handler, this),
                        TAG, "Registering IP_EVENT failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &WebServer::connect_handler, this),
                        TAG, "Registering ETH IP_EVENT failed");

    // ESP_RETURN_ON_ERROR(
    //     esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &WebServer::disconnect_handler, this),
    //     TAG, "Registering STA disconnect failed");
    // ESP_RETURN_ON_ERROR(
    //     esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, &WebServer::disconnect_handler, this),
    //     TAG, "Registering ETH disconnect failed");
    return ESP_OK;
}

void WebServer::onWsEvent(WebSocketServerEvent handler) {
    wsHandler_ = handler;
}

void WebServer::setRestHandler(RestCallback handler) {
    restHandler_ = handler;
}

void WebServer::setOtaHandler(RestCallback handler) {
    otaHandler_ = handler;
}

void WebServer::disconnect(int id) {
    if (!server_) {
        return;
    }

    httpd_sess_trigger_close(server_, id);
}

void WebServer::disconnectAll() {
    if (!server_) {
        return;
    }

    size_t clients = CONFIG_UCD_WEB_MAX_OPEN_SOCKETS;
    int    client_fds[CONFIG_UCD_WEB_MAX_OPEN_SOCKETS];
    ESP_RETURN_VOID_ON_ERROR(httpd_get_client_list(server_, &clients, client_fds), TAG,
                             "httpd_get_client_list failed!");

    for (size_t i = 0; i < clients; ++i) {
        httpd_sess_trigger_close(server_, client_fds[i]);
    }
}

esp_err_t WebServer::setAuthenticated(int id, bool authenticated) {
    if (!server_) {
        return ESP_FAIL;
    }

    void *sess_ctx = httpd_sess_get_ctx(server_, id);
    if (!sess_ctx) {
        ESP_LOGW(TAG, "Cannot set authentication: no session available for %d", id);
        return ESP_ERR_INVALID_ARG;
    }

    *(bool *)sess_ctx = authenticated;

    ESP_LOGI(TAG, "Set connection %d authenticated: %d", id, authenticated);

    return ESP_OK;
}

esp_err_t WebServer::sendWsTxt(int id, std::string &msg) {
    return sendWsTxt(id, msg.c_str());
}

esp_err_t WebServer::sendWsTxt(int id, const char *msg) {
    return sendWsTxt(id, strdup(msg));
}
esp_err_t WebServer::sendWsTxt(int id, char *msg) {
    if (!server_) {
        return ESP_FAIL;
    }

    if (httpd_ws_get_fd_info(server_, id) != HTTPD_WS_CLIENT_WEBSOCKET) {
        return ESP_ERR_INVALID_ARG;
    }

    // ESP_LOGI(TAG, "Active client (fd=%d) -> sending async message: %s", id, msg);
    struct async_resp_arg *resp_arg = static_cast<async_resp_arg *>(malloc(sizeof(struct async_resp_arg)));
    assert(resp_arg);
    resp_arg->hd = server_;
    resp_arg->fd = id;
    resp_arg->type = HTTPD_WS_TYPE_TEXT;
    // FIXME msg arg & buffer copy
    resp_arg->payload = (uint8_t *)msg;
    resp_arg->len = 0;
    esp_err_t ret = httpd_queue_work(resp_arg->hd, ws_async_send, resp_arg);
    if (ret != ESP_OK) {
        free(resp_arg);
        ESP_LOGE(TAG, "httpd_queue_work failed! %d", ret);
    }

    return ESP_OK;
}

void WebServer::broadcastWsTxt(std::string &msg) {
    if (!server_) {
        return;
    }

    size_t clients = CONFIG_UCD_WEB_MAX_OPEN_SOCKETS;
    int    client_fds[CONFIG_UCD_WEB_MAX_OPEN_SOCKETS];
    ESP_RETURN_VOID_ON_ERROR(httpd_get_client_list(server_, &clients, client_fds), TAG,
                             "httpd_get_client_list failed!");

    for (size_t i = 0; i < clients; ++i) {
        int fd = client_fds[i];
        sendWsTxt(fd, msg);
    }
}

esp_err_t WebServer::getRemoteIp(int fd, struct sockaddr_in6 *addr_in) {
    socklen_t addrlen = sizeof(*addr_in);
    if (lwip_getpeername(fd, (struct sockaddr *)addr_in, &addrlen) != -1) {
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Error getting peer's IP/port");
        return ESP_FAIL;
    }
}

esp_err_t httpd_resp_send_json_err(httpd_req_t *req, httpd_err_code_t error, const char *usr_msg) {
    esp_err_t   ret;
    const char *status;
    u_int16_t   code = 500;

    switch (error) {
        case HTTPD_501_METHOD_NOT_IMPLEMENTED:
            status = "501 Method Not Implemented";
            code = 501;
            break;
        case HTTPD_505_VERSION_NOT_SUPPORTED:
            status = "505 Version Not Supported";
            code = 505;
            break;
        case HTTPD_400_BAD_REQUEST:
            status = "400 Bad Request";
            code = 400;
            break;
        case HTTPD_401_UNAUTHORIZED:
            status = "401 Unauthorized";
            code = 401;
            break;
        case HTTPD_403_FORBIDDEN:
            status = "403 Forbidden";
            code = 403;
            break;
        case HTTPD_404_NOT_FOUND:
            status = "404 Not Found";
            code = 404;
            break;
        case HTTPD_405_METHOD_NOT_ALLOWED:
            status = "405 Method Not Allowed";
            code = 405;
            break;
        case HTTPD_408_REQ_TIMEOUT:
            status = "408 Request Timeout";
            code = 408;
            break;
        case HTTPD_414_URI_TOO_LONG:
            status = "414 URI Too Long";
            code = 414;
            break;
        case HTTPD_411_LENGTH_REQUIRED:
            status = "411 Length Required";
            code = 411;
            break;
        case HTTPD_431_REQ_HDR_FIELDS_TOO_LARGE:
            status = "431 Request Header Fields Too Large";
            code = 431;
            break;
        case HTTPD_500_INTERNAL_SERVER_ERROR:
        default:
            status = "500 Internal Server Error";
            code = 500;
    }

    char json_buf[200];
    snprintf(json_buf, sizeof(json_buf), "{\"code\": %d, \"msg\":\"%s\"}", code, usr_msg);

    /* Set error code in HTTP response */
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, HTTPD_TYPE_JSON);

#ifdef CONFIG_HTTPD_ERR_RESP_NO_DELAY
    /* Use TCP_NODELAY option to force socket to send data in buffer
     * This ensures that the error message is sent before the socket
     * is closed */
    int fd = httpd_req_to_sockfd(req);
    int nodelay = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
        /* If failed to turn on TCP_NODELAY, throw warning and continue */
        ESP_LOGW(TAG, "error calling setsockopt : %d", errno);
        nodelay = 0;
    }
#endif

    /* Send HTTP error message */
    ret = httpd_resp_send(req, json_buf, HTTPD_RESP_USE_STRLEN);

#ifdef CONFIG_HTTPD_ERR_RESP_NO_DELAY
    /* If TCP_NODELAY was set successfully above, time to disable it */
    if (nodelay == 1) {
        nodelay = 0;
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
            /* If failed to turn off TCP_NODELAY, throw error and
             * return failure to signal for socket closure */
            ESP_LOGE(TAG, "error calling setsockopt : %d", errno);
            return ESP_ERR_INVALID_STATE;
        }
    }
#endif
    esp_event_post(ESP_HTTP_SERVER_EVENT, HTTP_SERVER_EVENT_ERROR, &error, sizeof(httpd_err_code_t), portMAX_DELAY);

    return ret;
}
