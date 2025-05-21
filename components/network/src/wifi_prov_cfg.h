// SPDX-FileCopyrightText: Copyright (c) 2025 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "improv.h"

/// @HACK There's likely a very simple solution to this common problem...

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Workaround / bridge function to use the C++ Config class from C code.
/// @param cmd improv-wifi command containing device settings from wifi provisioning.
void set_device_parameters(struct ImprovCommand cmd);

/// @brief Get serial from device
const char* cfg_get_serial();
/// @brief Get model number from device
const char* cfg_get_model();
/// @brief Get device hardware revision
const char* cfg_get_revision();

#ifdef __cplusplus
}
#endif
