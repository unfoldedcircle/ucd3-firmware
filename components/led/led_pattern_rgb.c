// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "esp_log.h"

#include "led_indicator_rgb.h"
#include "led_pattern.h"
#include "sdkconfig.h"

#ifdef CONFIG_LED_PATTERN_RGB

extern led_indicator_handle_t led_handle;
extern const char *const      LED;

/**
 * @brief Software update in progress: breathing red
 */
static const blink_step_t ota[] = {
    {LED_BLINK_RGB, SET_RGB(0xFF, 0, 0), 0},    {LED_BLINK_BREATHE, LED_STATE_OFF, 1000},
    {LED_BLINK_BRIGHTNESS, LED_STATE_OFF, 500}, {LED_BLINK_BREATHE, LED_STATE_ON, 1000},
    {LED_BLINK_BRIGHTNESS, LED_STATE_ON, 500},  {LED_BLINK_LOOP, 0, 0},
};

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

/**
 * @brief amber blinking: device requires setup
 */
static const blink_step_t setup[] = {
    {LED_BLINK_RGB, SET_RGB(255, 170, 0), 1000},
    {LED_BLINK_RGB, LED_STATE_OFF, 1000},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief solid green: IR learning is active.
 */
static const blink_step_t ir_learn_on[] = {
    // use a short delay to quickly show learned ok / failed pattern
    {LED_BLINK_RGB, SET_RGB(0x00, 0xFF, 0x00), 100},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief green blinking twice: IR command learned successfully
 */
static const blink_step_t ir_learn_ok[] = {
    {LED_BLINK_RGB, SET_RGB(0x00, 0xFF, 0x00), 100},
    {LED_BLINK_RGB, SET_RGB(0, 0, 0), 100},
    {LED_BLINK_RGB, SET_RGB(0x00, 0xFF, 0x00), 100},
    {LED_BLINK_RGB, SET_RGB(0, 0, 0), 100},
    {LED_BLINK_STOP, 0, 0},
};

/**
 * @brief red blinking twice: IR command learning failed
 */
static const blink_step_t ir_learn_failed[] = {
    {LED_BLINK_RGB, SET_RGB(0xFF, 0, 0), 100},
    {LED_BLINK_RGB, SET_RGB(0, 0, 0), 100},
    {LED_BLINK_RGB, SET_RGB(0xFF, 0, 0), 100},
    {LED_BLINK_RGB, SET_RGB(0, 0, 0), 100},
    {LED_BLINK_STOP, 0, 0},
};

/**
 * @brief pseudo pattern to turn off LED
 */
static const blink_step_t idle[] = {
    {LED_BLINK_RGB, SET_RGB(0, 0, 0), 0},
    {LED_BLINK_STOP, 0, 0},
};

static blink_step_t const *led_mode[] = {
    [LED_OTA] = ota,
    [LED_IMPROV_FAILED] = improv_failed,
    [LED_IMPROV_STOPPED] = improv_stopped,
    [LED_IMPROV_PROVISIONED] = improv_provisioned,
    [LED_IMPROV_PROVISIONING] = improv_provisioning,
    [LED_IMPROV_IDENTIFY] = improv_identify,
    [LED_IMPROV_WAIT_CREDENTIALS] = improv_wait_credentials,
    [LED_IMPROV_WAIT_AUTHORIZATION] = improv_wait_authorization,
    [LED_SETUP] = setup,
    [LED_IR_LEARN_FAILED] = ir_learn_failed,
    [LED_IR_LEARN_OK] = ir_learn_ok,
    [LED_IR_LEARN_ON] = ir_learn_on,
    [LED_IDLE] = idle,
    [LED_PATTERNS_MAX] = NULL,
};

void init_led(uint32_t brightness) {
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
        .blink_lists = led_mode,
        .blink_list_num = LED_PATTERNS_MAX,
    };

    ESP_ERROR_CHECK_WITHOUT_ABORT(led_indicator_new_rgb_device(&config, &led_grb_cfg, &led_handle));

    if (led_handle) {
        ESP_LOGI(LED, "Created RGB LED");
        set_led_brightness(brightness);
    }
}
#endif  // CONFIG_LED_PATTERN_RGB
