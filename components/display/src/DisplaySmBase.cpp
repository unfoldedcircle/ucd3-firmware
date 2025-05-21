// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DisplaySmBase.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "DisplayDriver.h"
#include "mem_util.h"
#include "network.h"
#include "sdkconfig.h"
#include "service_ir.h"
#include "string_util.h"

static const char *const TAG = "UI";

DisplayBase::DisplayBase()
    : charging(false),
      network(ETHERNET),
      eth_link(false),
      wifi_connected(false),
      rssi(0),
      btn_holdtime(0),
      event_parameter_(nullptr),
      info_screen_index_(0),
      connect_screen_index_(0),
      reset_timer_(0),
      connecting_timer_(0),
      state_timer_(nullptr),
      timer_tag_(nullptr),
      display_(nullptr) {
    memset(&ip_, 0, sizeof(ip_));
}

DisplayBase::~DisplayBase() {
    if (state_timer_) {
        xTimerStop(state_timer_, portMAX_DELAY);
    }
    FREE_AND_NULL(timer_tag_);
}

void DisplayBase::setEventParameters(std::unique_ptr<EventParameter> parameter) {
    ESP_LOGI(TAG, "setEventParameters: icon=%d, %s / %s", parameter->icon(), parameter->title().c_str(),
             parameter->message().c_str());
    event_parameter_ = std::move(parameter);
}

void DisplayBase::setNetworkInfo(const uc_event_network_state_t *state) {
    network = state->connection;
    eth_link = state->eth_link;
    ssid = toPrintableString(state->ssid, sizeof(state->ssid));
    rssi = state->rssi;
    memcpy(&ip_, &state->ip, sizeof(ip_));
    ESP_LOGI(TAG, "network=%s, eth_link=%d, ssid=%s, rssi=%d, ip=%s", network == ETHERNET ? "eth" : "wifi", eth_link,
             ssid.c_str(), rssi, getIpString().c_str());
}

void DisplayBase::startBootupTimer() {
    setTimer(CONFIG_UCD_UISM_BOOTUP_TIMEOUT_SEC * 1000, "Boot up timer");
}

void DisplayBase::startRunningTimer() {
    setTimer(CONFIG_UCD_UISM_RUNNING_TIMEOUT_SEC * 1000, "Running timer");
}

void DisplayBase::startOtaTimer() {
    setTimer(CONFIG_UCD_UISM_OTA_TIMEOUT_SEC * 1000, "OTA timeout");
}

void DisplayBase::startIrLearnedOkTimer() {
    setTimer(3 * 1000, "IR learn OK");
}

void DisplayBase::stopIrLearning() {
    InfraredService::getInstance().stopIrLearn();
}

void DisplayBase::clearScreen() {
    ESP_LOGI(TAG, "clearScreen");
    if (display_) {
        display_->clearScreen();
    }
}

void DisplayBase::showIdentifyScreen() {
    ESP_LOGI(TAG, "showIdentifyScreen");
    if (display_) {
        // TODO finalize screen
        display_->showIconScreen(UI_ICON_OK, "Hello", "I'm here!");
    }
}

void DisplayBase::showErrorScreen() {
    ESP_LOGI(TAG, "showErrorScreen");
    if (display_) {
        // TODO finalize screen
        if (event_parameter_) {
            display_->showErrorScreen(event_parameter_->title().c_str(), event_parameter_->message().c_str(),
                                      event_parameter_->isFatalError());
        } else {
            display_->showErrorScreen("Error", "", event_parameter_->isFatalError());
        }
    }
}

void DisplayBase::showChargingScreen() {
    ESP_LOGI(TAG, "showChargingScreen");
    if (display_) {
        // TODO finalize screen
        display_->showIconScreen(UI_ICON_CHARGING, "Charging", " ");
    }
}

void DisplayBase::showChargingOffScreen() {
    ESP_LOGI(TAG, "showChargingOffScreen");
    if (display_) {
        // TODO finalize screen
        display_->showIconScreen(UI_ICON_NOT_CHARGING, "", "");
    }
}

