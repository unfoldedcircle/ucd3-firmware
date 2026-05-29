// SPDX-FileCopyrightText: Copyright (c) 2026 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "log_router.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "LOG_ROUTER";

// Maximum number of log output callbacks. At the moment we only have the UC WS API.
#define MAX_LOG_CALLBACKS 1

// Maximum length of formatted log message. Note: we are not using long log messages, 256 should be plenty
#define MAX_LOG_LEN 256

// Callback entry structure
typedef struct {
    int                   id;
    log_output_callback_t callback;
    void                 *ctx;
    bool                  active;
} log_callback_entry_t;

// Static storage for callbacks
static log_callback_entry_t s_callbacks[MAX_LOG_CALLBACKS];
static int                  s_next_id = 1;
static bool                 s_initialized = false;

// Original vprintf function (outputs to console/UART)
static int (*g_original_vprintf)(const char *fmt, va_list args) = NULL;

// Helper to parse log message and extract tag/level
static void parse_log_message(const char *message, size_t len, char *tag, size_t tag_size, esp_log_level_t *level) {
    tag[0] = '\0';
    *level = ESP_LOG_INFO;

    if (len < 10) {
        return;
    }

    // Extract level (first character)
    switch (message[0]) {
        case 'E':
            *level = ESP_LOG_ERROR;
            break;
        case 'W':
            *level = ESP_LOG_WARN;
            break;
        case 'I':
            *level = ESP_LOG_INFO;
            break;
        case 'D':
            *level = ESP_LOG_DEBUG;
            break;
        case 'V':
            *level = ESP_LOG_VERBOSE;
            break;
        default:
            *level = ESP_LOG_INFO;
            break;
    }

    // Find tag between ") " and ":"
    const char *tag_start = strchr(message, ')');
    if (tag_start) {
        tag_start++;
        while (*tag_start == ' ' && tag_start < message + len) tag_start++;

        const char *tag_end = strchr(tag_start, ':');
        if (tag_end && (tag_end - tag_start < (ptrdiff_t)tag_size)) {
            size_t tag_len = tag_end - tag_start;
            if (tag_len >= tag_size) {
                tag_len = tag_size - 1;
            }
            strncpy(tag, tag_start, tag_len);
            tag[tag_len] = '\0';
        }
    }
}

// Helper to check if any callbacks are active (fast path check)
static inline bool has_active_callbacks(void) {
    // Quick check: if next_id is still 1, no callbacks were ever registered
    if (s_next_id == 1) {
        return false;
    }

    // Check if any callback is currently active
    for (int i = 0; i < MAX_LOG_CALLBACKS; i++) {
        if (s_callbacks[i].active) {
            return true;
        }
    }
    return false;
}

// Custom vprintf function that forwards to multiple outputs
static int log_router_vprintf(const char *fmt, va_list args) {
    // Always output to console (original vprintf) - this is the common case
    int ret = 0;
    if (g_original_vprintf) {
        va_list args_copy;
        va_copy(args_copy, args);
        ret = g_original_vprintf(fmt, args_copy);
        va_end(args_copy);
    }

    // FAST PATH: Skip all callback logic if no subscribers active
    // This is the most common case - no extra log clients
    if (!has_active_callbacks()) {
        return ret;
    }

    // SLOW PATH: Only execute when callbacks are registered
    // Format the message into a buffer for callbacks (don't use a static buffer for thread safety)
    char    log_buffer[MAX_LOG_LEN];
    va_list args_copy2;
    va_copy(args_copy2, args);
    int len = vsnprintf(log_buffer, sizeof(log_buffer), fmt, args_copy2);
    va_end(args_copy2);

    if (len <= 0) {
        return ret;
    }

    // Ensure null termination
    if (len >= (int)sizeof(log_buffer)) {
        len = sizeof(log_buffer) - 1;
    }
    log_buffer[len] = '\0';

    // Parse the message to extract tag and level
    char            tag[32] = {0};
    esp_log_level_t level = ESP_LOG_INFO;

    parse_log_message(log_buffer, len, tag, sizeof(tag), &level);

    // Forward to all registered callbacks
    for (int i = 0; i < MAX_LOG_CALLBACKS; i++) {
        if (s_callbacks[i].active && s_callbacks[i].callback) {
            s_callbacks[i].callback(tag, level, log_buffer, len, s_callbacks[i].ctx);
        }
    }

    return ret;
}

esp_err_t log_router_init(void) {
    if (s_initialized) {
        return ESP_OK;
    }

    memset(s_callbacks, 0, sizeof(s_callbacks));
    s_next_id = 1;

    // Store the original vprintf function (default outputs to UART/console)
    g_original_vprintf = esp_log_set_vprintf(log_router_vprintf);

    s_initialized = true;
    ESP_LOGI(TAG, "Log router initialized");

    return ESP_OK;
}

esp_err_t log_router_register_callback(log_output_callback_t callback, void *ctx, int *callback_id) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!callback) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < MAX_LOG_CALLBACKS; i++) {
        if (!s_callbacks[i].active) {
            s_callbacks[i].id = s_next_id++;
            s_callbacks[i].callback = callback;
            s_callbacks[i].ctx = ctx;
            s_callbacks[i].active = true;

            if (callback_id) {
                *callback_id = s_callbacks[i].id;
            }

            ESP_LOGD(TAG, "Callback registered with ID %d", s_callbacks[i].id);
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "Maximum number of callbacks reached (%d)", MAX_LOG_CALLBACKS);
    return ESP_ERR_NO_MEM;
}

esp_err_t log_router_unregister_callback(int callback_id) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = 0; i < MAX_LOG_CALLBACKS; i++) {
        if (s_callbacks[i].active && s_callbacks[i].id == callback_id) {
            s_callbacks[i].active = false;
            s_callbacks[i].callback = NULL;
            s_callbacks[i].ctx = NULL;

            ESP_LOGD(TAG, "Callback %d unregistered", callback_id);
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}
