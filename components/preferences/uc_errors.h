// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/// UC error codes intended to be displayed on the LCD.
typedef enum uc_errors {
    // TODO define static IDs. Once used they should not change, new errors must be appended at the end!
    UC_ERROR_NONE = 0,
    UC_ERROR_INIT_LCD,
    UC_ERROR_INIT_FS,
    UC_ERROR_INIT_NET,
    UC_ERROR_INIT_MDNS,
    UC_ERROR_INIT_BUTTON,
    UC_ERROR_INIT_CHARGER,
    UC_ERROR_INIT_WEBSRV,
    /// Generic port initialization error
    UC_ERROR_INIT_PORT,
    UC_ERROR_INIT_PORT1,
    UC_ERROR_INIT_PORT2,
    /// Generic port ADC initialization error
    UC_ERROR_INIT_PORT_ADC = UC_ERROR_INIT_PORT + 10,
    UC_ERROR_INIT_PORT1_ADC,
    UC_ERROR_INIT_PORT2_ADC,
    UC_ERROR_IR_LEARN_UNKNOWN = 100,
    UC_ERROR_IR_LEARN_INVALID,
    UC_ERROR_IR_LEARN_OVERFLOW
} uc_errors_t;
