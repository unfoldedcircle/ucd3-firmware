// SPDX-FileCopyrightText: Copyright (c) 2023 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

/// GlobalCache request message
struct GCMsg {
    // command name
    char command[17];
    // optional module
    uint8_t module;
    // optional port
    uint8_t port;
    // optional parameter(s). Points to first parameter or NULL if not present.
    const char *param;
};