void DisplayBase::showInfoScreen() {
    ESP_LOGI(TAG, "showInfoScreen");
    info_screen_index_ = 0;
    if (display_) {
        // 1st screen: network type
        std::string ip = getIpString();
        const char *ip_info = ip.empty() ? " " : ip.c_str();
        if (network == ETHERNET) {
            if (eth_link) {
                display_->showIconScreen(UI_ICON_ETHERNET, "ETH", ip_info);
            } else {
                display_->showIconScreen(UI_ICON_FAILED, "ETH", " ");
            }
        } else {
            if (wifi_connected) {
                display_->showIconScreen(UI_ICON_WIFI, ssid.empty() ? " " : ssid, ip_info);
            } else {
                display_->showIconScreen(UI_ICON_WIFI_ERROR, "WiFi", ssid.empty() ? " " : ssid);
            }
        }
    }
}

void DisplayBase::showNextInfoScreen() {
    ESP_LOGI(TAG, "showNextInfoScreen");

    Config &cfg = Config::instance();
    uint8_t max_screen = cfg.hasChargingFeature() ? 5 : 4;

    info_screen_index_++;

    if (info_screen_index_ > max_screen) {
        info_screen_index_ = 0;
    }
    if (display_) {
        switch (info_screen_index_) {
            // network type
            case 0:
                showInfoScreen();
                break;
            // hostname
            case 1: {
                display_->showIconScreen(network == ETHERNET ? UI_ICON_ETHERNET : UI_ICON_WIFI, "hostname",
                                         cfg.getHostName());
                break;
            }
            // external port configuration
            case 2:
                display_->showIconScreen(UI_ICON_NONE, ext_port1_.title(), ext_port1_.message());
                break;
            case 3:
                display_->showIconScreen(UI_ICON_NONE, ext_port2_.title(), ext_port2_.message());
                break;
            // version
            case 4:
                // TODO info icon?
                display_->showIconScreen(UI_ICON_NONE, "Version", DOCK_VERSION);
                break;
            // charging state
            default:
                display_->showIconScreen(charging ? UI_ICON_CHARGING : UI_ICON_NOT_CHARGING, "", "");
        }
    }
}

void DisplayBase::connectingTimerReset() {
    connecting_timer_ = esp_log_timestamp();
}

bool DisplayBase::connectingTimerAfter(uint16_t duration_ms) {
    return (esp_log_timestamp() - connecting_timer_) > duration_ms;
}

void DisplayBase::updateConnectingScreen() {
    ESP_LOGI(TAG, "updateConnectingScreen, eth_link=%d", eth_link);

    connect_screen_index_++;
    if (connect_screen_index_ > 3) {
        connect_screen_index_ = 0;
    }

    if (display_) {
        ui_icon_t icon = UI_ICON_WIFI_ERROR;
        bool      wifi = network == WIFI;
        switch (connect_screen_index_) {
            case 1:
                icon = wifi ? UI_ICON_WIFI_WEAK : UI_ICON_ETHERNET;
                break;
            case 2:
                icon = wifi ? UI_ICON_WIFI_FAIR : UI_ICON_WIFI_ERROR;  // TODO ethernet error icon
                break;
            case 3:
                icon = wifi ? UI_ICON_WIFI : UI_ICON_ETHERNET;
                break;
        }
        if (event_parameter_) {
            display_->showIconScreen(icon, event_parameter_->title(), event_parameter_->message());
        } else {
            display_->showIconScreen(icon, "Connecting", wifi ? ssid : " ");
        }
    }
}

void DisplayBase::updateExtPortMode() {
    if (event_parameter_->value() == 1) {
        ext_port1_ = *event_parameter_;
    } else {
        ext_port2_ = *event_parameter_;
    }
}

