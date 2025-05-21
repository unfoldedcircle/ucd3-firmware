// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "button.h"

#include "esp_check.h"
#include "esp_log.h"

#include "sdkconfig.h"

#ifdef CONFIG_IMPROV_WIFI_AUTHENTICATION_BUTTON

#include "config.h"
#include "iot_button.h"
#include "network.h"
#include "uc_events.h"

static const char *const TAG = "BUTTON";

static void button_single_click_cb(void *button_handle, void *usr_data) {
    ESP_LOGI(TAG, "BUTTON_SINGLE_CLICK");
    // TODO refactor, only use UC_EVENT_BUTTON_CLICK
    trigger_button_press_event();
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, UC_EVENT_BUTTON_CLICK, NULL, 0, pdMS_TO_TICKS(200)));
}

static void button_double_click_cb(void *button_handle, void *usr_data) {
    ESP_LOGI(TAG, "BUTTON_DOUBLE_CLICK");
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_event_post(UC_DOCK_EVENTS, UC_EVENT_BUTTON_DOUBLE_CLICK, NULL, 0, pdMS_TO_TICKS(200)));
}

static void send_uc_long_press_event(void *arg, uc_event_id_t event_id) {
    button_handle_t button_handle = (button_handle_t)arg;
    ESP_LOGI(TAG, "BUTTON_LONG_CLICK");

    uint16_t holdtime = iot_button_get_ticks_time(button_handle);
    ESP_LOGI(TAG, "long click %u ms", holdtime);

    uc_event_button_long_t event = {.holdtime = holdtime};
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, event_id, &event, sizeof(event), pdMS_TO_TICKS(200)));
}

static void button_long_press_start_cb(void *arg, void *usr_data) {
    send_uc_long_press_event(arg, UC_EVENT_BUTTON_LONG_PRESS_START);
}

static void button_long_click_cb(void *arg, void *usr_data) {
    send_uc_long_press_event(arg, UC_EVENT_BUTTON_LONG_PRESS_UP);
}

esp_err_t init_button() {
    // create gpio button
    button_config_t gpio_btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = CONFIG_BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time = CONFIG_BUTTON_SHORT_PRESS_TIME_MS,
        .gpio_button_config =
            {
                .gpio_num = CONFIG_IMPROV_WIFI_BUTTON_GPIO,
                .active_level = 0,
                .disable_pull = false,
            },
    };
    button_handle_t gpio_btn = iot_button_create(&gpio_btn_cfg);
    ESP_RETURN_ON_FALSE(gpio_btn, ESP_FAIL, TAG, "Button create failed");
    int32_t duration = 2000;
    iot_button_set_param(gpio_btn, BUTTON_LONG_PRESS_TIME_MS, (void *)duration);

    ESP_RETURN_ON_ERROR(iot_button_register_cb(gpio_btn, BUTTON_SINGLE_CLICK, button_single_click_cb, NULL), TAG,
                        "Error registering button single click handler");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(gpio_btn, BUTTON_DOUBLE_CLICK, button_double_click_cb, NULL), TAG,
                        "Error registering button double click handler");

    ESP_RETURN_ON_ERROR(iot_button_register_cb(gpio_btn, BUTTON_LONG_PRESS_START, button_long_press_start_cb, NULL),
                        TAG, "Error registering button long press start handler");

    ESP_RETURN_ON_ERROR(iot_button_register_cb(gpio_btn, BUTTON_LONG_PRESS_UP, button_long_click_cb, NULL), TAG,
                        "Error registering button long click handler");

    return ESP_OK;
}

#else  // CONFIG_IMPROV_WIFI_AUTHENTICATION_BUTTON
esp_err_t init_button() {
    return ESP_OK;
}
#endif