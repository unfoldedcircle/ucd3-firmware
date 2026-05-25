// SPDX-FileCopyrightText: Copyright (c) 2026 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "serial_bridge.h"

#include <cerrno>
#include <cstring>

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *const TAG = "SER_BR";

#define MAX_CLIENTS_PER_PORT 8
#define BRIDGE_TASK_STACK_SIZE 4096
#define BRIDGE_TASK_PRIORITY 5
#define BRIDGE_TASK_CORE 0
#define SELECT_TIMEOUT_US 20000  // 20ms
#define STOP_WAIT_ITERATIONS 25  // 25 * 20ms = 500ms max wait

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

struct serial_bridge {
    serial_bridge_config_t config;
    TaskHandle_t           task_handle;
    volatile bool          running;

    int     listen_fd;
    int     client_fds[MAX_CLIENTS_PER_PORT];
    uint8_t client_count;

    serial_bridge_rx_cb_t rx_callback;
    void                 *rx_callback_ctx;
};

// --- Forward declarations ---
static void bridge_task(void *arg);
static int  create_tcp_server(uint16_t port);
static void accept_client(serial_bridge_t *br);
static void close_client(serial_bridge_t *br, int slot);
static void broadcast_to_clients(serial_bridge_t *br, const uint8_t *data, size_t len);
static void notify_rx_callback(serial_bridge_t *br, const uint8_t *data, size_t len);
static int  build_fd_set(serial_bridge_t *br, fd_set *read_fds);
static void cleanup_sockets(serial_bridge_t *br);

// --- Public API ---

serial_bridge_t *serial_bridge_create(const serial_bridge_config_t *config) {
    if (!config) {
        return nullptr;
    }

    auto *br = static_cast<serial_bridge_t *>(calloc(1, sizeof(serial_bridge_t)));
    if (!br) {
        ESP_LOGE(TAG, "Failed to allocate serial_bridge_t");
        return nullptr;
    }

    br->config = *config;
    br->task_handle = nullptr;
    br->running = false;
    br->listen_fd = -1;
    br->client_count = 0;
    br->rx_callback = nullptr;
    br->rx_callback_ctx = nullptr;

    for (int i = 0; i < MAX_CLIENTS_PER_PORT; i++) {
        br->client_fds[i] = -1;
    }

    ESP_LOGI(TAG, "Port %d: bridge instance created (TCP port %d %s, UART %d)", config->port_index, config->tcp_port,
             config->tcp_enabled ? "enabled" : "disabled", config->uart_num);

    return br;
}

