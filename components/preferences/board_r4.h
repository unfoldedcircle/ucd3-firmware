// SPDX-FileCopyrightText: Copyright (c) 2025 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Revision 4 specific configuration.
// Production board.

#pragma once

#define UCD_HW_MODEL_NAME "UCD3"
#define UCD_HW_REVISION_NAME "4"

// Inverted output or not for SWITCH_EXT_1 & SWITCH_EXT_2
#define SWITCH_EXT_INVERTED 0
// GPIO output mode for SWITCH_EXT_1 & SWITCH_EXT_2: open drain or floating
#define SWITCH_EXT_GPIO_MODE GPIO_MODE_OUTPUT
