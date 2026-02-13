// SPDX-FileCopyrightText: Copyright (c) 2025 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Revision 3 specific configuration.
// Pre-production board, only used for internal testing.

#pragma once

#define UCD_HW_MODEL_NAME "UCD3"
#define UCD_HW_REVISION_NAME "3"

// chipselect for W5500 ethernet ic, LOW -> enable communication to w5500
#define SPI_CS GPIO_NUM_6

// INPUT, measures low side via 0.1 Ohm current to remote
#define CHARGING_CURRENT GPIO_NUM_14
#define CHARGING_CURRENT_ADC_UNIT ADC_UNIT_2
#define CHARGING_CURRENT_ADC_CH ADC_CHANNEL_3

// OUTPUT,OPEN DRAIN. physically pulled up to 2.4V
#define IR_SEND_PIN_INT_TOP GPIO_NUM_7

// Inverted output or not for SWITCH_EXT_1 & SWITCH_EXT_2
#define SWITCH_EXT_INVERTED 1
// GPIO output mode for SWITCH_EXT_1 & SWITCH_EXT_2: open drain or floating
#define SWITCH_EXT_GPIO_MODE GPIO_MODE_OUTPUT_OD
