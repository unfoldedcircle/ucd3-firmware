// SPDX-FileCopyrightText: Copyright (c) 2023 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "globalcache_server.h"

#include <esp_netif.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/err.h>
#include <lwip/ip4_addr.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>

#include <cstdio>
#include <new>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "globalcache.h"
#include "sdkconfig.h"
#include "string_util.h"
#include "uc_events.h"

// TODO(#28) use IDF LWIP_IPV6 & LWIP_IPV4 and test IPv6
#define USE_IPV4

#define TCP_API_PORT 4998
#define KEEPALIVE_IDLE 5
#define KEEPALIVE_INTERVAL 5
#define KEEPALIVE_COUNT 3

#define BEACON_INTERVAL_SEC 30
#define BEACON_BROADCAST_PORT 9131
#define BEACON_BROADCAST_IP_ADDR "239.255.250.250"

static const char *TAG_GC = "GC";
static const char *TAG_BEACON = "GCB";

/// Parameters for client socket task `socket_task`
struct GCClient {
    /// Client socket identifier
    int socket;
    /// MAC address of the dock
    char mac[13];
    /// Semaphore to release once client disconnects
    SemaphoreHandle_t semaphore;
    InfraredService  *irService;
};

bool send_string_to_socket(const int socket, const char *buf) {
    ESP_LOGD(TAG_GC, "[%d] Sending: %s", socket, buf);
    // send() can return less bytes than supplied length.
    // Walk-around for robust implementation.
    int length = strlen(buf);
    int to_write = length;
    while (to_write > 0) {
        int written = send(socket, buf + (length - to_write), to_write, 0);
        if (written < 0) {
            ESP_LOGE(TAG_GC, "[%d] Error occurred during sending: errno %d", socket, errno);
            // Failed to retransmit, giving up
            return false;
        }
        to_write -= written;
    }

    return true;
}

GlobalCacheServer::GlobalCacheServer(InfraredService *irService, Config *config, bool beacon)
    : m_irService(irService), m_config(config) {
    xTaskCreatePinnedToCore(tcp_server_task,  // task function
                            "GC server",      // task name
                            4000,             // stack size
                            this,             // task parameter
                            3,                // task priority
                            NULL,             // Task handle to keep track of created task
                            0);               // core

    if (beacon) {
        xTaskCreatePinnedToCore(beacon_task,  // task function
                                "GC beacon",  // task name
                                4000,         // stack size
                                this,         // task parameter
                                2,            // task priority
                                NULL,         // Task handle to keep track of created task
                                0);           // core
    }
}

/// @brief TCP server task listening for clients.
/// @param param pointer to GlobalCacheServer instance
/// @details Socket TCP server code inspired from:
///  https://github.com/espressif/esp-idf/blob/master/examples/protocols/sockets/tcp_server/main/tcp_server.c
void GlobalCacheServer::tcp_server_task(void *param) {
    char                    addr_str[128];
    int                     addr_family = AF_INET;
    int                     ip_protocol = 0;
    int                     keepAlive = 1;
    int                     keepIdle = KEEPALIVE_IDLE;
    int                     keepInterval = KEEPALIVE_INTERVAL;
    int                     keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;
    SemaphoreHandle_t       clientCountSemaphore;

    GlobalCacheServer *gc = reinterpret_cast<GlobalCacheServer *>(param);

    clientCountSemaphore =
        xSemaphoreCreateCounting(CONFIG_UCD_GLOBALCACHE_MAX_CLIENTS, CONFIG_UCD_GLOBALCACHE_MAX_CLIENTS);
    if (clientCountSemaphore == NULL) {
        ESP_LOGE(TAG_GC, "Error starting server: unable to create client semaphore");
        vTaskDelete(NULL);
        return;
    }

#ifdef USE_IPV6
    addr_family = AF_INET6;
    struct sockaddr_in6 *dest_addr_ip6 = (struct sockaddr_in6 *)&dest_addr;
    bzero(&dest_addr_ip6->sin6_addr.un, sizeof(dest_addr_ip6->sin6_addr.un));
    dest_addr_ip6->sin6_family = AF_INET6;
    dest_addr_ip6->sin6_port = htons(TCP_API_PORT);
    ip_protocol = IPPROTO_IPV6;
#endif
#ifdef USE_IPV4
    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(TCP_API_PORT);
        ip_protocol = IPPROTO_IP;
    }
