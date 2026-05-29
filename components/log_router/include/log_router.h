// SPDX-FileCopyrightText: Copyright (c) 2026 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_log_level.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback function type for sending log messages to external destinations
 *
 * @param tag Log tag
 * @param level Log level
 * @param message Full log message (includes timestamp, level, tag, text)
 * @param len Length of the message
 * @param ctx User context
 * @return esp_err_t ESP_OK on success
 */
typedef esp_err_t (*log_output_callback_t)(const char *tag, esp_log_level_t level, const char *message, size_t len,
                                           void *ctx);

/**
 * @brief Initialize the log router
 *
 * This sets up a custom log writer that forwards all ESP_LOG* messages to
 * registered output callbacks while keeping the default console output active.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t log_router_init(void);

/**
 * @brief Register a callback for log output (e.g., WebSocket)
 *
 * Multiple callbacks can be registered. Each will receive all log messages.
 *
 * @param callback Callback function (NULL to unregister)
 * @param ctx User context passed to callback
 * @param callback_id Output parameter to store the callback ID for later removal
 * @return esp_err_t ESP_OK on success, ESP_ERR_NO_MEM if max callbacks reached
 */
esp_err_t log_router_register_callback(log_output_callback_t callback, void *ctx, int *callback_id);

/**
 * @brief Unregister a previously registered callback
 *
 * @param callback_id ID returned from log_router_register_callback
 * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FOUND if ID not found
 */
esp_err_t log_router_unregister_callback(int callback_id);

#ifdef __cplusplus
}
#endif
