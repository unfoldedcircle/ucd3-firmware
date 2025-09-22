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
void init_led(uint32_t) {}
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

    ESP_ERROR_CHECK_WITHOUT_ABORT(led_indicator_start(led_handle, pattern));
}

void led_pattern_stop(led_pattern_t pattern) {
#ifdef CONFIG_LED_PATTERN_NONE
    if (true) {
        return;
    }
#endif
    ESP_LOGI(LED, "Stopping LED pattern: %s", get_led_pattern_str(pattern));
    ESP_ERROR_CHECK_WITHOUT_ABORT(led_indicator_stop(led_handle, pattern));
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

void set_led_brightness(uint32_t brightness) {
    // I do not understand the brightness settings function.
    // It just doesn't work correctly for LED strips!
    // Switches back to a white LED and patterns don't use the new brightness!
    // --> see patched up code to at least make it work for basic blink patterns!
    ESP_ERROR_CHECK_WITHOUT_ABORT(led_indicator_set_brightness(led_handle, brightness));
    // HACK to turn off white LED if no pattern was active
    led_pattern(LED_IDLE);
}

const char* get_led_pattern_str(led_pattern_t pattern) {
    switch (pattern) {
        case LED_OTA:
            return "OTA";
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
        case LED_SETUP:
            return "SETUP";
        case LED_IR_LEARN_ON:
            return "IR_LEARN_ON";
        case LED_IR_LEARN_OK:
            return "IR_LEARN_OK";
        case LED_IR_LEARN_FAILED:
            return "IR_LEARN_FAILED";
        case LED_IDLE:
            return "IDLE";
        default:
            return "UNKNOWN";
    }
}