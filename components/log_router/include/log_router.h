// SPDX-FileCopyrightText: Copyright (c) 2026 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log_level.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Maximum length of a formatted log message including null terminator.
// Must match the message field size in log_entry_t.
#define LOG_MESSAGE_LEN 100

// Reserved level value used as a sentinel to signal the sender task to stop.
// Must not overlap any esp_log_level_t value (which are 0–5).
#define LOG_ENTRY_SENTINEL 0xFF

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A single log entry delivered through the queue.
 *
 * Allocated on the heap by log_router_vprintf. The receiver owns the pointer
 * and must free() it after use.
 */
typedef struct {
    char     tag[11];                   ///< Null-terminated log tag, e.g. "CHARGE"
    uint8_t  level;                     ///< esp_log_level_t cast to uint8_t
    uint16_t len;                       ///< Length of message, not including null terminator
    char     message[LOG_MESSAGE_LEN];  ///< Full formatted log line, null-terminated
} log_entry_t;

/**
 * @brief Initialise the log router.
 *
 * Installs a custom vprintf handler that intercepts all ESP_LOG* output.
 * Console/UART output is always preserved. The internal queue is created but
 * forwarding is inactive until log_router_start() is called.
 *
 * Must be called once at startup, before any task uses the log router.
 *
 * @return ESP_OK on success, ESP_ERR_NO_MEM if the queue cannot be created.
 */
esp_err_t log_router_init(void);

/**
 * @brief Start forwarding log entries to the queue.
 *
 * Returns the queue handle. The caller is responsible for draining it.
 * Entries are heap-allocated log_entry_t pointers; the receiver must free()
 * each one after processing.
 *
 * Calling start when already started is a no-op and returns the existing handle.
 *
 * @param[out] queue  Receives the queue handle.
 * @return ESP_OK on success.
 *         ESP_ERR_INVALID_STATE if log_router_init() has not been called.
 *         ESP_ERR_INVALID_ARG if queue is NULL.
 */
esp_err_t log_router_start(QueueHandle_t *queue);

/**
 * @brief Stop forwarding log entries to the queue.
 *
 * After this returns, no new entries will be enqueued. Entries already in the
 * queue are not flushed; the caller should drain the queue before destroying it.
 *
 * Calling stop when already stopped is a no-op.
 *
 * @return ESP_OK on success.
 *         ESP_ERR_INVALID_STATE if log_router_init() has not been called.
 */
esp_err_t log_router_stop(void);

#ifdef __cplusplus
}
#endif
