// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "charger.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board.h"
#include "sdkconfig.h"
#include "uc_events.h"

static const char *const TAG = "CHARGE";

RemoteCharger::RemoteCharger(std::unique_ptr<AdcReader> reader)
    : adc_reader_(std::move(reader)),
      charge_timer(nullptr),
      last_log_time(0),
      last_charger_state(CHARGER_DISABLED),
      change_state_count(0) {}

esp_err_t RemoteCharger::start() {
    ESP_RETURN_ON_ERROR(gpio_set_level(CHARGING_ENABLE, 1), TAG, "Failed to enable charging");

    if (!charge_timer) {
        ESP_LOGI(TAG, "Starting charger timer with period of %dms. Overcurrent protection: %umA",
                 CONFIG_UCD_CHARGER_PERIOD, CONFIG_UCD_CHARGER_MAX_CURRENT_MA);
        charge_timer = xTimerCreate("charger", pdMS_TO_TICKS(CONFIG_UCD_CHARGER_PERIOD), pdTRUE, this, chargerTimerCb);
        if (!charge_timer) {
            ESP_LOGE(TAG, "Failed to create charging timer");
            return ESP_FAIL;
        }
    }

    last_log_time = esp_timer_get_time() / 1000;
    ESP_RETURN_ON_FALSE(xTimerStart(charge_timer, pdMS_TO_TICKS(3000)), ESP_FAIL, TAG,
                        "Failed to start charging timer");

    return ESP_OK;
}

bool RemoteCharger::checkOverCurrent(int voltage) {
    int              remeasure = 0;
    const static int kMaxVoltage = CONFIG_UCD_CHARGER_MAX_CURRENT_MA / 10;

    if (voltage < kMaxVoltage) {
        return false;
    }

    for (int i = 0; i < CONFIG_UCD_CHARGER_OVERCURRENT_REMEASURE; i++) {
        if (adc_reader_->read(&remeasure) != ESP_OK) {
            // play it safe in this unlikely case and stop charging
            continue;
        }
        if (remeasure < kMaxVoltage) {
            return false;
        }
    }

    // Charging current too high: switch off charging!
    gpio_set_level(CHARGING_ENABLE, 0);
    ESP_LOGE(TAG, "Charging overcurrent protection: shut off charger! Detected charging current: %umA", voltage * 10);
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_event_post(UC_DOCK_EVENTS, UC_EVENT_OVER_CURRENT, NULL, 0, pdMS_TO_TICKS(10 * 1000)));

    return true;
}

void RemoteCharger::checkCharging(int voltage) {
    uint64_t time = esp_timer_get_time() / 1000;
    // for this simple log function we don't have to care about overflows
    if (time - last_log_time >= CONFIG_UCD_CHARGER_LOG_INTERVAL) {
        ESP_LOGD(TAG, "%d mV", voltage);
        last_log_time = time;
    }

    charger_state_t state = CHARGER_DISABLED;

    if (voltage < CONFIG_UCD_CHARGER_ADC_THRESHOLD) {
        state = CHARGER_IDLE;
    } else {
        state = CHARGER_CHARGING;
    }

    if (state != last_charger_state) {
        ESP_LOGD(TAG, "Charging state changed: %d -> %d: %dmV", last_charger_state, state, voltage);
        last_charger_state = state;
        change_state_count = 1;
    } else if (change_state_count < CONFIG_UCD_CHARGER_STATE_MEASURE_COUNT) {
        change_state_count++;
        ESP_LOGD(TAG, "New charging state %d active in %d/%d readings: %dmV", state, change_state_count,
                 CONFIG_UCD_CHARGER_STATE_MEASURE_COUNT, voltage);
    }

    if (change_state_count == CONFIG_UCD_CHARGER_STATE_MEASURE_COUNT) {
        change_state_count++;
        ESP_LOGI(TAG, "Charging state changed: %s (%dmV)", state == CHARGER_CHARGING ? "ON" : "OFF", voltage);
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            esp_event_post(UC_DOCK_EVENTS, state == CHARGER_CHARGING ? UC_EVENT_CHARGING_ON : UC_EVENT_CHARGING_OFF,
                           NULL, 0, pdMS_TO_TICKS(200)));
    }
}

void RemoteCharger::chargerTimerCb(TimerHandle_t timer_id) {
    if (!gpio_get_level(CHARGING_ENABLE)) {
        // ADC reading cannot be used if charging is disabled
        return;
    }

    RemoteCharger *that = (RemoteCharger *)pvTimerGetTimerID(timer_id);

    int voltage = 0;
    if (that->adc_reader_->read(&voltage) != ESP_OK) {
        return;
    }

    if (that->checkOverCurrent(voltage)) {
        that->last_charger_state = CHARGER_OVERCURRENT;
        return;
    }

    that->checkCharging(voltage);
}