esp_err_t serial_bridge_start(serial_bridge_t *bridge) {
    if (!bridge) {
        return ESP_ERR_INVALID_ARG;
    }
    if (bridge->running) {
        ESP_LOGW(TAG, "Port %d: bridge already running", bridge->config.port_index);
        return ESP_OK;
    }

    bridge->running = true;
    bridge->listen_fd = -1;
    bridge->client_count = 0;

    for (int i = 0; i < MAX_CLIENTS_PER_PORT; i++) {
        bridge->client_fds[i] = -1;
    }

    char name[16];
    snprintf(name, sizeof(name), "ser_br_%d", bridge->config.port_index);

    BaseType_t ret = xTaskCreatePinnedToCore(bridge_task, name, BRIDGE_TASK_STACK_SIZE, bridge, BRIDGE_TASK_PRIORITY,
                                             &bridge->task_handle, BRIDGE_TASK_CORE);

    if (ret != pdPASS) {
        bridge->running = false;
        ESP_LOGE(TAG, "Port %d: failed to create bridge task", bridge->config.port_index);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Port %d: bridge started (TCP %s)", bridge->config.port_index,
             bridge->config.tcp_enabled ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t serial_bridge_stop(serial_bridge_t *bridge) {
    if (!bridge) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!bridge->running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Port %d: stopping bridge...", bridge->config.port_index);
    bridge->running = false;

    // Wait for task to exit
    for (int i = 0; i < STOP_WAIT_ITERATIONS; i++) {
        if (bridge->task_handle == nullptr) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (bridge->task_handle != nullptr) {
        ESP_LOGW(TAG, "Port %d: force-deleting bridge task", bridge->config.port_index);
        vTaskDelete(bridge->task_handle);
        cleanup_sockets(bridge);
        bridge->task_handle = nullptr;
    }

    ESP_LOGI(TAG, "Port %d: bridge stopped", bridge->config.port_index);
    return ESP_OK;
}

void serial_bridge_destroy(serial_bridge_t *bridge) {
    if (!bridge) {
        return;
    }

    serial_bridge_stop(bridge);

    ESP_LOGI(TAG, "Port %d: bridge instance destroyed", bridge->config.port_index);
    free(bridge);
}

esp_err_t serial_bridge_send_to_uart(serial_bridge_t *bridge, const uint8_t *data, size_t len) {
    if (!bridge || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!bridge->running) {
        return ESP_ERR_INVALID_STATE;
    }

    int written = uart_write_bytes(bridge->config.uart_num, data, len);
    if (written < 0) {
        return ESP_FAIL;
    }
    return (static_cast<size_t>(written) == len) ? ESP_OK : ESP_ERR_TIMEOUT;
}

void serial_bridge_set_rx_callback(serial_bridge_t *bridge, serial_bridge_rx_cb_t cb, void *user_ctx) {
    if (!bridge) {
        return;
    }
    bridge->rx_callback_ctx = user_ctx;
    bridge->rx_callback = cb;
}

void serial_bridge_set_uart_queue(serial_bridge_t *bridge, QueueHandle_t queue) {
    if (bridge) {
        bridge->config.uart_event_queue = queue;
    }
}

void serial_bridge_set_tcp_enabled(serial_bridge_t *bridge, bool enabled) {
    if (bridge) {
        bridge->config.tcp_enabled = enabled;
    }
}

bool serial_bridge_is_running(const serial_bridge_t *bridge) {
    if (!bridge) {
        return false;
    }
    return bridge->running;
}

// --- Internal Implementation ---

static int create_tcp_server(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        return -1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind port %d: errno %d", port, errno);
        close(fd);
        return -1;
    }

    if (listen(fd, MAX_CLIENTS_PER_PORT) < 0) {
        ESP_LOGE(TAG, "Failed to listen on port %d: errno %d", port, errno);
        close(fd);
        return -1;
    }

    // Set non-blocking
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    return fd;
}

static void accept_client(serial_bridge_t *br) {
    struct sockaddr_in client_addr = {};
    socklen_t          addr_len = sizeof(client_addr);

    int client_fd = accept(br->listen_fd, reinterpret_cast<struct sockaddr *>(&client_addr), &addr_len);
    if (client_fd < 0) {
        return;
    }

    if (br->client_count >= MAX_CLIENTS_PER_PORT) {
        ESP_LOGW(TAG, "Port %d: max clients (%d) reached, rejecting", br->config.port_index, MAX_CLIENTS_PER_PORT);
        close(client_fd);
        return;
    }

    // TCP_NODELAY for low latency
    int opt = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    // Keep-alive to detect dead connections
    setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
    int idle = 60;
    int interval = 10;
    int count = 3;
    setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
    setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));

    // Send timeout to prevent blocking on slow clients
    struct timeval send_timeout = {.tv_sec = 5, .tv_usec = 0};
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));

    // Non-blocking
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    // Find free slot
    for (int i = 0; i < MAX_CLIENTS_PER_PORT; i++) {
        if (br->client_fds[i] < 0) {
            br->client_fds[i] = client_fd;
            br->client_count++;

            char addr_str[INET_ADDRSTRLEN];
            inet_ntoa_r(client_addr.sin_addr, addr_str, sizeof(addr_str));
            ESP_LOGI(TAG, "Port %d: client connected from %s:%d (slot %d, fd %d, total %d)", br->config.port_index,
                     addr_str, ntohs(client_addr.sin_port), i, client_fd, br->client_count);
            return;
        }
    }

    // Should not happen due to count check, safety net
    close(client_fd);
}

static void close_client(serial_bridge_t *br, int slot) {
    if (slot < 0 || slot >= MAX_CLIENTS_PER_PORT || br->client_fds[slot] < 0) {
        return;
    }

    ESP_LOGI(TAG, "Port %d: client disconnected (slot %d, fd %d)", br->config.port_index, slot, br->client_fds[slot]);

    close(br->client_fds[slot]);
    br->client_fds[slot] = -1;
    if (br->client_count > 0) {
        br->client_count--;
    }
}

static void broadcast_to_clients(serial_bridge_t *br, const uint8_t *data, size_t len) {
    for (int i = 0; i < MAX_CLIENTS_PER_PORT; i++) {
        if (br->client_fds[i] < 0) {
            continue;
        }

        int sent = send(br->client_fds[i], data, len, MSG_DONTWAIT);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                ESP_LOGW(TAG, "Port %d: client slot %d send buffer full, dropping %d bytes", br->config.port_index, i,
                         (int)len);
            } else {
                ESP_LOGW(TAG, "Port %d: client slot %d send error (errno %d), closing", br->config.port_index, i,
                         errno);
                close_client(br, i);
            }
        }
    }
}

static void notify_rx_callback(serial_bridge_t *br, const uint8_t *data, size_t len) {
    if (br->rx_callback) {
        br->rx_callback(br->config.port_index, data, len, br->rx_callback_ctx);
    }
}

static int build_fd_set(serial_bridge_t *br, fd_set *read_fds) {
    FD_ZERO(read_fds);

    int max_fd = br->listen_fd;
    FD_SET(br->listen_fd, read_fds);

    for (int i = 0; i < MAX_CLIENTS_PER_PORT; i++) {
        if (br->client_fds[i] >= 0) {
            FD_SET(br->client_fds[i], read_fds);
            if (br->client_fds[i] > max_fd) {
                max_fd = br->client_fds[i];
            }
        }
    }

    return max_fd;
}

