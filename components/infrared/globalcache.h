// SPDX-FileCopyrightText: Copyright (c) 2023 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Common utility functions for Global Cache IR codes & TCP srever
// Make sure this file also compiles natively and all functions are covered by unit tests.

#pragma once

#include <stdint.h>

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

/// Convert raw microsecond timings to Global Caché sendir format string.
/// Handles overflow encoding where values > UINT16_MAX are split into
/// multiple entries with 0 as skip marker in the opposite position.
///
/// Parameters:
///   timings_us   - raw timing array (unsigned µs, alternating mark/space)
///   timings_len  - number of entries
///   frequency_hz - carrier frequency in Hz
///   connector    - connector address string, e.g. "1:1"
///   id           - sendir ID (0-65535)
///
/// If the number of logical pulses is odd, the last value is duplicated as
/// final OFF.
///
/// Returns heap-allocated string, ending with \r. Caller must free().
/// Returns NULL on invalid input.
char *raw_timings_to_gc_sendir(const uint16_t *timings_us, uint16_t timings_len, uint32_t frequency_hz,
                               const char *connector, uint16_t id);
