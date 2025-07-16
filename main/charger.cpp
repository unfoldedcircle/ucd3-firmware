// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "charger.h"

#include <assert.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board.h"
#include "sdkconfig.h"
#include "uc_events.h"

static const char *const TAG = "CHARGE";

RemoteCharger::RemoteCharger(std::unique_ptr<AdcReader> reader, std::shared_ptr<AdcReader> vcc_reader)
    : adc_reader_(std::move(reader)),
      vcc_reader_(std::move(vcc_reader)),
      charge_timer_(nullptr),
      last_log_time_(0),
      charger_adc_threshold_(CONFIG_UCD_CHARGER_ADC_THRESHOLD_NEW),
      last_vcc_time_(0),
      last_vcc_event_time_(0),
      last_charger_state_(CHARGER_DISABLED),
      change_state_count_(0) {
    assert(adc_reader_);
}

esp_err_t RemoteCharger::start() {
    // Enable charging, wait to settle voltage reading, determine voltage offset for simple remote charging detection.
    // Charging circuit in the remote has a short delay to start charging. Even with a remote in the cradle, charging
    // doesn't immediatly start.
    ESP_RETURN_ON_ERROR(gpio_set_level(CHARGING_ENABLE, 1), TAG, "Failed to enable charging");
    vTaskDelay(pdMS_TO_TICKS(10));

    int voltage = 0;
    if (adc_reader_->read(&voltage) == ESP_OK) {
        ESP_LOGI(TAG, "Charging voltage offset: %d mV", voltage);
        if (voltage < 5) {
            charger_adc_threshold_ = CONFIG_UCD_CHARGER_ADC_THRESHOLD_OLD;
        }
    }

    if (!charge_timer_) {
        ESP_LOGI(TAG, "Starting charger timer with period of %dms. Overcurrent protection: %umA",
                 CONFIG_UCD_CHARGER_PERIOD, CONFIG_UCD_CHARGER_MAX_CURRENT_MA);
        charge_timer_ = xTimerCreate("charger", pdMS_TO_TICKS(CONFIG_UCD_CHARGER_PERIOD), pdTRUE, this, chargerTimerCb);
        if (!charge_timer_) {
            ESP_LOGE(TAG, "Failed to create charging timer");
            return ESP_FAIL;
        }
    }

    last_log_time_ = esp_timer_get_time() / 1000;
    ESP_RETURN_ON_FALSE(xTimerStart(charge_timer_, pdMS_TO_TICKS(3000)), ESP_FAIL, TAG,
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

    uc_event_error_t event = {.error = UC_ERROR_OVER_CURRENT, .esp_err = 0, .value = voltage * 10, .fatal = true};
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_event_post(UC_DOCK_EVENTS, UC_EVENT_ERROR, &event, sizeof(event), pdMS_TO_TICKS(10 * 1000)));

    return true;
}

void RemoteCharger::checkCharging(int voltage) {
    uint64_t now = esp_timer_get_time() / 1000;
    // for this simple log function we don't have to care about overflows
    if (now - last_log_time_ >= CONFIG_UCD_CHARGER_LOG_INTERVAL) {
        ESP_LOGI(TAG, "%d mV", voltage);
        last_log_time_ = now;
    }

    charger_state_t state = CHARGER_DISABLED;

    if (voltage < charger_adc_threshold_) {
        state = CHARGER_IDLE;
    } else {
        state = CHARGER_CHARGING;
    }

    if (state != last_charger_state_) {
        ESP_LOGD(TAG, "Charging state changed: %d -> %d: %dmV", last_charger_state_, state, voltage);
        last_charger_state_ = state;
        change_state_count_ = 1;
    } else if (change_state_count_ < CONFIG_UCD_CHARGER_STATE_MEASURE_COUNT) {
        change_state_count_++;
        ESP_LOGD(TAG, "New charging state %d active in %d/%d readings: %dmV", state, change_state_count_,
                 CONFIG_UCD_CHARGER_STATE_MEASURE_COUNT, voltage);
    }

    if (change_state_count_ == CONFIG_UCD_CHARGER_STATE_MEASURE_COUNT) {
        change_state_count_++;
        ESP_LOGI(TAG, "Charging state changed: %s (%dmV)", state == CHARGER_CHARGING ? "ON" : "OFF", voltage);
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            esp_event_post(UC_DOCK_EVENTS, state == CHARGER_CHARGING ? UC_EVENT_CHARGING_ON : UC_EVENT_CHARGING_OFF,
                           NULL, 0, pdMS_TO_TICKS(200)));
    }

    if (vcc_reader_) {
        int vcc = 0;

        // Only check input voltage in the configured interval, usually every 5 sec.
        // This function gets called multiple times per second by the chargerTimerCb.
        if (now - last_vcc_time_ >= CONFIG_UCD_CHARGER_VCHECK_INTERVAL_MS) {
            if (vcc_reader_->read(&vcc) == ESP_OK) {
                last_vcc_time_ = now;
                // adjust for voltage divider
                vcc *= 2;

                if (vcc < CONFIG_UCD_CHARGER_MIN_VOLTAGE) {
                    ESP_LOGW(TAG, "Supply voltage low: %dmV", vcc);
                    if (now - last_vcc_event_time_ >= CONFIG_UCD_CHARGER_VCHECK_ERR_INTERVAL_MS) {
                        last_vcc_event_time_ = now;
                        uc_event_error_t event = {
                            .error = UC_ERROR_VCC_LOW, .esp_err = 0, .value = vcc, .fatal = false};
                        ESP_ERROR_CHECK_WITHOUT_ABORT(
                            esp_event_post(UC_DOCK_EVENTS, UC_EVENT_ERROR, &event, sizeof(event), pdMS_TO_TICKS(200)));
                    }
                }
            }
        }
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
        that->last_charger_state_ = CHARGER_OVERCURRENT;
        return;
    }

    that->checkCharging(voltage);
}
