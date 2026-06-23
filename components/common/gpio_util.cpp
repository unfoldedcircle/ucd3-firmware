// SPDX-FileCopyrightText: Copyright (c) 2026 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpio_util.h"

void gpio_init(gpio_num_t gpio_num, gpio_mode_t mode, gpio_pullup_t pullup, gpio_pulldown_t pulldown) {
    assert(GPIO_IS_VALID_GPIO(gpio_num));
    gpio_config_t cfg = {
        .pin_bit_mask = BIT64(gpio_num),
        .mode = mode,
        // for powersave reasons, the GPIO should not be floating, select pullup
        .pull_up_en = pullup,
        .pull_down_en = pulldown,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}