#endif

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG_GC, "Unable to create socket: errno %d", errno);
        vSemaphoreDelete(clientCountSemaphore);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#if defined(USE_IPV4) && defined(USE_IPV6)
    // Note that by default IPV6 binds to both protocols, it must be disabled
    // if both protocols used at the same time (used in CI)
    setsockopt(listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
#endif

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG_GC, "Socket unable to bind: errno %d", errno);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG_GC, "Socket bound, port %d", TCP_API_PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG_GC, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (true) {
        // limit number of clients, wait until a client slot is available
        if (uxSemaphoreGetCount(clientCountSemaphore) == 0) {
            ESP_LOGW(TAG_GC, "Maximum number of clients reached, not accepting new connections");
        }
        if (xSemaphoreTake(clientCountSemaphore, portMAX_DELAY) == pdFALSE) {
            // timeout
            continue;
        }
        ESP_LOGD(TAG_GC, "Listening for clients");

        struct sockaddr_storage source_addr;  // Large enough for both IPv4 or IPv6
        socklen_t               addr_len = sizeof(source_addr);
        int                     sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG_GC, "Unable to accept connection: errno %d", errno);
            xSemaphoreGive(clientCountSemaphore);
            continue;
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        // Convert ip address to string
#ifdef USE_IPV4
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str));
        }
#endif
#ifdef USE_IPV6
        if (source_addr.ss_family == PF_INET6) {
            inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str));
        }
#endif
        ESP_LOGI(TAG_GC, "Socket accepted client: %s", addr_str);

        // hand over to new client Task
        GCClient *client = new (std::nothrow) GCClient();
        if (client == nullptr) {
            ESP_LOGE(TAG_GC, "Unable to allocate GC client");
            close(sock);
            xSemaphoreGive(clientCountSemaphore);
            continue;
        }
        client->socket = sock;
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(client->mac, sizeof(client->mac), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
                 mac[5]);
        client->semaphore = clientCountSemaphore;
        client->irService = gc->m_irService;
        if (xTaskCreatePinnedToCore(socket_task,     // task function
                                    "GC client",     // task name
                                    4000,            // stack size
                                    client,          // task parameter
                                    5,               // task priority
                                    NULL,            // Task handle to keep track of created task
                                    1) != pdPASS) {  // core
            ESP_LOGE(TAG_GC, "Unable to create GC client task");
            close(sock);
            delete client;
            xSemaphoreGive(clientCountSemaphore);
        }
    }

CLEAN_UP:
    close(listen_sock);
    vSemaphoreDelete(clientCountSemaphore);
    vTaskDelete(NULL);
}

