// SPDX-FileCopyrightText: Copyright (c) 2025 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief External port operation modes
typedef enum ExtPortMode {
    AUTO = 0,
    NOT_CONFIGURED = 1,
    IR_BLASTER = 2,
    IR_EMITTER_MONO_PLUG = 3,
    IR_EMITTER_STEREO_PLUG = 4,
    TRIGGER_5V = 5,
    RS232 = 6,
    PORT_MODE_MAX
} ext_port_mode_t;

/// @brief Get a string representation, e.g. for JSON serialization.
/// @param mode the external port mode.
const char *ExtPortMode_to_str(enum ExtPortMode mode);

/// @brief Convert a string representation to an ExtPortMode enum.
/// @param mode the external port mode string.
/// @return the enum representation or PORT_MODE_MAX in case of an error.
enum ExtPortMode ExtPortMode_from_str(const char *mode);

/// @brief Get a friendly name of the port mode enum.
/// @param mode the external port mode.
const char *ExtPortMode_to_friendly_str(enum ExtPortMode mode);

#ifdef __cplusplus
}
#endif
