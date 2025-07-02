// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "esp_check.h"
#include "esp_log.h"

#include "adc_unit.h"
#include "mem_util.h"

static const char *const TAG = "ADC";

AdcChannel::AdcChannel(adc_channel_t channel, std::shared_ptr<AdcUnit> adc_unit, adc_cali_handle_t handle)
    : channel_(channel), adc_cali_handle_(handle), adc_unit_(adc_unit) {
    if (asprintf(&tag_, "ADCC%d", channel) < 0) {
        tag_ = nullptr;
    };
}

AdcChannel::~AdcChannel() {
    ESP_LOGI(TAG, "AdcChannel destructor");
    AdcUnit::calibrationDeinit(adc_cali_handle_);
    FREE_AND_NULL(tag_);
}

esp_err_t AdcChannel::read(int *voltage) {
    static constexpr int SAMPLES_TO_AVERAGE = 3;
    static constexpr int DISCARD_SAMPLES = 1;

    int adc_raw = 0;
    int sum = 0;

    auto adc_handle = adc_unit_->getHandle();

    // Discard initial unstable samples
    for (int i = 0; i < DISCARD_SAMPLES; i++) {
        ESP_RETURN_ON_ERROR(adc_oneshot_read(adc_handle, channel_, &adc_raw), tag_, "Failed to discard sample %d", i);
    }

    // Perform averaging of valid samples
    for (int i = 0; i < SAMPLES_TO_AVERAGE; i++) {
        ESP_RETURN_ON_ERROR(adc_oneshot_read(adc_handle, channel_, &adc_raw), tag_, "Failed to read sample %d", i);
        sum += adc_raw;
    }

    int averaged_raw = sum / SAMPLES_TO_AVERAGE;

    // Convert averaged raw value to voltage
    ESP_RETURN_ON_ERROR(adc_cali_raw_to_voltage(adc_cali_handle_, averaged_raw, voltage), tag_,
                        "Failed to convert averaged raw value %d", averaged_raw);

    ESP_LOGD(tag_, "ADC read: raw = %d, voltage = %d mV", averaged_raw, *voltage);

    return ESP_OK;
}
