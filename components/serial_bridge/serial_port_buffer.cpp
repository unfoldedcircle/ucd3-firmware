// SPDX-FileCopyrightText: Copyright (c) 2026 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "serial_port_buffer.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *const TAG = "SER_BUF";
static const size_t      SERIAL_BUFFER_PSRAM_THRESHOLD = 1024;

bool SerialPortBuffer::allocate() {
    deallocate();

    if (buffer_size >= SERIAL_BUFFER_PSRAM_THRESHOLD) {
        buf = static_cast<uint8_t *>(heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM));
    }
    if (!buf) {
        buf = static_cast<uint8_t *>(heap_caps_malloc(buffer_size, MALLOC_CAP_INTERNAL));
    }
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate serial buffer (%d bytes)", buffer_size);
        return false;
    }

    if (!mutex) {
        mutex = xSemaphoreCreateMutex();
        if (!mutex) {
            deallocate();
            return false;
        }
    }

    len = 0;
    last_rx_us = 0;
    return true;
}

void SerialPortBuffer::deallocate() {
    if (buf) {
        heap_caps_free(buf);
        buf = nullptr;
    }
    len = 0;
    last_rx_us = 0;
}

void SerialPortBuffer::reset() {
    len = 0;
    last_rx_us = 0;
}
