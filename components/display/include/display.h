// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include "esp_err.h"
#include "esp_event.h"

#include "config.h"

/// @brief Available icons to display on the screen.
typedef enum {
    UI_ICON_NONE = 0,
    UI_ICON_CHARGING,
    UI_ICON_NOT_CHARGING,
    UI_ICON_ERROR,
    UI_ICON_ETHERNET,
    UI_ICON_FAILED,
    UI_ICON_IR_LEARNING,
    UI_ICON_OK,
    UI_ICON_PRESS,
    UI_ICON_RESET,
    UI_ICON_SETUP,
    UI_ICON_OTA_FAILED,
    UI_ICON_OTA_OK,
    UI_ICON_OTA,
    UI_ICON_WIFI_ERROR,
    UI_ICON_WIFI,
    UI_ICON_WIFI_FAIR,
    UI_ICON_WIFI_WEAK
} ui_icon_t;

/// @brief Public LCD interface to show icons and text on the screen.
///
/// This interface is used to:
/// - initialize and start the display driver during startup.
/// - control the shown content from within the display state machine.
class Display {
 public:
    /// @brief Get driver instance of LCD interface.
    /// @param cfg Configuration class
    /// @return Pointer to concrete instance.
    static Display* instance(Config* cfg);

    /// @brief Initialize the display. May only be called once.
    /// @return ESP_OK if successful.
    virtual esp_err_t init() = 0;

    /// @brief Start the UI screens and show the bootup animation. May only be called once after a successful `init`
    /// call.
    /// @return ESP_OK if the boot animation could be started.
    virtual esp_err_t start() = 0;

    /// @brief Clear screen and dispose any UI elements.
    virtual void clearScreen() = 0;

    /// @brief Show an error message.
    /// @param title Main error information, usually just a UC error code.
    /// @param text Further error information, e.g. ESP IDF error code.
    /// @param fatal A fatal error will remain visible, non-fatal errors will disappear after a timeout.
    virtual void showErrorScreen(std::string title, std::string text, bool fatal) = 0;

    /// @brief Show the animated WiFi connecting screen.
    /// @param title Title message.
    /// @param text Subtitle, e.g. SSID.
    virtual void showWifiConnectingScreen(std::string title, std::string text) = 0;

    /// @brief Remove previous screen and show an icon screen with text information.
    /// @param icon the icon to show
    /// @param title the title in to top row
    /// @param text the text in the bottom row
    virtual void showIconScreen(ui_icon_t icon, std::string title, std::string text) = 0;
};
