// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "esp_log.h"

#include "led_pattern.h"
#include "sdkconfig.h"

#ifdef CONFIG_LED_PATTERN_STRIP

// extern blink_step_t const *led_mode[];
extern led_indicator_handle_t led_handle;
extern const char *const      LED;

/**
 * @brief LED off: The improv service is stopped
 */
static const blink_step_t improv_stopped[] = {
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0), 1000},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief slow red blinking for 3 times: The improv service failed to provision the received credentials.
 */
static const blink_step_t improv_failed[] = {
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0xFF, 0, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0xFF, 0, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0xFF, 0, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0), 1000},
    {LED_BLINK_STOP, 0, 0},
};

/**
 * @brief blinking blue 3 times per second: Credentials are being verified and saved to the device.
 */
static const blink_step_t improv_provisioning[] = {
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0xFF), 166},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0), 166},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief blinking 3 times per second with a break in between for 3 seconds: The identify command has been used by the
 * client.
 */
static const blink_step_t improv_identify[] = {
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0xFF, 0, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0xFF, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0xFF), 166},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0xFF, 0, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0xFF, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0xFF), 166},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0xFF, 0, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0xFF, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0xFF), 166},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0), 1000},
    {LED_BLINK_STOP, 0, 0},
};

/**
 * @brief blinking blue once per second: The improv service is awaiting credentials.
 */
static const blink_step_t improv_wait_credentials[] = {
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0xFF), 200},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0), 800},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief solid white: The improv service is active and waiting to be authorized.
 */
static const blink_step_t improv_wait_authorization[] = {
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0xFF, 0xFF, 0xFF), 1000},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief green blink twice: provision done
 */
static const blink_step_t improv_provisioned[] = {
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0xFF, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0xFF, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(MAX_INDEX, 0, 0, 0), 1000},
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

void init_led() {
    ESP_LOGI(LED, "Creating LED strip object with RMT backend");

    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_LED_PATTERN_STRIP_GPIO,  // The GPIO that connected to the LED strip's data line
        .max_leds = CONFIG_LED_PATTERN_STRIP_NUMBER,      // The number of LEDs in the strip,
        .led_pixel_format = CONFIG_LED_PATTERN_STRIP_PIXEL_FORMAT,  // Pixel format of your LED strip
        .led_model = LED_MODEL_WS2812,                              // LED strip model
        .flags.invert_out =                                         // whether to invert the output signal
#ifdef CONFIG_LED_PATTERN_STRIP_INVERT
        true
#else
        false
#endif
    };

#ifdef CONFIG_LED_PATTERN_STRIP_RMT
    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config = {
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
        .rmt_channel = 0,
#else
        .clk_src = RMT_CLK_SRC_DEFAULT,  // different clock source can lead to different power consumption
        .resolution_hz = CONFIG_LED_PATTERN_STRIP_RESOLUTION,  // RMT counter clock frequency
        .flags.with_dma =                                      // DMA feature is available on ESP target like ESP32-S3
#ifdef CONFIG_LED_PATTERN_STRIP_DMA
        true
#else
        false
#endif
#endif
    };

    led_indicator_strips_config_t strips_config = {
        .led_strip_cfg = strip_config,
        .led_strip_driver = LED_STRIP_RMT,
        .led_strip_rmt_cfg = rmt_config,
    };

#else  // #ifdef CONFIG_LED_PATTERN_STRIP_RMT

    // LED strip backend configuration: SPI
    led_strip_spi_config_t spi_config = {
        .clk_src = SPI_CLK_SRC_DEFAULT,  // different clock source can lead to different power consumption
        .spi_bus = SPI3_HOST,            // other buses are already used
        .flags.with_dma =                // only a single LED, should not be required
#ifdef CONFIG_LED_PATTERN_STRIP_DMA
        true
#else
        false
#endif
    };

    led_indicator_strips_config_t strips_config = {
        .led_strip_cfg = strip_config,
        .led_strip_driver = LED_STRIP_SPI,
        .led_strip_spi_cfg = spi_config,
    };
#endif  // CONFIG_LED_PATTERN_STRIP_RMT

    const led_indicator_config_t config = {
        .mode = LED_STRIPS_MODE,
        .led_indicator_strips_config = &strips_config,
        .blink_lists = led_mode,
        .blink_list_num = LED_PATTERNS_MAX,
    };

    led_handle = led_indicator_create(&config);
    if (led_handle) {
        ESP_LOGI(LED, "Created LED strip object");
    }
}
#endif  // CONFIG_LED_PATTERN_STRIP
