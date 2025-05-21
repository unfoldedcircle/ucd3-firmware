// SPDX-FileCopyrightText: Copyright (c) 2023 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Common utility functions for IR codes like conversions.
// Make sure this file also compiles natively and all functions are covered by unit tests.

#pragma once

#include <stdint.h>
#include <stdlib.h>

#include <string>

#include "IRremoteESP8266.h"
#include "string.h"

enum class IRFormat {
    UNKNOWN = 0,
    UNFOLDED_CIRCLE = 1,
    PRONTO = 2,
    GLOBAL_CACHE = 3,
};

struct GpioPinMask {
    // IR signal enable mask: GPIOs to set before sending an IR signal
    uint64_t w1ts_enable;
    // IR signal output mask: GPIOs to set
    uint64_t w1ts;
    // IR signal output mask: GPIOs to clear
    uint64_t w1tc;
};

struct IRSendMessage {
    int16_t     clientId;
    uint32_t    msgId;
    IRFormat    format;
    std::string message;
    uint16_t    repeat;
    GpioPinMask pin_mask;
    // TCP socket of message if received from the GlobalCache server, 0 otherwise.
    int gcSocket;
};

struct IRHexData {
    decode_type_t protocol;
    uint64_t      command;
    uint16_t      bits;
    uint16_t      repeat;
};

uint32_t parseUint32(const char *number, int *error = NULL, int base = 10);

bool buildIRHexData(const std::string &message, IRHexData *data);

uint16_t countValuesInCStr(const char *str, char sep);

uint16_t *prontoBufferToArray(const char *msg, char separator, uint16_t *codeCount, int *memError = NULL);

uint16_t *globalCacheBufferToArray(const char *msg, uint16_t *codeCount, int *memError = NULL);
