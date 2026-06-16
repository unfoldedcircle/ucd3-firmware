// SPDX-FileCopyrightText: Copyright (c) 2026 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>

enum class RawParseError {
    OK,
    EMPTY_INPUT,
    INVALID_TOKEN,
    VALUE_OUT_OF_RANGE,
    INVALID_FREQUENCY,
    ALLOC_FAILED,
};

struct RawParseResult {
    uint16_t     *buf;
    uint16_t      len;
    uint16_t      hz;
    RawParseError error;
    uint16_t      error_token_index;  // Which token caused the error (0-based)
};

RawParseResult parse_raw_timings(const char *str, size_t str_len);
