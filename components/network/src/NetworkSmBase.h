// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdlib.h>

#include "esp_eth_driver.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "config.h"
#include "network_priv.h"

// Log macros for StateSmith

#define LOGE(format, ...) ESP_LOGE("SM", format __VA_OPT__(, ) __VA_ARGS__)
#define LOGW(format, ...) ESP_LOGW("SM", format __VA_OPT__(, ) __VA_ARGS__)
#define LOGI(format, ...) ESP_LOGI("SM", format __VA_OPT__(, ) __VA_ARGS__)
#define LOGD(format, ...) ESP_LOGD("SM", format __VA_OPT__(, ) __VA_ARGS__)

/// Base class of the StateSmith generated NetworkSm state machine class
class NetworkBase {
 public:
    NetworkBase();

    /// @brief pass optional parameters to SM
    /// @param parameters
    void setEventParameters(queue_message* parameters);

    // helper methods exposed to the StateSmith diagram
 protected:
    bool isWifiPreferred();
    bool hasWifiConfig();
    void initNetwork();
    void initEthernet();
    bool initWifi();
    void startEthLinkTimer();
    void startEthLinkDownTimer();
    void startDhcpTimer();
    void startWifiConnectedTimer();
    void startWifiPollingTimer();
    void stopTimer();
    void connectActiveSsid();
    bool shouldRetryActiveWifiConnection();
    void retryActiveWiFiConnection();
    bool isWifiErrReason(uint8_t reason);
    void connectWifi();
    bool isWifiConnected() { return wifi_connected_; };
    void saveActiveWifiConfig();
    void clearWifiConfig();
    void clearEventParameters();
    void statusUpdate(update_reason_code_t update_reason_code);
    void startWifiDhcpClient();
    void startImprovWifi();
    void setImprovStopped();
    void setImprovAuthRequired();
    void setImprovAuthorized();
    void onImprovConnectTimeout();
    void setImprovProvisioning();
    void startImprovTimer();
    void setImprovWifiProvisioned();
    void reboot();

    // private methods only for the base class, not intended for the StateSmith diagram
 private:
    void setTimer(uint32_t timeout_ms, const char* tag);
    bool isInterfaceConnected(esp_netif_t* netif);

    // state machine variables exposed to the StateSmith diagram
 protected:
    uint16_t retries_;

    // private data only for the base class, not intended for the StateSmith diagram
 private:
    esp_eth_handle_t eth_handle_;
    esp_netif_t*     eth_netif_;
    esp_netif_t*     wifi_netif_;
    TimerHandle_t    state_timer_;
    char*            timer_tag_;
    queue_message*   event_parameters_;
    bool             wifi_connected_;
    bool             improv_init_;

    bool     wifi_preferred_;
    uint32_t sta_duration_ms_;

    int32_t  total_connected_time_;
    int64_t  last_connected_;
    uint16_t num_disconnect_;
};
