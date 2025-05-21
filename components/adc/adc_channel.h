// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include "esp_adc/adc_oneshot.h"
#include "hal/adc_types.h"

#include "adc_reader.h"

class AdcUnit;

/// @brief ADC reading from a oneshot ADC channel.
class AdcChannel : public AdcReader {
 public:
    AdcChannel(adc_channel_t channel, std::shared_ptr<AdcUnit> adc_unit, adc_cali_handle_t handle);
    virtual ~AdcChannel();

    virtual esp_err_t read(int *voltage);

 private:
    // logging tag, based on adc channel
    char *tag_;

    adc_channel_t            channel_;
    adc_cali_handle_t        adc_cali_handle_;
    std::shared_ptr<AdcUnit> adc_unit_;
};
