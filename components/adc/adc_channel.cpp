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
    esp_err_t ret = ESP_OK;
    int       adc_raw = 0;

    auto adc_handle = adc_unit_->getHandle();
    ESP_RETURN_ON_ERROR(adc_oneshot_read(adc_handle, channel_, &adc_raw), tag_, "Failed to measure gnd");
    ESP_RETURN_ON_ERROR(adc_cali_raw_to_voltage(adc_cali_handle_, adc_raw, voltage), tag_,
                        "Failed to convert raw gnd value %d", adc_raw);

    return ret;
}