/// @brief Client socket task to process request messages
///  The protocol is described e.g. http://www.globalcache.com/files/docs/API-GC-100.pdf.
/// @param param point to GCClient struct. The task is responsible to delete the struct when terminating.
void GlobalCacheServer::socket_task(void *param) {
    GCClient *client = reinterpret_cast<GCClient *>(param);

    int  len = 0;
    int  total_len = 0;
    char rx_buffer[1024];

    while (true) {
        len = recv(client->socket, rx_buffer + total_len, sizeof(rx_buffer) - total_len - 1, 0);
        if (len <= 0) {
            break;
        }
        total_len += len;
        rx_buffer[total_len] = 0;
        ESP_LOGD(TAG_GC, "[%d] Received %d bytes, total in buffer: %d", client->socket, len, total_len);

        char *ptr = rx_buffer;
        char *end_of_cmd;
        bool  should_break = false;

        while ((end_of_cmd = strchr(ptr, '\r')) != NULL) {
            *end_of_cmd = 0;
            char *current_cmd = ptr;
            ptr = end_of_cmd + 1;

            // find start of message, skip all non graphical representation characters
            // https://en.cppreference.com/w/c/string/byte/isgraph
            while (current_cmd != end_of_cmd && !isgraph(*current_cmd)) {
                current_cmd++;
            }
            if (current_cmd == end_of_cmd) {
                // ignore, no error (as iTach device)
                continue;
            }

            GCMsg req;
            auto  result = parseGcRequest(current_cmd, &req);
            if (result) {
                char buf[16];
                // global cache iTach error code
                snprintf(buf, sizeof(buf), "ERR_1:1,%03d\r", result);
                if (!send_string_to_socket(client->socket, buf)) {
                    should_break = true;
                    break;
                }
                continue;
            }

            ESP_LOGD(TAG_GC, "Command: %s,%d:%d,%s", req.command, req.module, req.port, req.param ? req.param : "");

            // API spec: disable learning if any other command is sent
            if (client->irService->isIrLearning() && strcmp(req.command, "stop_IRL") != 0) {
                client->irService->stopIrLearn();
            }

            if (strcmp(req.command, "sendir") == 0) {
                int16_t  clientId = IR_CLIENT_GC;
                uint32_t msgId = atoi(req.param);  // this _should_ point to ID
                auto     res = client->irService->sendGlobalCache(clientId, msgId, current_cmd, client->socket);
                ESP_LOGD(TAG_GC, "[%d] sendGlobalCache result: %d", client->socket, res);

                char        buf[17];
                const char *msg = nullptr;
                if (res == 0 || res == 200) {
                    // OK, async callback over the passed socket (code 200 shouldn't be used anymore)
                } else if (res == 202) {
                    // accepted IR repeat. Original iTach device doesn't send a reply, so we do the same!
                } else if (res > 0 && res < 100) {
                    // global cache iTach error code
                    snprintf(buf, sizeof(buf), "ERR_%d:%d,%03d\r", req.module, req.port, res);
                    msg = buf;
                } else if (res == 500) {
                    // invalid parameter
                    snprintf(buf, sizeof(buf), "ERR_%d:%d,023\r", req.module, req.port);
                    msg = buf;
                } else if (res == 429 || res == 503) {
                    msg = "busyir\r";
                } else {
                    // invalid command (unknown)
                    snprintf(buf, sizeof(buf), "ERR_%d:%d,001\r", req.module, req.port);
                    msg = buf;
                }

                if (msg && !send_string_to_socket(client->socket, msg)) {
                    should_break = true;
                    break;
                }
            } else if (strcmp(req.command, "stopir") == 0) {
                client->irService->stopSend();
                char reply[16];
                snprintf(reply, sizeof(reply), "%.12s\r", current_cmd);
                if (!send_string_to_socket(client->socket, reply)) {
                    should_break = true;
                    break;
                }
            } else if (strcmp(req.command, "getdevices") == 0) {
                if (!send_string_to_socket(client->socket, "device,0,0 ETHERNET\r")) {
                    should_break = true;
                    break;
                }
                int  ports = 4;
                char msg[64];
                snprintf(msg, sizeof(msg), "device,0,0 WIFI\rdevice,1,%d IR\rendlistdevices\r", ports);
                if (!send_string_to_socket(client->socket, msg)) {
                    should_break = true;
                    break;
                }
            } else if (strcmp(req.command, "getversion") == 0) {
                // GlobalCache iHelp doesn't like dots in version string, or device doesn't show up!
                char version[30];
                snprintf(version, sizeof(version), "%s\r", DOCK_VERSION[0] == 'v' ? DOCK_VERSION + 1 : DOCK_VERSION);
                replacechar(version, '.', '-');
                replacechar(version, '+', '-');
                if (!send_string_to_socket(client->socket, version)) {
                    should_break = true;
                    break;
                }
            } else if (strcmp(req.command, "getmac") == 0) {
                // command discovered with iHelp
                char mac[30];
                snprintf(mac, sizeof(mac), "MACaddress,%s\r", client->mac);
                if (!send_string_to_socket(client->socket, mac)) {
                    should_break = true;
                    break;
                }
            } else if (strcmp(req.command, "blink") == 0) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(
                    esp_event_post(UC_DOCK_EVENTS, UC_ACTION_IDENTIFY, NULL, 0, pdMS_TO_TICKS(200)));
            } else if (strcmp(req.command, "get_IRL") == 0) {
                int clientSocket = client->socket;
                client->irService->startIrLearn([clientSocket](IrRawResponse *response) -> esp_err_t {
                    if (response) {
                        char *sendir = raw_timings_to_gc_sendir(response->timings_us, response->timings_len,
                                                                response->frequency, "1:1", 1);
                        if (sendir) {
                            send_string_to_socket(clientSocket, sendir);
                            free(sendir);
                        }
                        free(response->timings_us);
                        delete response;
                    }
                    return ESP_OK;
                });
                if (!send_string_to_socket(client->socket, "IR Learner Enabled\r")) {
                    should_break = true;
                    break;
                }
            } else if (strcmp(req.command, "stop_IRL") == 0) {
                client->irService->stopIrLearn();
                if (!send_string_to_socket(client->socket, "IR Learner Disabled\r")) {
                    should_break = true;
                    break;
                }
            } else {
                // Command unrecognized
                char buf[17];
                snprintf(buf, sizeof(buf), "ERR_%d:%d,001\r", req.module, req.port);
                if (!send_string_to_socket(client->socket, buf)) {
                    should_break = true;
                    break;
                }
            }
        }

        if (should_break) {
            break;
        }

        // Move remaining data to the beginning of the buffer
        int remaining = (rx_buffer + total_len) - ptr;
        if (remaining > 0) {
            memmove(rx_buffer, ptr, remaining);
            total_len = remaining;
        } else {
            total_len = 0;
        }

        if (total_len == sizeof(rx_buffer) - 1) {
            // error: code too long / no carriage return
            const char *msg = (strncmp(rx_buffer, "sendir,", 7) == 0) ? "ERR 020\r" : "ERR 016\r";
            if (!send_string_to_socket(client->socket, msg)) {
                break;
            }
            total_len = 0;
        }
    }

    if (len < 0) {
        ESP_LOGE(TAG_GC, "[%d] Error occurred during receiving: errno %d", client->socket, errno);
    } else if (len == 0) {
        ESP_LOGI(TAG_GC, "[%d] Connection closed", client->socket);
    }

    // Stop IR learning if active (clears callback override)
    if (client->irService->isIrLearning()) {
        client->irService->stopIrLearn();
    }

    shutdown(client->socket, 0);
    close(client->socket);
    // release client slot
    xSemaphoreGive(client->semaphore);

    delete client;
    vTaskDelete(NULL);
}

