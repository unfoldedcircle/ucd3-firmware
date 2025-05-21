// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include "esp_adc/adc_oneshot.h"
#include "hal/adc_types.h"

#include "adc_channel.h"

/// @brief Oneshot ADC unit wrapper for creating AdcChannel instances.
class AdcUnit : public std::enable_shared_from_this<AdcUnit> {
    friend class AdcChannel;

    struct Private {
        adc_unit_t                unit;
        adc_oneshot_unit_handle_t adc_handle;
        explicit Private() = default;
    };

 public:
    // Constructor is only usable by this class. Use create method!
    AdcUnit(Private cfg);
    virtual ~AdcUnit();

    /// @brief Factory creation method to create an AdcUnit instance.
    /// @param unit ADC unit to create.
    /// @return A shared pointer to the created unit, nullptr in case of an error.
    static std::shared_ptr<AdcUnit> create(adc_unit_t unit);

    std::unique_ptr<AdcChannel> createChannel(adc_channel_t channel, adc_atten_t attenuation = ADC_ATTEN_DB_12);

    adc_oneshot_unit_handle_t getHandle() { return adc_handle_; }

 private:
    static bool calibrationInit(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten,
                                adc_cali_handle_t *out_handle);
    static void calibrationDeinit(adc_cali_handle_t handle);

 private:
    // logging tag, based on adc unit
    char *tag_;

    adc_unit_t                unit_;
    adc_oneshot_unit_handle_t adc_handle_;
};
