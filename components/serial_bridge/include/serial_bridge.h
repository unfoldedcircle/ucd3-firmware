// SPDX-FileCopyrightText: Copyright (c) 2026 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BRIDGE_RX_BUF_SIZE 256

#ifdef __cplusplus
extern "C" {
#endif

typedef struct serial_bridge serial_bridge_t;

/// Callback invoked on the bridge task for UART RX data or idle tick.
/// - data != NULL, len > 0: new UART data received
/// - data == NULL, len == 0: idle tick (called every ~20ms for timeout processing)
/// Implementations MUST be non-blocking.
/// @param port_index 1-based port number
/// @param data pointer to received bytes, or NULL for idle tick
/// @param len number of bytes, or 0 for idle tick
/// @param user_ctx context pointer provided at registration
typedef void (*serial_bridge_rx_cb_t)(uint8_t port_index, const uint8_t *data, size_t len, void *user_ctx);

typedef struct {
    uint8_t       port_index;        ///< 1-based port number
    uart_port_t   uart_num;          ///< UART port number (from ext_port_config_t)
    QueueHandle_t uart_event_queue;  ///< UART event queue (owned by ExternalPort)
    uint16_t      tcp_port;          ///< TCP listen port (4999, 5000)
    bool          tcp_enabled;       ///< Enable TCP server (false = UART + rx_callback only)
} serial_bridge_config_t;

/// Create a serial bridge instance. Does NOT start it.
serial_bridge_t *serial_bridge_create(const serial_bridge_config_t *config);

/// Start the bridge task (TCP server + UART bridging).
/// Call only when UART driver is installed and port is in RS232 mode.
esp_err_t serial_bridge_start(serial_bridge_t *bridge);

/// Stop the bridge task and close all TCP connections.
/// Safe to call if not started. Blocks until task exits.
esp_err_t serial_bridge_stop(serial_bridge_t *bridge);

/// Destroy the bridge instance and free all resources.
void serial_bridge_destroy(serial_bridge_t *bridge);

/// Send data to UART from an external source (e.g. WebSocket API).
/// Thread-safe. Non-blocking (uses UART TX ringbuffer).
esp_err_t serial_bridge_send_to_uart(serial_bridge_t *bridge, const uint8_t *data, size_t len);

/// Set the UART RX callback (e.g. for WebSocket forwarding).
/// Called from bridge task context. Only one callback supported. Pass NULL to clear.
void serial_bridge_set_rx_callback(serial_bridge_t *bridge, serial_bridge_rx_cb_t cb, void *user_ctx);

/// Update the UART event queue handle (needed after UART re-initialization).
void serial_bridge_set_uart_queue(serial_bridge_t *bridge, QueueHandle_t queue);

/// Enable or disable the TCP server. Takes effect on next serial_bridge_start().
void serial_bridge_set_tcp_enabled(serial_bridge_t *bridge, bool enabled);

/// Check if the bridge task is currently running.
bool serial_bridge_is_running(const serial_bridge_t *bridge);

#ifdef __cplusplus
}
#endif