/// @brief AMXB beacon advertisement with UDP broadcast.
/// @param param pointer to GlobalCacheServer instance
void GlobalCacheServer::beacon_task(void *param) {
    GlobalCacheServer *gc = reinterpret_cast<GlobalCacheServer *>(param);

    int                socket_fd;
    struct sockaddr_in sa, ra;
    char               buffer[220];

    // Simple UDP broadcast functionality with BSD socket
    // If we need IPv6: https://github.com/espressif/esp-idf/tree/master/examples/protocols/sockets/udp_multicast
    socket_fd = socket(PF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        ESP_LOGE(TAG_BEACON, "socket call failed: %d", socket_fd);
        vTaskDelete(NULL);
        return;
    }

    memset(&sa, 0, sizeof(struct sockaddr_in));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = IPADDR_ANY;
    sa.sin_port = htons(BEACON_BROADCAST_PORT);

    if (bind(socket_fd, (struct sockaddr *)&sa, sizeof(struct sockaddr_in)) == -1) {
        ESP_LOGE(TAG_BEACON, "Bind to port number %d failed: errno %d", BEACON_BROADCAST_PORT, errno);
        close(socket_fd);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG_BEACON, "Sending discovery beacons every %ds", BEACON_INTERVAL_SEC);

    memset(&ra, 0, sizeof(struct sockaddr_in));
    ra.sin_family = AF_INET;
    ra.sin_addr.s_addr = inet_addr(BEACON_BROADCAST_IP_ADDR);
    ra.sin_port = htons(BEACON_BROADCAST_PORT);

    // GlobalCache iHelp doesn't like dots and other characters in version string, or device doesn't show up!
    // This worked in older versions, new versions check more data.
    char version[30];
    snprintf(version, sizeof(version), "%s", DOCK_VERSION[0] == 'v' ? DOCK_VERSION + 1 : DOCK_VERSION);
    replacechar(version, '.', '-');
    replacechar(version, '+', '-');
    char    uuid[30];
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(uuid, sizeof(uuid), "UnfoldedCircle_%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);

    esp_netif_ip_info_t ipInfo;

    while (true) {
        esp_netif_t *netif = esp_netif_get_default_netif();
        // TODO(#28) ipv6?
        esp_err_t err = esp_netif_get_ip_info(netif, &ipInfo);
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        char ip_buf[64];
        snprintf(ip_buf, sizeof(ip_buf), IPSTR, IP2STR(&ipInfo.ip));

        int len = snprintf(buffer, sizeof(buffer),
                           "AMXB<-UUID=%s><-SDKClass=Utility><-Make=Unfolded "
                           "Circle><-Model=%s><-Revision=%s><-Config-URL=http://%s><-PCB_PN=%s><-Status=Ready>",
                           uuid, gc->m_config->getModel(), version, ip_buf, gc->m_config->getSerial());
        int sent_data = sendto(socket_fd, buffer, len, 0, (struct sockaddr *)&ra, sizeof(ra));

        ESP_LOGD(TAG_BEACON, "Sent %d bytes: %s", sent_data, buffer);

        vTaskDelay(pdMS_TO_TICKS(BEACON_INTERVAL_SEC * 1000));
    }

    close(socket_fd);
    vTaskDelete(NULL);
}
