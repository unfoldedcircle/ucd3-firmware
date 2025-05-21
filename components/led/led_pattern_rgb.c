// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "esp_log.h"

#include "led_pattern.h"
#include "sdkconfig.h"

#ifdef CONFIG_LED_PATTERN_RGB

extern led_indicator_handle_t led_handle;
extern const char *const      LED;

/**
 * @brief LED off: The improv service is stopped
 */
static const blink_step_t improv_stopped[] = {
    {LED_BLINK_RGB, SET_RGB(0, 0, 0), 1000},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief slow red blinking for 3 times: The improv service failed to provision the received credentials.
 */
static const blink_step_t improv_failed[] = {
    {LED_BLINK_RGB, SET_RGB(255, 0, 0), 1000},
    {LED_BLINK_RGB, LED_STATE_OFF, 1000},
    {LED_BLINK_RGB, SET_RGB(255, 0, 0), 1000},
    {LED_BLINK_RGB, LED_STATE_OFF, 1000},
    {LED_BLINK_RGB, SET_RGB(255, 0, 0), 1000},
    {LED_BLINK_RGB, LED_STATE_OFF, 1000},
    {LED_BLINK_STOP, 0, 0},
};

/**
 * @brief blinking blue 3 times per second: Credentials are being verified and saved to the device.
 */
static const blink_step_t improv_provisioning[] = {
    {LED_BLINK_RGB, SET_RGB(0, 0, 255), 166},
    {LED_BLINK_RGB, LED_STATE_OFF, 166},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief blinking 3 times per second with a break in between for 3 seconds: The identify command has been used by the
 * client.
 */
static const blink_step_t improv_identify[] = {
    {LED_BLINK_RGB, SET_RGB(255, 0, 0), 166},
    {LED_BLINK_RGB, LED_STATE_OFF, 166},
    {LED_BLINK_RGB, SET_RGB(0, 255, 0), 166},
    {LED_BLINK_RGB, LED_STATE_OFF, 166},
    {LED_BLINK_RGB, SET_RGB(0, 0, 255), 166},
    {LED_BLINK_RGB, LED_STATE_OFF, 1000},
    {LED_BLINK_RGB, SET_RGB(255, 0, 0), 166},
    {LED_BLINK_RGB, LED_STATE_OFF, 166},
    {LED_BLINK_RGB, SET_RGB(0, 255, 0), 166},
    {LED_BLINK_RGB, LED_STATE_OFF, 166},
    {LED_BLINK_RGB, SET_RGB(0, 0, 255), 166},
    {LED_BLINK_RGB, LED_STATE_OFF, 1000},
    {LED_BLINK_RGB, SET_RGB(255, 0, 0), 166},
    {LED_BLINK_RGB, LED_STATE_OFF, 166},
    {LED_BLINK_RGB, SET_RGB(0, 255, 0), 166},
    {LED_BLINK_RGB, LED_STATE_OFF, 166},
    {LED_BLINK_RGB, SET_RGB(0, 0, 255), 166},
    {LED_BLINK_RGB, LED_STATE_OFF, 1000},
    {LED_BLINK_STOP, 0, 0},
};

/**
 * @brief blinking blue once per second: The improv service is awaiting credentials.
 */
static const blink_step_t improv_wait_credentials[] = {
    {LED_BLINK_RGB, SET_RGB(0, 0, 255), 200},
    {LED_BLINK_RGB, LED_STATE_OFF, 800},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief solid white: The improv service is active and waiting to be authorized.
 */
static const blink_step_t improv_wait_authorization[] = {
    {LED_BLINK_RGB, SET_RGB(255, 255, 255), 1000},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief green blink twice: provision done
 */
static const blink_step_t improv_provisioned[] = {
    {LED_BLINK_RGB, SET_RGB(0, 255, 0), 1000},
    {LED_BLINK_RGB, LED_STATE_OFF, 1000},
    {LED_BLINK_RGB, SET_RGB(0, 255, 0), 1000},
    {LED_BLINK_RGB, LED_STATE_OFF, 1000},
    {LED_BLINK_STOP, 0, 0},
};

static blink_step_t const *led_mode[] = {
    [LED_IMPROV_FAILED] = improv_failed,
    [LED_IMPROV_STOPPED] = improv_stopped,
    [LED_IMPROV_PROVISIONED] = improv_provisioned,
    [LED_IMPROV_PROVISIONING] = improv_provisioning,
    [LED_IMPROV_IDENTIFY] = improv_identify,
    [LED_IMPROV_WAIT_CREDENTIALS] = improv_wait_credentials,
    [LED_IMPROV_WAIT_AUTHORIZATION] = improv_wait_authorization,
    [LED_PATTERNS_MAX] = NULL,
};

void init_led(led_pattern_t pattern) {
    led_indicator_rgb_config_t led_rgb_config = {
        .is_active_level_high =
#ifdef CONFIG_LED_PATTERN_RGB_ACTIVE_LEVEL
            true,
#else
            false,
#endif
        .timer_inited = false,
        .timer_num = LEDC_TIMER_0,
        .red_gpio_num = CONFIG_LED_PATTERN_RED_GPIO,
        .green_gpio_num = CONFIG_LED_PATTERN_GREEN_GPIO,
        .blue_gpio_num = CONFIG_LED_PATTERN_BLUE_GPIO,
        .red_channel = CONFIG_LED_PATTERN_RED_CHANNEL,
        .green_channel = CONFIG_LED_PATTERN_GREEN_CHANNEL,
        .blue_channel = CONFIG_LED_PATTERN_BLUE_CHANNEL,
    };

    const led_indicator_config_t config = {
        .mode = LED_RGB_MODE,
        .led_indicator_rgb_config = &led_rgb_config,
        .blink_lists = led_mode,
        .blink_list_num = LED_PATTERNS_MAX,
    };

    led_handle = led_indicator_create(&config);
    if (led_handle) {
        ESP_LOGI(LED, "Created RGB LED");
    }
}
#endif  // CONFIG_LED_PATTERN_RGB