void DisplayBase::updateNetworkScreen() {
    connect_screen_index_ = 0;
    refreshNetwork();
    ESP_LOGI(TAG, "updateNetworkScreen, eth_link=%d", eth_link);
    if (display_) {
        if (event_parameter_) {
            display_->showIconScreen(event_parameter_->icon(), event_parameter_->title(), event_parameter_->message());
        } else {
            display_->showIconScreen(UI_ICON_WIFI_ERROR, "Connecting", " ");
        }
    }
}

void DisplayBase::refreshNetwork() {
    ESP_LOGI(TAG, "refreshNetwork");
    eth_link = is_eth_link_up();

    if (eth_link) {
        // TODO check IP
    }
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        wifi_connected = true;
        std::string ssid = (const char *)ap_info.ssid;
        toPrintableString(ap_info.ssid, sizeof(ap_info.ssid));
        // TODO check IP
    } else {
        wifi_connected = false;
    }
}

void DisplayBase::showImprovScreen() {
    ESP_LOGI(TAG, "showImprovScreen");
    if (display_) {
        display_->showIconScreen(UI_ICON_SETUP, "Setup", "WiFi");
    }
}

void DisplayBase::showImprovConfirmationScreen() {
    ESP_LOGI(TAG, "showImprovConfirmationScreen");
    if (display_) {
        display_->showIconScreen(UI_ICON_PRESS, "WiFi setup", "Confirm with button");
    }
}

void DisplayBase::showImprovAuthorizedScreen() {
    ESP_LOGI(TAG, "showImprovAuthorizedScreen");
    if (display_) {
        display_->showIconScreen(UI_ICON_SETUP, "WiFi setup", "Waiting for data");
    }
}

void DisplayBase::showImprovConnectingScreen() {
    ESP_LOGI(TAG, "showImprovConnectingScreen");

    if (display_) {
        // ssid has been set with the IMPROV_PROVISIONING event
        display_->showWifiConnectingScreen("Setup", ssid);
    }
}

void DisplayBase::showImprovDoneScreen() {
    ESP_LOGI(TAG, "showImprovDoneScreen");
    if (display_) {
        display_->showIconScreen(UI_ICON_OK, "WiFi", "connected");
    }
}

void DisplayBase::showIrLearningScreen() {
    ESP_LOGI(TAG, "showIrLearningScreen");
    if (display_) {
        display_->showIconScreen(UI_ICON_IR_LEARNING, "IR learning", " ");
    }
}

void DisplayBase::showIrLearnedOkScreen() {
    ESP_LOGI(TAG, "showIrLearnedOkScreen");
    if (display_) {
        display_->showIconScreen(UI_ICON_OK, "IR code", event_parameter_->message());
    }
}

void DisplayBase::showIrLearnedFailedScreen() {
    ESP_LOGI(TAG, "showIrLearnedFailedScreen");
    if (display_) {
        display_->showIconScreen(UI_ICON_FAILED, "IR learning", event_parameter_->message());
    }
}

void DisplayBase::showOtaScreen() {
    ESP_LOGI(TAG, "showOtaScreen");
    if (display_) {
        display_->showIconScreen(UI_ICON_OTA, "OTA", "in progress");
    }
}

void DisplayBase::updateOtaScreen() {
    ESP_LOGI(TAG, "updateOtaScreen");
    if (display_ && event_parameter_) {
        display_->showIconScreen(UI_ICON_OTA, event_parameter_->title(), event_parameter_->message());
    }
}

void DisplayBase::showOtaSuccessScreen() {
    ESP_LOGI(TAG, "showOtaSuccessScreen");
    if (display_) {
        display_->showIconScreen(UI_ICON_OTA_OK, "Restarting", " ");
    }
}

void DisplayBase::showOtaFailScreen() {
    ESP_LOGI(TAG, "showOtaFailScreen");
    if (display_) {
        display_->showIconScreen(UI_ICON_OTA_FAILED, "Failed", " ");
    }
}

void DisplayBase::triggerConnected() {
    trigger_ui_connected_event();
}

