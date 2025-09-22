// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "esp_log.h"

#include "led_indicator_strips.h"
#include "led_pattern.h"
#include "sdkconfig.h"

#ifdef CONFIG_LED_PATTERN_STRIP

// extern blink_step_t const *led_mode[];
extern led_indicator_handle_t led_handle;
extern const char *const      LED;

#define STATUS_LED 0

/**
 * @brief Software update in progress: breathing red
 */
static const blink_step_t ota[] = {
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0xFF, 0, 0), 0},
    {LED_BLINK_BREATHE, INSERT_INDEX(STATUS_LED, LED_STATE_OFF), 1000},
    {LED_BLINK_BRIGHTNESS, INSERT_INDEX(STATUS_LED, LED_STATE_OFF), 500},
    {LED_BLINK_BREATHE, INSERT_INDEX(STATUS_LED, LED_STATE_ON), 1000},
    {LED_BLINK_BRIGHTNESS, INSERT_INDEX(STATUS_LED, LED_STATE_ON), 500},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief LED off: The improv service is stopped
 */
static const blink_step_t improv_stopped[] = {
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 1000},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief slow red blinking for 3 times: The improv service failed to provision the received credentials.
 */
static const blink_step_t improv_failed[] = {
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0xFF, 0, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0xFF, 0, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0xFF, 0, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 1000},
    {LED_BLINK_STOP, 0, 0},
};

/**
 * @brief blinking blue 3 times per second: Credentials are being verified and saved to the device.
 */
static const blink_step_t improv_provisioning[] = {
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0xFF), 166},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 166},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief blinking 3 times per second with a break in between for 3 seconds: The identify command has been used by the
 * client.
 */
static const blink_step_t improv_identify[] = {
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0xFF, 0, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0xFF, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0xFF), 166},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0xFF, 0, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0xFF, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0xFF), 166},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0xFF, 0, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0xFF, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 166},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0xFF), 166},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 1000},
    {LED_BLINK_STOP, 0, 0},
};

/**
 * @brief blinking blue once per second: The improv service is awaiting credentials.
 */
static const blink_step_t improv_wait_credentials[] = {
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0xFF), 200},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 800},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief solid white: The improv service is active and waiting to be authorized.
 */
static const blink_step_t improv_wait_authorization[] = {
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0xFF, 0xFF, 0xFF), 1000},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief green blink twice: provision done
 */
static const blink_step_t improv_provisioned[] = {
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0xFF, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0xFF, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 1000},
    {LED_BLINK_STOP, 0, 0},
};

/**
 * @brief amber blinking: device requires setup
 */
static const blink_step_t setup[] = {
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 255, 170, 0), 1000},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 1000},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief solid green: IR learning is active.
 */
static const blink_step_t ir_learn_on[] = {
    // use a short delay to quickly show learned ok / failed pattern
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0x00, 0xFF, 0x00), 100},
    {LED_BLINK_LOOP, 0, 0},
};

/**
 * @brief green blinking twice: IR command learned successfully
 */
static const blink_step_t ir_learn_ok[] = {
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0x00, 0xFF, 0x00), 100},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 100},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0x00, 0xFF, 0x00), 100},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 100},
    {LED_BLINK_STOP, 0, 0},
};

/**
 * @brief red blinking twice: IR command learning failed
 */
static const blink_step_t ir_learn_failed[] = {
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0xFF, 0, 0), 100},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 100},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0xFF, 0, 0), 100},
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 100},
    {LED_BLINK_STOP, 0, 0},
};

/**
 * @brief pseudo pattern to turn off LED
 */
static const blink_step_t idle[] = {
    {LED_BLINK_RGB, SET_IRGB(STATUS_LED, 0, 0, 0), 0},
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
    ESP_LOGI(LED, "Creating LED strip object with RMT backend");

    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_LED_PATTERN_STRIP_GPIO,  // The GPIO that connected to the LED strip's data line
        .max_leds = CONFIG_LED_PATTERN_STRIP_NUMBER,      // The number of LEDs in the strip,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,  // Pixel format of your LED strip
        .led_model = LED_MODEL_WS2812,                                // LED strip model
        .flags.invert_out =                                           // whether to invert the output signal
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
        .blink_lists = led_mode,
        .blink_list_num = LED_PATTERNS_MAX,
    };

    ESP_ERROR_CHECK_WITHOUT_ABORT(led_indicator_new_strips_device(&config, &strips_config, &led_handle));

    if (led_handle) {
        ESP_LOGI(LED, "Created LED strip object");
        set_led_brightness(brightness);
    }
}
#endif  // CONFIG_LED_PATTERN_STRIP
