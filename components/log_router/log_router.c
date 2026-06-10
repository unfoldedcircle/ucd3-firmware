// SPDX-FileCopyrightText: Copyright (c) 2026 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "log_router.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "LOG_ROUTER";

// Number of log_entry_t* pointers that can be buffered.
// If the sender task falls behind, oldest entries are dropped (non-blocking send).
#define LOG_QUEUE_DEPTH 64

// Original vprintf installed by IDF (outputs to UART/console). Always called.
static vprintf_like_t g_original_vprintf = NULL;

// The queue through which log_entry_t* pointers are delivered to the consumer.
static QueueHandle_t g_log_queue = NULL;

// Atomic flag: true when the consumer has called log_router_start().
// acquire/release ordering ensures that g_log_queue is visible before
// forwarding begins, and that the NULL write in stop is visible after the flag drops.
static _Atomic bool g_active = false;

static bool g_initialized = false;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static esp_log_level_t parse_level(char c) {
    switch (c) {
        case 'E':
            return ESP_LOG_ERROR;
        case 'W':
            return ESP_LOG_WARN;
        case 'I':
            return ESP_LOG_INFO;
        case 'D':
            return ESP_LOG_DEBUG;
        case 'V':
            return ESP_LOG_VERBOSE;
        default:
            return ESP_LOG_INFO;
    }
}

static void parse_tag(const char *message, size_t len, char *tag, size_t tag_size) {
    tag[0] = '\0';
    if (len < 10) {
        return;
    }

    // Format: "L (timestamp) TAG: text"
    // Tag lives between ") " and ":"
    const char *tag_start = strchr(message, ')');
    if (!tag_start) {
        return;
    }

    tag_start++;
    while (*tag_start == ' ' && tag_start < message + len) {
        tag_start++;
    }

    const char *tag_end = strchr(tag_start, ':');
    if (!tag_end) {
        return;
    }

    size_t tag_len = (size_t)(tag_end - tag_start);
    if (tag_len >= tag_size) {
        tag_len = tag_size - 1;
    }

    memcpy(tag, tag_start, tag_len);
    tag[tag_len] = '\0';
}

// ---------------------------------------------------------------------------
// Custom vprintf — installed by log_router_init()
// ---------------------------------------------------------------------------

static int log_router_vprintf(const char *fmt, va_list args) {
    // Always forward to the original console handler first.
    int ret = 0;
    if (g_original_vprintf) {
        va_list console_args;
        va_copy(console_args, args);
        ret = g_original_vprintf(fmt, console_args);
        va_end(console_args);
    }

    // Fast path: skip queue logic when not active.
    // acquire: if we observe true, g_log_queue is guaranteed visible.
    if (!atomic_load_explicit(&g_active, memory_order_acquire)) {
        return ret;
    }

    // Allocate entry. If the heap is exhausted we drop the message rather than block.
    // The log hook must never stall the calling task.
    log_entry_t *entry = (log_entry_t *)malloc(sizeof(log_entry_t));
    if (!entry) {
        return ret;
    }

    // Format into the entry's message buffer.
    va_list fmt_args;
    va_copy(fmt_args, args);
    int len = vsnprintf(entry->message, LOG_MESSAGE_LEN, fmt, fmt_args);
    va_end(fmt_args);

    if (len <= 0) {
        free(entry);
        return ret;
    }

    entry->len = (len < LOG_MESSAGE_LEN) ? (uint16_t)len : (uint16_t)(LOG_MESSAGE_LEN - 1);
    entry->message[entry->len] = '\0';

    // Parse level and tag directly from the formatted line.
    entry->level = (uint8_t)parse_level(entry->message[0]);
    parse_tag(entry->message, entry->len, entry->tag, sizeof(entry->tag));

    // Post to queue non-blocking. Drop if full, producer must never block.
    if (xQueueSend(g_log_queue, &entry, 0) != pdTRUE) {
        free(entry);
    }

    return ret;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t log_router_init(void) {
    if (g_initialized) {
        return ESP_OK;
    }

    g_log_queue = xQueueCreate(LOG_QUEUE_DEPTH, sizeof(log_entry_t *));
    if (!g_log_queue) {
        return ESP_ERR_NO_MEM;
    }

    atomic_store_explicit(&g_active, false, memory_order_release);

    g_original_vprintf = esp_log_set_vprintf(log_router_vprintf);
    g_initialized = true;

    ESP_LOGI(TAG, "Log router initialized");
    return ESP_OK;
}

esp_err_t log_router_start(QueueHandle_t *queue) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!queue) {
        return ESP_ERR_INVALID_ARG;
    }

    *queue = g_log_queue;
    // ensures g_log_queue is visible to any core that observes g_active == true
    atomic_store_explicit(&g_active, true, memory_order_release);

    ESP_LOGI(TAG, "Log forwarding started");
    return ESP_OK;
}

esp_err_t log_router_stop(void) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Flip the active flag first so no new real entries are enqueued after this point.
    atomic_store_explicit(&g_active, false, memory_order_release);

    // Post a sentinel to unblock the consumer if it is waiting in xQueueReceive.
    // Allocated on the heap like real entries so the consumer can free() it uniformly.
    log_entry_t *sentinel = (log_entry_t *)malloc(sizeof(log_entry_t));
    if (sentinel) {
        memset(sentinel, 0, sizeof(log_entry_t));
        sentinel->level = LOG_ENTRY_SENTINEL;
        if (xQueueSend(g_log_queue, &sentinel, pdMS_TO_TICKS(100)) != pdTRUE) {
            free(sentinel);  // queue full — task will drain and eventually idle-exit
            ESP_LOGW(TAG, "Failed to enqueue sentinel, task will drain on next message");
        }
    }

    ESP_LOGI(TAG, "Log forwarding stopped");
    return ESP_OK;
}