void DisplayBase::resetTimerRestart() {
    reset_timer_ = esp_log_timestamp();
}

bool DisplayBase::resetTimerAfter(uint16_t duration_ms) {
    bool ret = (esp_log_timestamp() - reset_timer_) > duration_ms;
    if (ret) {
        btn_holdtime += duration_ms;
    }
    return ret;
}

void DisplayBase::startFactoryResetScreen() {
    btn_holdtime = event_parameter_ ? static_cast<uint16_t>(event_parameter_->value()) : 0;
    ESP_LOGW(TAG, "startFactoryResetScreen: %u", btn_holdtime);
    if (display_) {
        std::string title = std::to_string(kFactoryResetTimeoutMs);
        display_->showIconScreen(UI_ICON_RESET, title, "");
    }
}

void DisplayBase::updateFactoryResetScreen() {
    ESP_LOGW(TAG, "updateFactoryResetScreen: %u", btn_holdtime);

    uint16_t    time = btn_holdtime > kFactoryResetTimeoutMs ? 0 : (kFactoryResetTimeoutMs - btn_holdtime + 500) / 1000;
    std::string title = std::to_string(time);
    if (display_) {
        display_->showIconScreen(UI_ICON_RESET, title, "");
    }
}

void DisplayBase::updateBtnHoldtime() {
    if (event_parameter_) {
        btn_holdtime = static_cast<uint16_t>(event_parameter_->value());
        ESP_LOGI(TAG, "updated button hold time: %u", btn_holdtime);
    }
}

void DisplayBase::factoryReset() {
    ESP_LOGW(TAG, "factoryReset");
    if (display_) {
        display_->showIconScreen(UI_ICON_RESET, "Restarting", " ");
    }

    // we are running in the UI event loop!
    // Don't block it, otherwise we can't do any screen updates anymore.
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, UC_ACTION_RESET, NULL, 0, portMAX_DELAY));
}

void DisplayBase::setTimer(uint32_t timeout_ms, const char *tag) {
    ESP_LOGI(TAG, "Setting timer tag to %s", tag);
    FREE_AND_NULL(timer_tag_);
    timer_tag_ = strdup_to_psram(tag);

    if (!state_timer_) {
        ESP_LOGI(TAG, "Starting %s timer with period of %lums.", timer_tag_, timeout_ms);
        state_timer_ = xTimerCreate("display", pdMS_TO_TICKS(timeout_ms), pdFALSE, timer_tag_, timerCallback);
    } else {
        ESP_LOGI(TAG, "Changing %s timer period to %lums.", timer_tag_, timeout_ms);
        xTimerChangePeriod(state_timer_, pdMS_TO_TICKS(timeout_ms), portMAX_DELAY);
    }
    xTimerStart(state_timer_, portMAX_DELAY);
}

void DisplayBase::stopTimer() {
    if (state_timer_) {
        ESP_LOGI(TAG, "Stopping timer: %s", timer_tag_ ? timer_tag_ : "-");
        xTimerStop(state_timer_, portMAX_DELAY);
        FREE_AND_NULL(timer_tag_);
    } else {
        ESP_LOGW(TAG, "No state timer found to stop");
    }
}

void DisplayBase::timerCallback(TimerHandle_t timer_id) {
    const char *timer_tag = (const char *)pvTimerGetTimerID(timer_id);

    ESP_LOGI(TAG, "Timer expired: %s", timer_tag ? timer_tag : "-");
    trigger_ui_timer_event();
}

std::string DisplayBase::getIpString() {
    char buf[64];
    if (ip_.type == ESP_IPADDR_TYPE_V4) {
        snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip_.u_addr.ip4));
    } else if (ip_.type == ESP_IPADDR_TYPE_V6) {
        snprintf(buf, sizeof(buf), IPV6STR, IPV62STR(ip_.u_addr.ip6));
    } else {
        return std::string();
    }

    return buf;
}
