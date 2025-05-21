// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "driver/i2c_master.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"

#include "display.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Display state machine event queue message.
struct ui_event_queue_message {
    /// DisplaySM::EventId number
    uint8_t event_id;
    /// Related data structure of the `event`. One of the defined `uc_event_xx_t` structures in uc_events.h.
    /// This data must be released by the state machine once it's no longer used!
    void* event_data;
};

// --- internal events --------------------------------------------------------

void trigger_ui_connected_event();

/// @brief Timer expired.
void trigger_ui_timer_event();

struct ScreenData {
    ui_icon_t   info_icon = ui_icon_t::UI_ICON_NONE;
    std::string info_title;
    std::string info_text;

    std::string title() { return info_title; }

    std::string text() { return info_text; }

    void clear() {
        info_icon = ui_icon_t::UI_ICON_NONE;
        info_title.clear();
        info_text.clear();
    }

    bool empty() { return info_icon == ui_icon_t::UI_ICON_NONE; }
};

/// @brief LCD driver class controlling the display.
class DisplayDriver : public Display {
 public:
    explicit DisplayDriver(Config* config);
    virtual ~DisplayDriver();

    virtual esp_err_t init();
    virtual esp_err_t start();

    virtual void clearScreen();
    virtual void showErrorScreen(std::string title, std::string text, bool fatal);
    virtual void showIconScreen(ui_icon_t icon, std::string title, std::string text);
    virtual void showWifiConnectingScreen(std::string title, std::string text);

 private:
    /// @brief Create an animated icon screen.
    /// Attention: LVGL mutex must be controlled by caller!
    /// @param parent parent of the icon screen elements
    /// @param anim_imgs pointer to a series of images
    /// @param num number of images
    /// @param duration image animation duration time in ms
    /// @param titleText title text next to the icon
    /// @param bottomText text next to the icon below the title
    void createIconScreen(lv_obj_t* parent, const lv_img_dsc_t* anim_imgs[], uint8_t num, uint32_t duration,
                          const std::string& titleText, const std::string& bottomText);

    /// @brief Create an icon screen.
    /// Attention: LVGL mutex must be controlled by caller!
    /// @param parent parent of the icon screen elements
    /// @param uiIcon icon identifier
    /// @param titleText title text next to the icon
    /// @param bottomText text next to the icon below the title
    void createIconScreen(lv_obj_t* parent, ui_icon_t uiIcon, const std::string& titleText,
                          const std::string& bottomText);

    /// @brief Create an icon screen with a pre-defined image object.
    /// Attention: LVGL mutex must be controlled by caller!
    /// @param parent parent of the icon screen elements
    /// @param img_obj icon image
    /// @param titleText title text next to the icon
    /// @param bottomText text next to the icon below the title
    void createIconScreen(lv_obj_t* parent, lv_obj_t* img_obj, const std::string& titleText,
                          const std::string& bottomText);

    /// Attention: LVGL mutex must be controlled by caller!
    lv_obj_t* createTextScreen(lv_obj_t* parent, const std::string& text);

    /// @brief Restore the previous info screen. Called after an error screen expires.
    void restorePreviousScreen();

    static void dockEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

    /// @brief Callback to poll display event queue and to drive the display event machine for new events.
    /// @param timer FreeRTOS timer
    static void onEventQueueTimer(TimerHandle_t timer);

    /// @brief Callback for error screen timeout. Clears the error and restores the previous screen.
    /// @param timer FreeRTOS timer
    static void onClearErrorScreenTimer(TimerHandle_t timer);

    /// @brief LVGL boot animation screen callback.
    /// @param display Pointer to Display instance.
    /// @param screen Screen number to show.
    static void setBootScreenCb(void* display, int32_t screen_nbr);

    /// @brief Completion callback of the LVGL boot animation.
    /// @param anim LVGL boot animation.
    static void setBootAnimCompletedCb(lv_anim_t* anim);

 private:
    Config*       config_;
    lv_display_t* disp_;

    i2c_master_bus_handle_t i2c_bus_;

    /// flag indicating that the bootup animation is running
    bool    boot_up_;
    int32_t last_active_screen_;
    /// reusable animation
    lv_anim_t lv_anim_;
    lv_obj_t* main_screen_;

    bool       error_screen_;
    ScreenData previous_screen_;

    TimerHandle_t event_queue_timer_;

    /// boot animation label title
    lv_obj_t* label_title_;
    /// boot animation label text
    lv_obj_t* label_text_;
};

#ifdef __cplusplus
}
#endif
