// SPDX-FileCopyrightText: Copyright (c) 2023 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Common utility functions for Global Cache IR codes & TCP srever
// Make sure this file also compiles natively and all functions are covered by unit tests.

#pragma once

#include <stdint.h>

#include <algorithm>
#include <cstring>

#include "util_types.h"

/// @brief Parse a GlobalCache request message
/// @param request request message string **without** terminating line feed.
/// @param msg the GCMsg struct to store the parsed request.
/// @return 0 if successful, iTach error code otherwise.
/// @details Tested with the following iTack request messages:
/// - getversion[,module]
/// - getdevices
/// - blink,<mode>
/// - get_IR,<module>:<port>
/// - set_IR,<module>:<port>,<mode>
/// - sendir,<module>:<port>, <ID>,<freq>,<repeat>,<offset>, <on1>,<off1>,<on2>,<off2>,...,<onN>,<offN>
/// - stopir,<module>:<port>
/// - get_IRL
/// - stop_IRL
uint8_t parseGcRequest(const char *request, GCMsg *msg);