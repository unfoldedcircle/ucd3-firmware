// SPDX-FileCopyrightText: Copyright (c) 2023 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "globalcache.h"

#include <inttypes.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

uint8_t parseGcRequest(const char *request, GCMsg *msg) {
    if (request == nullptr || msg == nullptr) {
        return 17;  // bad command syntax
    }

    // command
    auto        len = sizeof(msg->command);
    const char *next = strchr(request, ',');
    if (next == NULL) {
        // simple command without module:port or parameters
        auto cmdLen = strlen(request);
        if (cmdLen >= len) {
            return 1;  // command name too long: unknown command
        }
        auto count = std::min(len, cmdLen);
        strncpy(msg->command, request, count);
        msg->command[count] = 0;
        msg->module = 0;
        msg->port = 0;
        msg->param = nullptr;
        return 0;
    }

    if ((next - request) >= len) {
        return 1;  // command name too long: unknown command
    }
    strncpy(msg->command, request, next - request);
    msg->command[next - request] = 0;

    // check for:  <module>:<port>,<param(s)>
    const char *current = next + 1;
    next = strchr(current, ',');
    if (next == NULL) {
        // no comma, check for: <module>:<port>
        next = strchr(current, ':');
        if (next == NULL) {
            msg->module = 0;
            msg->port = 0;
            msg->param = current;
            return 0;
        }
    }
    auto module = atoi(current);
    if (module != 1) {
        return 2;  // invalid module address
    }
    msg->module = module;

    next = strchr(current, ':');
    if (next == NULL) {
        return 3;  // invalid port address
    }
    current = next + 1;
    auto port = atoi(current);
    if (port < 1 || port > 15) {
        return 3;  // invalid port address
    }
    msg->port = port;

    // param(s)
    next = strchr(current, ',');
    if (next == NULL) {
        msg->param = nullptr;
    } else {
        msg->param = next + 1;
    }

    return 0;
}

/// Count decimal digits of a uint32_t value.
static size_t uint32_digits(uint32_t val) {
    if (val == 0) return 1;
    size_t digits = 0;
    while (val > 0) {
        val /= 10;
        digits++;
    }
    return digits;
}

char *raw_timings_to_gc_sendir(const uint16_t *timings_us, uint16_t timings_len, uint32_t frequency_hz,
                               const char *connector, uint16_t id) {
    if (timings_us == NULL || timings_len == 0 || frequency_hz == 0 || connector == NULL) {
        return NULL;
    }

    // Frequency must be within iTach range: 15000-500000
    if (frequency_hz < 15000 || frequency_hz > 500000) {
        return NULL;
    }

    // First pass: count logical pulses, compute exact string length,
    // and determine last value for potential odd-count duplication.
    // This is optimized for memory usage on ESP32, not compute efficiency!
    // It is not a time critical operation, only called once after a learned IR signal,
    // the 2 passes are acceptable
    uint16_t logical_count = 0;
    uint32_t last_periods = 0;
    size_t   values_len = 0;  // total chars for all comma-separated values
    uint16_t i = 0;

    while (i < timings_len) {
        uint32_t total_us = timings_us[i];
        i++;
        while (i + 1 < timings_len && timings_us[i] == 0) {
            i++;
            total_us += timings_us[i];
            i++;
        }
        last_periods = (uint32_t)(((uint64_t)total_us * frequency_hz + 500000) / 1000000);
        if (last_periods == 0) {
            last_periods = 1;
        }
        values_len += 1 + uint32_digits(last_periods);  // comma + digits
        logical_count++;
    }

    if (logical_count == 0) {
        return NULL;
    }

    // Account for duplicated OFF if odd count.
    if (logical_count % 2 != 0) {
        values_len += 1 + uint32_digits(last_periods);
    }

    // Calculate exact header length.
    // "sendir," + connector + "," + id + "," + freq + ",1,1"
    size_t header_len = 7 + strlen(connector) + 1 + uint32_digits(id) + 1 + uint32_digits(frequency_hz) + 4;

    size_t buf_size = header_len + values_len + 2;  // +2 for \r + null terminator
    char  *buf = (char *)malloc(buf_size);
    if (buf == NULL) {
        return NULL;
    }

    int offset = snprintf(buf, buf_size, "sendir,%s,%u,%" PRIu32 ",1,1", connector, id, frequency_hz);

    // Second pass: convert and write values.
    i = 0;
    while (i < timings_len) {
        uint32_t total_us = timings_us[i];
        i++;
        while (i + 1 < timings_len && timings_us[i] == 0) {
            i++;
            total_us += timings_us[i];
            i++;
        }
        uint32_t periods = (uint32_t)(((uint64_t)total_us * frequency_hz + 500000) / 1000000);
        if (periods == 0) {
            periods = 1;
        }
        offset += snprintf(buf + offset, buf_size - offset, ",%lu", (unsigned long)periods);
    }

    if (logical_count % 2 != 0) {
        offset += snprintf(buf + offset, buf_size - offset, ",%lu", (unsigned long)last_periods);
    }

    if (offset < buf_size - 1) {
        buf[offset] = '\r';
        buf[offset + 1] = '\0';
    }

    return buf;
}
