// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * Monochrome blinking patterns for GPIO and PWM LED drivers.
 */
#include "esp_log.h"

#include "led_pattern.h"
#include "sdkconfig.h"

#if defined                   CONFIG_LED_PATTERN_GPIO || defined CONFIG_LED_PATTERN_PWN
extern led_indicator_handle_t led_handle;
extern const char *const      LED;

/**
 * @brief LED off: The improv service is stopped
 */
static const blink_step_t improv_stopped[] = {
    {LED_BLINK_HOLD, LED_STATE_OFF, 1000},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief slow blinking for 3 times: The improv service failed to provision the received credentials.
 */
static const blink_step_t improv_failed[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 1000},
    {LED_BLINK_HOLD, LED_STATE_OFF, 1000},
    {LED_BLINK_HOLD, LED_STATE_ON, 1000},
    {LED_BLINK_HOLD, LED_STATE_OFF, 1000},
    {LED_BLINK_HOLD, LED_STATE_ON, 1000},
    {LED_BLINK_HOLD, LED_STATE_OFF, 1000},
    {LED_BLINK_STOP, 0, 0},
};

/**
 * @brief blinking 5 times per second: Credentials are being verified and saved to the device.
 */
static const blink_step_t improv_provisioning[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 100},
    {LED_BLINK_HOLD, LED_STATE_OFF, 100},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief blinking 3 times per second with a break in between for 3 seconds: The identify command has been used by the
 * client.
 */
static const blink_step_t improv_identify[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 166},
    {LED_BLINK_HOLD, LED_STATE_OFF, 166},
    {LED_BLINK_HOLD, LED_STATE_ON, 166},
    {LED_BLINK_HOLD, LED_STATE_OFF, 166},
    {LED_BLINK_HOLD, LED_STATE_ON, 166},
    {LED_BLINK_HOLD, LED_STATE_OFF, 1000},
    {LED_BLINK_HOLD, LED_STATE_ON, 166},
    {LED_BLINK_HOLD, LED_STATE_OFF, 166},
    {LED_BLINK_HOLD, LED_STATE_ON, 166},
    {LED_BLINK_HOLD, LED_STATE_OFF, 166},
    {LED_BLINK_HOLD, LED_STATE_ON, 166},
    {LED_BLINK_HOLD, LED_STATE_OFF, 1000},
    {LED_BLINK_HOLD, LED_STATE_ON, 166},
    {LED_BLINK_HOLD, LED_STATE_OFF, 166},
    {LED_BLINK_HOLD, LED_STATE_ON, 166},
    {LED_BLINK_HOLD, LED_STATE_OFF, 166},
    {LED_BLINK_HOLD, LED_STATE_ON, 166},
    {LED_BLINK_HOLD, LED_STATE_OFF, 1000},
    {LED_BLINK_STOP, 0, 0},
};

/**
 * @brief blinking once per second: The improv service is awaiting credentials.
 */
static const blink_step_t improv_wait_credentials[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 200},
    {LED_BLINK_HOLD, LED_STATE_OFF, 800},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief solid: The improv service is active and waiting to be authorized.
 */
static const blink_step_t improv_wait_authorization[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 1000},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief provision done
 */
static const blink_step_t improv_provisioned[] = {
    {LED_BLINK_HOLD, LED_STATE_OFF, 1000},
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
#endif

#ifdef CONFIG_LED_PATTERN_GPIO
void init_led() {
    led_indicator_gpio_config_t gpio_config = {
        .gpio_num = CONFIG_LED_PATTERN_GPIO_NUM,
        .is_active_level_high = CONFIG_LED_PATTERN_ACTIVE_LEVEL,
    };

    const led_indicator_config_t config = {
        .mode = LED_GPIO_MODE,
        .led_indicator_gpio_config = &gpio_config,
        .blink_lists = led_mode,
        .blink_list_num = LED_PATTERNS_MAX,
    };

    led_handle = led_indicator_create(&config);
    if (led_handle) {
        ESP_LOGI(LED, "Created GPIO LED");
    }
}
#endif  // CONFIG_LED_PATTERN_GPIO

#ifdef CONFIG_LED_PATTERN_PWN
void init_led(led_pattern_t pattern) {
    led_indicator_ledc_config_t ledc_config = {
        .is_active_level_high = CONFIG_LED_PATTERN_PWN_ACTIVE_LEVEL,
        .timer_inited = false,
        .timer_num = LEDC_TIMER_0,
        .gpio_num = CONFIG_LED_PATTERN_PWN_GPIO,
        .channel = CONFIG_LED_PATTERN_PWN_CHANNEL,
    };

    const led_indicator_config_t config = {
        .mode = LED_LEDC_MODE,
        .led_indicator_ledc_config = &ledc_config,
        .blink_lists = led_mode,
        .blink_list_num = LED_PATTERNS_MAX,
    };

    led_handle = led_indicator_create(&config);
    if (led_handle) {
        ESP_LOGI(LED, "Created PWN LED");
    }
}
#endif  // CONFIG_LED_PATTERN_PWN
