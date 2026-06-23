// SPDX-FileCopyrightText: Copyright (c) 2026 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "driver/gpio.h"
#include "hal/gpio_types.h"

/// @brief Initialize a GPIO pin with the specified mode and pull-up/pull-down configuration.
/// @param gpio_num GPIO number to initialize
/// @param mode GPIO mode to set
/// @param pullup Pull-up resistor configuration. Enabeled by default.
/// @param pulldown Pull-down resistor configuration. Disabled by default.
void gpio_init(gpio_num_t gpio_num, gpio_mode_t mode, gpio_pullup_t pullup = GPIO_PULLUP_ENABLE,
               gpio_pulldown_t pulldown = GPIO_PULLDOWN_DISABLE);