// SPDX-FileCopyrightText: Copyright (c) 2025 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Revision 6 specific configuration.
// Production board.

#pragma once

#define UCD_HW_MODEL_NAME "UCD3"
#define UCD_HW_REVISION_NAME "6"

// chipselect for KSZ8851SNL ethernet ic, LOW -> enable communication to KSZ8851SNL
#define SPI_CS GPIO_NUM_14
// OUTPUT, PoE voltage switch (rev6+)
#define POE_SWITCH GPIO_NUM_7

// INPUT, measures low side via 0.1 Ohm current to remote
#define CHARGING_CURRENT GPIO_NUM_6
#define CHARGING_CURRENT_ADC_UNIT ADC_UNIT_1
#define CHARGING_CURRENT_ADC_CH ADC_CHANNEL_5

// tied to IR side output on rev6+
#define IR_SEND_PIN_INT_TOP IR_SEND_PIN_INT_SIDE

// Inverted output or not for SWITCH_EXT_1 & SWITCH_EXT_2
#define SWITCH_EXT_INVERTED 0
// GPIO output mode for SWITCH_EXT_1 & SWITCH_EXT_2: open drain or floating
#define SWITCH_EXT_GPIO_MODE GPIO_MODE_OUTPUT
