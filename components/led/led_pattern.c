// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "led_pattern.h"

#include "esp_log.h"

#include "sdkconfig.h"

/// @brief LED PATTERN Log tag.
const char* const LED = "LED";

/// @brief Private LED indicator handle
led_indicator_handle_t led_handle = NULL;

#ifdef CONFIG_LED_PATTERN_NONE
void init_led() {}
#endif

void led_pattern(led_pattern_t pattern) {
#ifdef CONFIG_LED_PATTERN_NONE
    if (true) {
        return;
    }
#endif
    ESP_LOGI(LED, "Starting LED pattern: %s", get_led_pattern_str(pattern));

    // stop all transitional looping patterns
    switch (pattern) {
        case LED_IMPROV_WAIT_AUTHORIZATION:
        case LED_IMPROV_WAIT_CREDENTIALS:
        case LED_IMPROV_PROVISIONING:
        case LED_IMPROV_PROVISIONED:
        case LED_IMPROV_STOPPED:
            led_indicator_stop(led_handle, LED_IMPROV_WAIT_AUTHORIZATION);
            led_indicator_stop(led_handle, LED_IMPROV_WAIT_CREDENTIALS);
            led_indicator_stop(led_handle, LED_IMPROV_PROVISIONING);
            led_indicator_stop(led_handle, LED_IMPROV_STOPPED);
            break;
        default:
            // ignore non-looping pattern
            break;
    }

    led_indicator_start(led_handle, pattern);
}

void led_pattern_stop(led_pattern_t pattern) {
#ifdef CONFIG_LED_PATTERN_NONE
    if (true) {
        return;
    }
#endif
    ESP_LOGI(LED, "Stopping LED pattern: %s", get_led_pattern_str(pattern));
    led_indicator_stop(led_handle, pattern);
}

void led_pattern_stop_all() {
#ifdef CONFIG_LED_PATTERN_NONE
    if (true) {
        return;
    }
#endif
    ESP_LOGI(LED, "Stopping all LED patterns");
    // stop all patterns, from lowest to highest prio
    for (uint8_t i = 0; i < LED_PATTERNS_MAX; i++) {
        led_indicator_stop(led_handle, LED_PATTERNS_MAX - i - 1);
    }
}

const char* get_led_pattern_str(led_pattern_t pattern) {
    switch (pattern) {
        case LED_IMPROV_STOPPED:
            return "IMPROV_STOPPED";
        case LED_IMPROV_FAILED:
            return "IMPROV_FAILED";
        case LED_IMPROV_PROVISIONED:
            return "IMPROV_PROVISIONED";
        case LED_IMPROV_PROVISIONING:
            return "IMPROV_PROVISIONING";
        case LED_IMPROV_IDENTIFY:
            return "IMPROV_IDENTIFY";
        case LED_IMPROV_WAIT_CREDENTIALS:
            return "IMPROV_WAIT_CREDENTIALS";
        case LED_IMPROV_WAIT_AUTHORIZATION:
            return "IMPROV_WAIT_AUTHORIZATION";
        default:
            return "UNKNOWN";
    }
}