static void cleanup_sockets(serial_bridge_t *br) {
    for (int i = 0; i < MAX_CLIENTS_PER_PORT; i++) {
        if (br->client_fds[i] >= 0) {
            close(br->client_fds[i]);
            br->client_fds[i] = -1;
        }
    }
    br->client_count = 0;

    if (br->listen_fd >= 0) {
        close(br->listen_fd);
        br->listen_fd = -1;
    }
}

static void bridge_task(void *arg) {
    auto   *br = static_cast<serial_bridge_t *>(arg);
    uint8_t rx_buf[BRIDGE_RX_BUF_SIZE];
    bool    tcp_active = br->config.tcp_enabled;

    // Subscribe to task watchdog
    esp_task_wdt_add(NULL);

    // Only create TCP server if enabled
    if (tcp_active) {
        br->listen_fd = create_tcp_server(br->config.tcp_port);
        if (br->listen_fd < 0) {
            ESP_LOGE(TAG, "Port %d: failed to create TCP server on port %d", br->config.port_index,
                     br->config.tcp_port);
            // Continue without TCP — still service UART for rx_callback
            tcp_active = false;
        } else {
            ESP_LOGI(TAG, "Port %d: TCP server listening on port %d", br->config.port_index, br->config.tcp_port);
        }
    } else {
        ESP_LOGI(TAG, "Port %d: TCP disabled, UART callback only", br->config.port_index);
    }

    while (br->running) {
        esp_task_wdt_reset();

        if (tcp_active) {
            fd_set read_fds;
            int    max_fd = build_fd_set(br, &read_fds);

            struct timeval tv = {.tv_sec = 0, .tv_usec = SELECT_TIMEOUT_US};
            int            ready = select(max_fd + 1, &read_fds, NULL, NULL, &tv);

            if (ready > 0) {
                // Accept new connections
                if (FD_ISSET(br->listen_fd, &read_fds)) {
                    accept_client(br);
                }

                // Read from TCP clients → write to UART
                for (int i = 0; i < MAX_CLIENTS_PER_PORT; i++) {
                    if (br->client_fds[i] < 0) {
                        continue;
                    }
                    if (!FD_ISSET(br->client_fds[i], &read_fds)) {
                        continue;
                    }

                    int n = recv(br->client_fds[i], rx_buf, sizeof(rx_buf), 0);
                    if (n > 0) {
                        uart_write_bytes(br->config.uart_num, rx_buf, n);
                    } else if (n == 0) {
                        close_client(br, i);
                    } else {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            close_client(br, i);
                        }
                    }
                }
            } else if (ready < 0) {
                if (errno != EINTR) {
                    ESP_LOGE(TAG, "Port %d: select() error: errno %d", br->config.port_index, errno);
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            }
        } else {
            // No TCP: just wait for UART events with a delay
            vTaskDelay(pdMS_TO_TICKS(SELECT_TIMEOUT_US / 1000));
        }

        // Drain UART event queue → broadcast to TCP clients + callback
        if (br->config.uart_event_queue != NULL) {
            uart_event_t event;
            while (xQueueReceive(br->config.uart_event_queue, &event, 0) == pdTRUE) {
                switch (event.type) {
                    case UART_DATA: {
                        if (event.size == 0) {
                            break;
                        }
                        size_t remaining = event.size;
                        while (remaining > 0) {
                            size_t chunk_size = MIN(remaining, sizeof(rx_buf));
                            int    len = uart_read_bytes(br->config.uart_num, rx_buf, chunk_size, 0);
                            if (len <= 0) {
                                break;
                            }
                            if (tcp_active) {
                                broadcast_to_clients(br, rx_buf, len);
                            }
                            notify_rx_callback(br, rx_buf, len);
                            remaining -= len;
                        }
                        break;
                    }
                    case UART_FIFO_OVF:
                    case UART_BUFFER_FULL:
                        ESP_LOGW(TAG, "Port %d: UART %s, flushing", br->config.port_index,
                                 event.type == UART_FIFO_OVF ? "FIFO overflow" : "buffer full");
                        uart_flush_input(br->config.uart_num);
                        xQueueReset(br->config.uart_event_queue);
                        break;
                    case UART_FRAME_ERR:
                        ESP_LOGW(TAG, "Port %d: UART frame error", br->config.port_index);
                        break;
                    case UART_PARITY_ERR:
                        ESP_LOGW(TAG, "Port %d: UART parity error", br->config.port_index);
                        break;
                    default:
                        break;
                }
            }
        }

        // Idle tick for timeout processing (called every iteration)
        notify_rx_callback(br, NULL, 0);
    }

    // Cleanup
    cleanup_sockets(br);
    esp_task_wdt_delete(NULL);

    ESP_LOGI(TAG, "Port %d: bridge task exiting", br->config.port_index);
    br->task_handle = nullptr;
    vTaskDelete(NULL);
}
