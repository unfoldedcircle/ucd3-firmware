// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adc_unit.h"

#include "esp_check.h"
#include "esp_log.h"

#include "mem_util.h"

static const char *const TAG = "ADC";

std::shared_ptr<AdcUnit> AdcUnit::create(adc_unit_t unit) {
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = unit,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    adc_oneshot_unit_handle_t adc_handle;

    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC unit: %d", ret);
        return nullptr;
    }

    Private cfg;
    cfg.unit = unit;
    cfg.adc_handle = adc_handle;
    return std::make_shared<AdcUnit>(cfg);
}

AdcUnit::AdcUnit(Private cfg) : unit_(cfg.unit), adc_handle_(cfg.adc_handle) {
    if (asprintf(&tag_, "ADCU%d", unit_) < 0) {
        tag_ = nullptr;
    };
}

AdcUnit::~AdcUnit() {
    ESP_LOGD(TAG, "AdcUnit destructor");
    ESP_ERROR_CHECK_WITHOUT_ABORT(adc_oneshot_del_unit(adc_handle_));
    FREE_AND_NULL(tag_);
}

std::unique_ptr<AdcChannel> AdcUnit::createChannel(adc_channel_t channel, adc_atten_t attenuation) {
    esp_err_t              ret = ESP_OK;
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = attenuation,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    ESP_GOTO_ON_FALSE(tag_, ESP_ERR_NO_MEM, err, TAG, "No memory for tag creation");

    ESP_GOTO_ON_ERROR(adc_oneshot_config_channel(adc_handle_, channel, &chan_cfg), err, tag_,
                      "Failed to configure ADC channel");

    adc_cali_handle_t cali_handle;
    ESP_GOTO_ON_FALSE(calibrationInit(unit_, channel, chan_cfg.atten, &cali_handle), ESP_ERR_NOT_SUPPORTED, err, tag_,
                      "ADC calibration not supported");

    return std::make_unique<AdcChannel>(channel, shared_from_this(), cali_handle);
err:
    return nullptr;
}

bool AdcUnit::calibrationInit(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten,
                              adc_cali_handle_t *out_handle) {
    adc_cali_handle_t handle = NULL;
    esp_err_t         ret = ESP_FAIL;
    bool              calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "Calibration scheme version is Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "Calibration scheme version is Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration success %d:%d", unit, channel);
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

void AdcUnit::calibrationDeinit(adc_cali_handle_t handle) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "Deregister Curve Fitting calibration scheme");
    ESP_ERROR_CHECK_WITHOUT_ABORT(adc_cali_delete_scheme_curve_fitting(handle));

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "Deregister Line Fitting calibration scheme");
    ESP_ERROR_CHECK_WITHOUT_ABORT(adc_cali_delete_scheme_line_fitting(handle));
#endif
}
