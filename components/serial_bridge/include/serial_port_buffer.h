// SPDX-FileCopyrightText: Copyright (c) 2026 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/// Per-port serial buffering configuration and runtime state
struct SerialPortBuffer {
    enum Mode { LINE = 0, CHUNK = 1 };

    // Configuration (persisted via Config class)
    Mode     mode = LINE;
    uint8_t  terminator = '\n';
    size_t   buffer_size = 512;
    uint32_t timeout_ms = 100;

    // Runtime state
    uint8_t          *buf = nullptr;
    size_t            len = 0;
    int64_t           last_rx_us = 0;  // esp_timer_get_time() when last byte was buffered
    SemaphoreHandle_t mutex = nullptr;

    /// Allocate buffer based on current buffer_size. Returns true on success.
    bool allocate();
    /// Free buffer memory.
    void deallocate();
    /// Reset buffer state (clear contents, keep allocation).
    void reset();
};
