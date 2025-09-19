// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * LED identification patterns if IMPROV_WIFI_CAPABILITY_IDENTIFY is enabled.
 * Uses the espressif/led_indicator component with the configured driver.
 */
#pragma once

#include "led_indicator.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Public enum of all availale LED patterns, from highest to lowest priority.
typedef enum {
    /// @brief Software update is in progress.
    /// Breathing red.
    LED_OTA,
    /// @brief WiFi connection failed.
    LED_IMPROV_FAILED,
    /// @brief The improv service is stopped. Notification LED is off.
    LED_IMPROV_STOPPED,
    /// @brief The improv service has successfully provisioned the WiFi credentials.
    LED_IMPROV_PROVISIONED,
    /// @brief Looping pattern: credentials are being verified and saved to the device.
    LED_IMPROV_PROVISIONING,
    /// @brief The identify command has been used by the client.
    /// Looping pattern until LED_IMPROV_PROVISIONED or LED_IMPROV_WAIT_CREDENTIALS is activated.
    LED_IMPROV_IDENTIFY,
    /// @brief Looping pattern: the improv service is awaiting credentials.
    /// Looping pattern until LED_IMPROV_PROVISIONING is activated.
    LED_IMPROV_WAIT_CREDENTIALS,
    /// @brief The improv service is active and waiting to be authorized.
    /// Looping pattern until LED_IMPROV_WAIT_CREDENTIALS is activated.
    LED_IMPROV_WAIT_AUTHORIZATION,
    /// @brief Dock is in setup mode. Ethernet not connected and no WiFi credentials.
    /// Blinking amber pattern.
    LED_SETUP,
    /// @brief IR learning failed.
    /// Double red blink.
    LED_IR_LEARN_FAILED,
    /// @brief Successfully learned an IR code.
    /// Double green blink.
    LED_IR_LEARN_OK,
    /// @brief IR learning is active.
    /// Solid green
    LED_IR_LEARN_ON,
    LED_IDLE,
    LED_PATTERNS_MAX,
} led_pattern_t;

/// @brief Return a string representation of led_pattern_t value.
const char* get_led_pattern_str(led_pattern_t pattern);

/// @brief Initialize the LED indicator component.
void init_led(uint32_t brightness);

/// @brief Start the given LED pattern.
/// @param pattern LED pattern to start.
void led_pattern(led_pattern_t pattern);

/// @brief Stop the given LED pattern.
/// @param pattern LED pattern to stop.
void led_pattern_stop(led_pattern_t pattern);

/// @brief Stop all running LED patterns.
void led_pattern_stop_all();

void set_led_brightness(uint32_t brightness);

#ifdef __cplusplus
}
#endif