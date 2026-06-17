// SPDX-FileCopyrightText: Copyright (c) 2026 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "raw_timings.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

static const uint16_t kDefaultFrequencyHz = 38000;

/// Check if character is a whitespace separator (space, tab, CR, LF, single-byte NBSP).
static inline bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || (uint8_t)c == 0xA0;
}

/// Check if the string at current position is a UTF-8 non-breaking space (0xC2 0xA0).
/// Returns 2 if it is an NBSP, 0 otherwise.
static inline size_t is_utf8_nbsp(const char *str, size_t str_len, size_t pos) {
    if (pos + 1 < str_len && (uint8_t)str[pos] == 0xC2 && (uint8_t)str[pos + 1] == 0xA0) {
        return 2;
    }
    return 0;
}

/// Advance index past all separator characters, including multi-byte NBSP.
static void skip_separators(const char *str, size_t str_len, size_t &pos) {
    while (pos < str_len) {
        if (is_whitespace(str[pos])) {
            pos++;
        } else {
            size_t nbsp_len = is_utf8_nbsp(str, str_len, pos);
            if (nbsp_len > 0) {
                pos += nbsp_len;
            } else {
                break;
            }
        }
    }
}

/// Try to parse optional frequency prefix. Returns true if found.
/// On success, advances pos past the ':' and any trailing whitespace.
static bool try_parse_frequency(const char *str, size_t str_len, size_t &pos, uint16_t &hz_out) {
    // Look for ':' before the first whitespace to determine if frequency is present.
    size_t scan = pos;
    while (scan < str_len && !is_whitespace(str[scan])) {
        if (is_utf8_nbsp(str, str_len, scan)) {
            break;
        }
        if (str[scan] == ':') {
            // Found colon: everything from pos to scan is the frequency
            uint32_t value = 0;
            for (size_t i = pos; i < scan; i++) {
                if (str[i] < '0' || str[i] > '9') {
                    return false;  // Non-digit before colon
                }
                value = value * 10 + (str[i] - '0');
                if (value > UINT16_MAX) {
                    return false;
                }
            }
            if (scan == pos) {
                // Empty frequency (just ':' with no digits)
                return false;
            }
            hz_out = (uint16_t)value;
            pos = scan + 1;  // Skip past ':'
            skip_separators(str, str_len, pos);
            return true;
        }
        scan++;
    }
    return false;
}

/// Parse raw IR timing string into dynamically allocated uint16_t buffer.
/// Caller must free result.buf when done.
RawParseResult parse_raw_timings(const char *str, size_t str_len) {
    RawParseResult result = {nullptr, 0, kDefaultFrequencyHz, RawParseError::OK, 0};

    if (str == NULL || str_len == 0) {
        result.error = RawParseError::EMPTY_INPUT;
        return result;
    }

    size_t pos = 0;
    skip_separators(str, str_len, pos);

    // Try to parse optional frequency prefix
    uint16_t hz = kDefaultFrequencyHz;
    size_t   freq_start = pos;
    if (!try_parse_frequency(str, str_len, pos, hz)) {
        // Check if there was an invalid frequency attempt (colon present but bad value)
        size_t scan = freq_start;
        while (scan < str_len && !is_whitespace(str[scan])) {
            if (is_utf8_nbsp(str, str_len, scan)) {
                break;
            }
            if (str[scan] == ':') {
                // Colon was found but parsing failed
                result.error = RawParseError::INVALID_FREQUENCY;
                return result;
            }
            scan++;
        }
        // No colon found: no frequency prefix, use default
        pos = freq_start;
    }
    result.hz = hz;

    // First pass: count tokens
    uint16_t count = 0;
    size_t   scan_pos = pos;
    skip_separators(str, str_len, scan_pos);
    while (scan_pos < str_len) {
        count++;
        while (scan_pos < str_len && !is_whitespace(str[scan_pos])) {
            // Also check for multi-byte NBSP
            if (is_utf8_nbsp(str, str_len, scan_pos)) {
                break;
            }
            scan_pos++;
        }
        skip_separators(str, str_len, scan_pos);
    }

    if (count == 0) {
        result.error = RawParseError::EMPTY_INPUT;
        return result;
    }

    result.buf = (uint16_t *)malloc(count * sizeof(uint16_t));
    if (result.buf == NULL) {
        result.error = RawParseError::ALLOC_FAILED;
        return result;
    }

    // Second pass: parse values
    skip_separators(str, str_len, pos);
    uint16_t idx = 0;

    while (pos < str_len && idx < count) {
        // Skip optional sign
        if (str[pos] == '+' || str[pos] == '-') {
            pos++;
        }

        // Parse digits
        if (pos >= str_len || str[pos] < '0' || str[pos] > '9') {
            free(result.buf);
            result.buf = NULL;
            result.error = RawParseError::INVALID_TOKEN;
            result.error_token_index = idx;
            return result;
        }

        uint32_t value = 0;
        while (pos < str_len && str[pos] >= '0' && str[pos] <= '9') {
            value = value * 10 + (str[pos] - '0');
            if (value > UINT16_MAX) {
                free(result.buf);
                result.buf = NULL;
                result.error = RawParseError::VALUE_OUT_OF_RANGE;
                result.error_token_index = idx;
                return result;
            }
            pos++;
        }

        result.buf[idx] = (uint16_t)value;
        idx++;

        skip_separators(str, str_len, pos);
    }

    result.len = count;
    return result;
}
