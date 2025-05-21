// SPDX-FileCopyrightText: Copyright (c) 2025 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Revision 3 specific configuration.
// Pre-production board, only used for internal testing.

#pragma once

#define UCD_HW_MODEL_NAME "UCD3"
#define UCD_HW_REVISION_NAME "3"

// Inverted output or not for SWITCH_EXT_1 & SWITCH_EXT_2
#define SWITCH_EXT_INVERTED 1
// GPIO output mode for SWITCH_EXT_1 & SWITCH_EXT_2: open drain or floating
#define SWITCH_EXT_GPIO_MODE GPIO_MODE_OUTPUT_OD
