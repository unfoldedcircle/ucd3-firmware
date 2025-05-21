// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "adc_reader.h"

typedef enum ChargerState {
    CHARGER_DISABLED,
    CHARGER_IDLE,
    CHARGER_CHARGING,
    CHARGER_OVERCURRENT,
} charger_state_t;

class RemoteCharger {
 public:
    RemoteCharger(std::unique_ptr<AdcReader> reader);

    /// @brief Start charger monitoring and enable charging.
    /// @return ESP_OK if started, ESP_FAIL if timer could not be started.
    esp_err_t start();

 protected:
    /// @brief Check the provided voltage value if it is over the defined charging current limit.
    ///
    /// 100mv correspond to 1A charging current. If the provided value is over the maximum charging limit,
    /// a re-measurement is started and charging is disabled if all readings are still over the limit.
    /// @param voltage the measured charging value in mV
    /// @return false if charging is below the limit, true if charging current is too high and charging has been
    /// disabled.
    bool checkOverCurrent(int voltage);

    void checkCharging(int voltage);

 private:
    /// @brief Periodic timer to read charging current and perform over current check.
    ///
    /// In case of over-current, the reading will be re-checked and the charging disabled if the consecutive
    /// measurement(s) are still over the limit.
    static void chargerTimerCb(TimerHandle_t timer_id);

 private:
    std::unique_ptr<AdcReader> adc_reader_;

    TimerHandle_t   charge_timer;
    uint64_t        last_log_time;
    charger_state_t last_charger_state;
    uint8_t         change_state_count;
};
