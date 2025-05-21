// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdlib.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "DisplayDriver.h"
#include "EventParameter.h"
#include "config.h"
#include "sdkconfig.h"

// Log macros for StateSmith

#define LOGE(format, ...) ESP_LOGE("SM", format __VA_OPT__(, ) __VA_ARGS__)
#define LOGW(format, ...) ESP_LOGW("SM", format __VA_OPT__(, ) __VA_ARGS__)
#define LOGI(format, ...) ESP_LOGI("SM", format __VA_OPT__(, ) __VA_ARGS__)
#define LOGD(format, ...) ESP_LOGD("SM", format __VA_OPT__(, ) __VA_ARGS__)

const static uint16_t kFactoryResetTimeoutMs = CONFIG_UCD_UISM_FACTORY_RESET_TIMOUT_SEC * 1000;

/// Base class of the StateSmith generated DisplaySm state machine class.
///
/// Provides all exposed state machine variables and methods for the StateSmith diagram.
/// It is the proxy between the generated state machine and the rest of the system.
class DisplayBase {
 public:
    DisplayBase();
    virtual ~DisplayBase();

    void setDisplay(Display* display) { display_ = display; };

    /// @brief pass optional parameters to SM
    /// This is a HACK because the generated DisplaySm::dispatchEvent method does not allow parameters!
    /// For a proper implementation the code generation should be changed.
    /// @param parameters
    void setEventParameters(std::unique_ptr<EventParameter> parameter);

    void setNetworkInfo(const uc_event_network_state_t* state);

    // helper methods exposed to the StateSmith diagram
 protected:
    void startBootupTimer();
    void startRunningTimer();
    void startOtaTimer();
    void startIrLearnedOkTimer();
    void stopIrLearning();
    void stopTimer();

    void clearScreen();
    void showIdentifyScreen();
    void showErrorScreen();
    void showChargingScreen();
    void showChargingOffScreen();
    void showInfoScreen();
    void showNextInfoScreen();
    void refreshNetwork();
    void updateNetworkScreen();

    void connectingTimerReset();
    bool connectingTimerAfter(uint16_t duration_ms);
    void updateConnectingScreen();

    void updateExtPortMode();

    void showImprovScreen();
    void showImprovConfirmationScreen();
    void showImprovAuthorizedScreen();
    void showImprovConnectingScreen();
    void showImprovDoneScreen();
    void showIrLearningScreen();
    void showIrLearnedOkScreen();
    void showIrLearnedFailedScreen();
    void showOtaScreen();
    void updateOtaScreen();
    void showOtaSuccessScreen();
    void showOtaFailScreen();

    void triggerConnected();  // TODO still required?

    void resetTimerRestart();
    bool resetTimerAfter(uint16_t duration_ms);
    void startFactoryResetScreen();
    void updateFactoryResetScreen();
    void updateBtnHoldtime();
    void factoryReset();

    // private methods only for the base class, not intended for the StateSmith diagram
 private:
    void        setTimer(uint32_t timeout_ms, const char* tag);
    static void timerCallback(TimerHandle_t timer_id);
    std::string getIpString();

    // state machine variables exposed to the StateSmith diagram
 protected:
    bool           charging;
    network_kind_t network;
    bool           eth_link;
    bool           wifi_connected;
    std::string    ssid;
    int8_t         rssi;

    u_int16_t btn_holdtime;

    // private data only for the base class, not intended for the StateSmith diagram
 private:
    std::unique_ptr<EventParameter> event_parameter_;

    esp_ip_addr_t  ip_;
    EventParameter ext_port1_;
    EventParameter ext_port2_;

    uint8_t       info_screen_index_;
    uint8_t       connect_screen_index_;
    uint32_t      reset_timer_;
    uint32_t      connecting_timer_;
    TimerHandle_t state_timer_;
    char*         timer_tag_;
    Display*      display_;
};
