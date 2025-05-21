// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "esp_err.h"

/// @brief ADC read interface
class AdcReader {
 public:
    virtual esp_err_t read(int *voltage) = 0;
};
