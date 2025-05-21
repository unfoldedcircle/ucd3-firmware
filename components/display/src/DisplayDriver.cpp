// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DisplayDriver.h"

#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_eth.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "DisplaySm.h"
#include "board.h"
#include "font/lv_font.h"
#include "lvgl.h"
#include "mem_util.h"
#include "uc_events.h"

ESP_EVENT_DEFINE_BASE(UC_DOCK_EVENTS);

static const char *const TAG = "LCD";

LV_IMG_DECLARE(img_charging)
LV_IMG_DECLARE(img_not_charging)
LV_IMG_DECLARE(img_error)
LV_IMG_DECLARE(img_ethernet)
LV_IMG_DECLARE(img_failed)
LV_IMG_DECLARE(img_ir_learning)
LV_IMG_DECLARE(img_ok)
LV_IMG_DECLARE(img_press)
LV_IMG_DECLARE(img_reset)
LV_IMG_DECLARE(img_setup)
LV_IMG_DECLARE(img_update_failed)
LV_IMG_DECLARE(img_update_ok)
LV_IMG_DECLARE(img_updating)
LV_IMG_DECLARE(img_wifi_error)
LV_IMG_DECLARE(img_wifi)
LV_IMG_DECLARE(img_wifi_fair)
LV_IMG_DECLARE(img_wifi_weak)

// JUST FOR TESTING
static const bool disable_boot_anim = false;

// --- Event queue handling ----
// System events and internal events are queued in a dedicated FreeRTOS queue.
// This allows priority dispatching for internal events like expired timers.
// An LVGL timer polls the queue and dispatches available events to the DisplaySM state machine.
//
// Note: quick and dirty "design", only works because DisplayDriver is a singleton.

static QueueHandle_t display_event_queue;

static DisplaySm displaySm;

/// Internal event queing function. Add the specified event to the front or back of the queue.
static void queue_ui_sm_event(DisplaySm::EventId event, void *event_data, size_t event_data_size,
                              bool priority = false) {
    ui_event_queue_message msg;
    memset(&msg, 0, sizeof(msg));

    msg.event_id = static_cast<uint8_t>(event);
    if (event_data && event_data_size) {
        msg.event_data = malloc(event_data_size);
        if (msg.event_data) {
            memcpy(msg.event_data, event_data, event_data_size);
        } else {
            ESP_LOGE(TAG, "No memory to queue event %d", msg.event_id);
        }
    }

    ESP_LOGI(TAG, "Posting event: %s (priority: %d)", DisplaySm::eventIdToString(event), priority);
    BaseType_t ret;
    if (priority) {
        ret = xQueueSendToFront(display_event_queue, &msg, pdMS_TO_TICKS(1000));
    } else {
        ret = xQueueSendToBack(display_event_queue, &msg, pdMS_TO_TICKS(1000));
    }
    if (ret != pdTRUE) {
        ESP_LOGE(TAG, "Failed to enqueue display event");
    }
}

void trigger_ui_connected_event() {
    bool priority = true;
    queue_ui_sm_event(DisplaySm::EventId::CONNECTED, nullptr, 0, priority);
}

void trigger_ui_timer_event() {
    bool priority = true;
    queue_ui_sm_event(DisplaySm::EventId::TIMER, nullptr, 0, priority);
}

// ----------------------------------------------------------------------------

Display *Display::instance(Config *cfg) {
    static DisplayDriver instance(cfg);
    return &instance;
}

DisplayDriver::DisplayDriver(Config *config)
    : config_(config),
      disp_(nullptr),
      i2c_bus_(nullptr),
      last_active_screen_(-1),
      main_screen_(nullptr),
      error_screen_(false),
      event_queue_timer_(nullptr),
      label_title_(nullptr),
      label_text_(nullptr) {
    assert(config_);

    lv_anim_init(&lv_anim_);
}

DisplayDriver::~DisplayDriver() {
    if (event_queue_timer_) {
        xTimerDelete(event_queue_timer_, portMAX_DELAY);
    }
}

esp_err_t DisplayDriver::init() {
    if (disp_) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initialize I2C bus");

    i2c_master_bus_config_t bus_config = {};

    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.i2c_port = LCD_I2C_BUS_PORT;
    bus_config.sda_io_num = SDA;
    bus_config.scl_io_num = SCL;
    // TODO verify enable_internal_pullup: an IDF warning is printed. -> we have external pullups
    bus_config.flags.enable_internal_pullup = false;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &i2c_bus_), TAG, "Failed to create i2c master bus");

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t     io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = {};
    io_config.dev_addr = LCD_I2C_HW_ADDR;
    io_config.control_phase_bytes = 1;  // According to SSD1306 datasheet
    io_config.lcd_cmd_bits = 8;         // According to SSD1306 datasheet
    io_config.lcd_param_bits = 8;       // According to SSD1306 datasheet
    io_config.dc_bit_offset = 6;        // According to SSD1306 datasheet
    io_config.scl_speed_hz = LCD_PIXEL_CLOCK_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c_v2(i2c_bus_, &io_config, &io_handle), TAG,
                        "Failed to create i2c LCD panel");

    ESP_LOGI(TAG, "Install SSD1306 panel driver");
    esp_lcd_panel_handle_t     panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = LCD_PIN_NUM_RESET;
    panel_config.bits_per_pixel = 1;
    esp_lcd_panel_ssd1306_config_t ssd1306_config = {
        .height = LCD_V_RES,
    };
    panel_config.vendor_config = &ssd1306_config;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle), TAG,
                        "Failed to create SSD1306 panel");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle), TAG, "Failed to reset panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_handle), TAG, "Failed to initialize panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_handle, true), TAG, "Failed to turn on panel");

    ESP_LOGI(TAG, "Initialize LVGL");
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    // lvgl_cfg.task_stack = 9000;  // TESTING ONLY
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "Failed to initialize LVGL");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .control_handle = nullptr,
        .buffer_size = LCD_H_RES * LCD_V_RES,
        .double_buffer = true,
        .trans_size = 0,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = true,
        .rotation =
            {
                .swap_xy = false,
                .mirror_x = false,
                .mirror_y = false,
            },
        .flags =
            {
                .buff_dma = 0,
                .buff_spiram = 0,
                .sw_rotate = 0,
                .full_refresh = 0,
                .direct_mode = 0,
            },
    };
    disp_ = lvgl_port_add_disp(&disp_cfg);

    lv_disp_set_rotation(disp_, LV_DISP_ROT_NONE);  // lvgl 9: LV_DISPLAY_ROTATION_0

    ESP_LOGD(TAG, "Creating message queue");
    display_event_queue = xQueueCreate(6, sizeof(ui_event_queue_message));
    event_queue_timer_ = xTimerCreate("displayEvents", pdMS_TO_TICKS(50), pdTRUE, this, onEventQueueTimer);
    if (event_queue_timer_ == NULL) {
        ESP_LOGE(TAG, "Failed to create display event timer");
        return ESP_ERR_NO_MEM;
    }

    // Register all UC dock events
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(UC_DOCK_EVENTS, ESP_EVENT_ANY_ID, dockEventHandler, this, NULL), TAG,
        "Registering UC_DOCK_EVENTS failed");

    main_screen_ = lv_obj_create(NULL);

    displaySm.setDisplay(this);
    displaySm.start();

    return ESP_OK;
}

esp_err_t DisplayDriver::start() {
    if (!disp_) {
        return ESP_ERR_NOT_ALLOWED;
    }

    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (!lvgl_port_lock(1000)) {
        return ESP_ERR_INVALID_STATE;
    }

    lv_anim_init(&lv_anim_);
    lv_anim_set_var(&lv_anim_, this);
    lv_anim_set_exec_cb(&lv_anim_, setBootScreenCb);
    // v9 feature :-(
    // lv_anim_set_completed_cb(&lv_anim_, setBootAnimCompletedCb);
    lv_anim_set_values(&lv_anim_, 0, 3);
    // v9: lv_anim_set_duration
    lv_anim_set_time(&lv_anim_, 4000);
    // lv_anim_set_delay(&lv_anim_, 600);
    lv_anim_set_repeat_count(&lv_anim_, 0);
    lv_anim_set_user_data(&lv_anim_, this);
    lv_anim_start(&lv_anim_);

    lvgl_port_unlock();

    return ESP_OK;
}

void DisplayDriver::clearScreen() {
    if (!main_screen_) {
        return;
    }

    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "clearScreen LVGL lock failed");
        return;
    }

    previous_screen_.clear();

    if (!error_screen_) {
        ESP_LOGI(TAG, "clearing display");
        lv_obj_clean(main_screen_);
        lv_obj_invalidate(main_screen_);
        lv_refr_now(disp_);
    }

    lvgl_port_unlock();
}

const void *getUiIcon(ui_icon_t icon) {
    switch (icon) {
        case UI_ICON_CHARGING:
            return &img_charging;
        case UI_ICON_NOT_CHARGING:
            return &img_not_charging;
        case UI_ICON_ERROR:
            return &img_error;
        case UI_ICON_ETHERNET:
            return &img_ethernet;
        case UI_ICON_FAILED:
            return &img_failed;
        case UI_ICON_IR_LEARNING:
            return &img_ir_learning;
        case UI_ICON_OK:
            return &img_ok;
        case UI_ICON_PRESS:
            return &img_press;
        case UI_ICON_RESET:
            return &img_reset;
        case UI_ICON_SETUP:
            return &img_setup;
        case UI_ICON_OTA_FAILED:
            return &img_update_failed;
        case UI_ICON_OTA_OK:
            return &img_update_ok;
        case UI_ICON_OTA:
            return &img_updating;
        case UI_ICON_WIFI_ERROR:
            return &img_wifi_error;
        case UI_ICON_WIFI:
            return &img_wifi;
        case UI_ICON_WIFI_FAIR:
            return &img_wifi_fair;
        case UI_ICON_WIFI_WEAK:
            return &img_wifi_weak;
        default:
            return nullptr;
    }
}

void DisplayDriver::showErrorScreen(std::string title, std::string text, bool fatal) {
    if (!main_screen_) {
        return;
    }

    if (error_screen_) {
        ESP_LOGW(TAG, "Old error still displaying: ignoring new error. TODO queue / alterate error display");
        return;
    }
    error_screen_ = true;

    if (boot_up_) {
        ESP_LOGW(TAG, "TODO handle error screen during bootup animation!");
    }

    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "LVGL lock failed");
        error_screen_ = false;
        return;
    }

    ESP_LOGI(TAG, "showing %serror screen: %s / %s", fatal ? "fatal " : "", title.c_str(), text.c_str());

    lv_obj_clean(main_screen_);
    createIconScreen(main_screen_, UI_ICON_ERROR, title, text);

    // start timer to automatically dismiss non-fatal error
    if (!fatal) {
        ESP_LOGI(TAG, "Starting error screen timer with timeout 5s");
        TimerHandle_t timer = xTimerCreate("uiErrScreen", pdMS_TO_TICKS(5000), pdFALSE, this, onClearErrorScreenTimer);
        if (timer == NULL) {
            ESP_LOGE(TAG, "Failed to create error screen timer");
        } else {
            if (xTimerStart(timer, pdMS_TO_TICKS(1000)) != pdPASS) {
                ESP_LOGE(TAG, "Could not start error screen timer");
            }
        }
    }

    lvgl_port_unlock();
}

void DisplayDriver::showIconScreen(ui_icon_t icon, std::string title, std::string text) {
    if (!main_screen_) {
        return;
    }

    previous_screen_ = {icon, title, text};

    if (error_screen_) {
        ESP_LOGI(TAG, "error screen loaded: NOT showing icon screen %d: %s / %s", icon, title.c_str(), text.c_str());
        return;
    }

    ESP_LOGI(TAG, "showing icon screen %d: %s / %s", icon, title.c_str(), text.c_str());
    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "LVGL lock failed for showIconScreen");
        return;
    }

    lv_obj_clean(main_screen_);
    createIconScreen(main_screen_, icon, title, text);

    lvgl_port_unlock();
}

void DisplayDriver::showWifiConnectingScreen(std::string title, std::string text) {
    if (!main_screen_) {
        return;
    }

    static const lv_img_dsc_t *anim_imgs[3] = {
        &img_wifi_weak,
        &img_wifi_fair,
        &img_wifi,
    };

    if (error_screen_) {
        ESP_LOGI(TAG, "error screen loaded: NOT showing wifi connecting screen: %s / %s", title.c_str(), text.c_str());
        return;
    }

    ESP_LOGI(TAG, "showing wifi connecting screen: %s / %s", title.c_str(), text.c_str());

    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "LVGL lock failed for showWifiConnectingScreen");
        return;
    }

    lv_obj_clean(main_screen_);
    createIconScreen(main_screen_, anim_imgs, 3, 1000, title, text);

    lvgl_port_unlock();
}

void DisplayDriver::createIconScreen(lv_obj_t *parent, const lv_img_dsc_t *anim_imgs[], uint8_t num, uint32_t duration,
                                     const std::string &titleText, const std::string &bottomText) {
    lv_obj_t *animimg = lv_animimg_create(parent);
    lv_obj_remove_style_all(animimg);
    lv_obj_center(animimg);
    lv_animimg_set_src(animimg, (const void **)anim_imgs, num);
    lv_animimg_set_duration(animimg, duration);
    lv_animimg_set_repeat_count(animimg, LV_ANIM_REPEAT_INFINITE);
    lv_animimg_start(animimg);
    lv_obj_set_style_pad_right(animimg, 4, 0);

    createIconScreen(parent, animimg, titleText, bottomText);
}

void DisplayDriver::createIconScreen(lv_obj_t *parent, ui_icon_t uiIcon, const std::string &titleText,
                                     const std::string &bottomText) {
    lv_obj_t   *icon = nullptr;
    const void *img_src = getUiIcon(uiIcon);
    if (img_src) {
        icon = lv_img_create(parent);
        lv_obj_remove_style_all(icon);
        lv_img_set_src(icon, img_src);
        lv_obj_set_style_pad_right(icon, 4, 0);
    }

    createIconScreen(parent, icon, titleText, bottomText);
}

void DisplayDriver::createIconScreen(lv_obj_t *parent, lv_obj_t *img_obj, const std::string &titleText,
                                     const std::string &bottomText) {
    // container next to icon
    lv_obj_t *text = lv_obj_create(parent);
    // remove paddings, round edges, ...
    lv_obj_remove_style_all(text);

    // Important: height needs to be set, i.e. limited to parent's height, otherwise it could overflow
    lv_obj_set_height(text, lv_pct(100));

    lv_obj_t *title = lv_label_create(text);
    // easiest way to get rid of all padding
    lv_obj_remove_style_all(title);

    lv_obj_set_style_text_font(title, bottomText.empty() ? &lv_font_montserrat_32 : &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(title, bottomText.empty() ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
    lv_label_set_text(title, titleText.c_str());
    lv_obj_set_width(title, lv_pct(100));

    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *label = nullptr;
    if (!bottomText.empty()) {
        lv_obj_t *label = lv_label_create(text);
        lv_obj_remove_style_all(label);

        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(label, bottomText.c_str());
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }

    // FIXME using a static style as suggested in the docs doesn't work! ESP panics without stacktrace after ~64 screen
    // changes! remove padding from flex layout (only shown in Simulator, not on device)

    // static lv_style_t style;
    // lv_style_init(&style);
    // lv_style_set_pad_column(&style, 0);
    // lv_obj_add_style(parent, &style, 0);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    if (img_obj) {
        lv_obj_set_flex_grow(img_obj, 0);
    }
    lv_obj_set_flex_grow(text, 1);

    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    if (label) {
        lv_obj_set_flex_grow(title, 7);
        lv_obj_set_flex_grow(label, 9);
    }
}

lv_obj_t *DisplayDriver::createTextScreen(lv_obj_t *parent, const std::string &text) {
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);

    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text.c_str());
    lv_obj_set_width(label, disp_->driver->hor_res);   // lvgl 9: disp->hor_res
    lv_obj_set_height(label, disp_->driver->ver_res);  // lvgl 9: disp->hor_res
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);

    return label;
}

void DisplayDriver::restorePreviousScreen() {
    if (!previous_screen_.empty()) {
        ESP_LOGI(TAG, "Restoring previous info screen");
        showIconScreen(previous_screen_.info_icon, previous_screen_.title(), previous_screen_.text());
    } else {
        clearScreen();
    }
}

void DisplayDriver::dockEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    DisplayDriver *that = static_cast<DisplayDriver *>(arg);
    LV_ASSERT(that);

    ESP_LOGI(TAG, "%s:%s: dockEventHandler", event_base, uc_event_id_to_string(static_cast<uc_event_id_t>(event_id)));

    DisplaySm::EventId eventId;  // = DisplaySm::EventIdCount;
    size_t             event_data_size = 0;
    switch (event_id) {
        case UC_EVENT_BUTTON_CLICK:
        case UC_EVENT_BUTTON_DOUBLE_CLICK:  // TODO add double click SM event when required
            eventId = DisplaySm::EventId::BUTTON_CLICK;
            break;
        case UC_EVENT_BUTTON_LONG_PRESS_START:
            event_data_size = sizeof(uc_event_button_long_t);
            eventId = DisplaySm::EventId::BUTTON_LONG_PRESS_START;
            break;
        case UC_EVENT_BUTTON_LONG_PRESS_UP:
            event_data_size = sizeof(uc_event_button_long_t);
            eventId = DisplaySm::EventId::BUTTON_LONG_PRESS_UP;
            break;
        case UC_ACTION_RESET:
            eventId = DisplaySm::EventId::FACTORY_RESET;
            break;
        case UC_EVENT_IMPROV_START:
            eventId = DisplaySm::EventId::IMPROV_START;
            break;
        case UC_EVENT_IMPROV_AUTH_REQUIRED:
            eventId = DisplaySm::EventId::IMPROV_AUTH_REQUIRED;
            break;
        case UC_EVENT_IMPROV_AUTHORIZED:
            eventId = DisplaySm::EventId::IMPROV_AUTHORIZED;
            break;
        case UC_EVENT_IMPROV_PROVISIONING:
            eventId = DisplaySm::EventId::IMPROV_PROVISIONING;
            event_data_size = sizeof(uc_event_network_state_t);
            break;
        case UC_EVENT_IMPROV_END:
            eventId = DisplaySm::EventId::IMPROV_END;
            break;
        case UC_EVENT_ERROR:
            eventId = DisplaySm::EventId::ERROR;
            event_data_size = sizeof(uc_event_error_t);
            break;
        case UC_EVENT_CONNECTING:
            eventId = DisplaySm::EventId::CONNECTING;
            event_data_size = sizeof(uc_event_network_state_t);
            break;
        case UC_EVENT_CONNECTED:
            eventId = DisplaySm::EventId::CONNECTED;
            event_data_size = sizeof(uc_event_network_state_t);
            break;
        case UC_EVENT_DISCONNECTED:
            eventId = DisplaySm::EventId::LOST_CONNECTION;
            event_data_size = sizeof(uc_event_network_state_t);
            break;
        case UC_EVENT_CHARGING_ON:
            eventId = DisplaySm::EventId::CHARGING_ON;
            break;
        case UC_EVENT_CHARGING_OFF:
            eventId = DisplaySm::EventId::CHARGING_OFF;
            break;
        // case UC_EVENT_OVER_CURRENT:  // translate to error?
        //     eventId = DisplaySm::EventId::;
        //     break;
        case UC_ACTION_IDENTIFY:
            eventId = DisplaySm::EventId::IDENTIFY;
            break;
        case UC_EVENT_IR_LEARNING_START:
            eventId = DisplaySm::EventId::IR_LEARNING_START;
            break;
        case UC_EVENT_IR_LEARNING_OK:
            eventId = DisplaySm::EventId::IR_LEARNING_OK;
            event_data_size = sizeof(uc_event_ir_t);
            break;
        case UC_EVENT_IR_LEARNING_FAIL:
            eventId = DisplaySm::EventId::IR_LEARNING_FAILED;
            event_data_size = sizeof(uc_event_ir_t);
            break;
        case UC_EVENT_IR_LEARNING_STOP:
            eventId = DisplaySm::EventId::IR_LEARNING_STOP;
            break;
        case UC_EVENT_OTA_START:
            eventId = DisplaySm::EventId::OTA_START;
            break;
        case UC_EVENT_OTA_PROGRESS:
            eventId = DisplaySm::EventId::OTA_PROGRESS;
            event_data_size = sizeof(uc_event_ota_progress_t);
            break;
        case UC_EVENT_OTA_SUCCESS:
            eventId = DisplaySm::EventId::OTA_SUCCESS;
            break;
        case UC_EVENT_OTA_FAIL:
            eventId = DisplaySm::EventId::OTA_FAIL;
            break;
        case UC_EVENT_REBOOT:
            eventId = DisplaySm::EventId::REBOOT;
            break;
        case UC_EVENT_EXT_PORT_MODE:
            eventId = DisplaySm::EventId::EXT_PORT_MODE;
            event_data_size = sizeof(uc_event_ext_port_mode_t);
            break;
        default:
            ESP_LOGW(TAG, "%s:%ld: ignoring invalid or not yet implemented event", event_base, event_id);
            return;
    }

    queue_ui_sm_event(eventId, event_data, event_data_size);
}

void DisplayDriver::onEventQueueTimer(TimerHandle_t timer) {
    DisplayDriver *that = static_cast<DisplayDriver *>(pvTimerGetTimerID(timer));
    LV_ASSERT(that);

    // periodically trigger state-machine, this handles auto-transistions and timeouts
    displaySm.dispatchEvent(DisplaySm::EventId::DO);

    // check for new event
    ui_event_queue_message msg;
    BaseType_t             xStatus = xQueueReceive(display_event_queue, &msg, 0);
    if (xStatus != pdTRUE) {
        return;
    }

    // and dispatch to state-machine
    auto oldState = displaySm.stateId;

    if (msg.event_id >= DisplaySm::EventIdCount) {
        ESP_LOGE(TAG, "Invalid event: %u", msg.event_id);
        FREE_AND_NULL(msg.event_data);
        return;
    }
    DisplaySm::EventId eventId = static_cast<DisplaySm::EventId>(msg.event_id);

    ESP_LOGI(TAG, "Dispatching event: %s => %s", DisplaySm::eventIdToString(eventId),
             DisplaySm::stateIdToString(displaySm.stateId));
    displaySm.setEventParameters(std::make_unique<EventParameter>(&msg));

    switch (eventId) {
        case DisplaySm::EventId::IMPROV_PROVISIONING:
        case DisplaySm::EventId::CONNECTING:
        case DisplaySm::EventId::CONNECTED:
        case DisplaySm::EventId::LOST_CONNECTION: {
            uc_event_network_state_t *net_state = static_cast<uc_event_network_state_t *>(msg.event_data);
            displaySm.setNetworkInfo(net_state);
            break;
        }
        default: {
            // ignore
        }
    }

    displaySm.dispatchEvent(eventId);

    FREE_AND_NULL(msg.event_data);

    auto newState = displaySm.stateId;
    ESP_LOGI(TAG, "UI SM transition: %s -> %s", DisplaySm::stateIdToString(oldState),
             DisplaySm::stateIdToString(newState));
}

void DisplayDriver::onClearErrorScreenTimer(TimerHandle_t timer) {
    ESP_LOGI(TAG, "Error screen timer expired");
    DisplayDriver *that = static_cast<DisplayDriver *>(pvTimerGetTimerID(timer));
    LV_ASSERT(that);

    xTimerDelete(timer, portMAX_DELAY);

    that->error_screen_ = false;
    that->restorePreviousScreen();
}

void DisplayDriver::setBootScreenCb(void *display, int32_t screen_nbr) {
    DisplayDriver *that = static_cast<DisplayDriver *>(display);
    LV_ASSERT(that);

    ESP_LOGI(TAG, "Boot screen: %ld -> %ld", that->last_active_screen_, screen_nbr);

    if (screen_nbr == that->last_active_screen_) {
        return;
    }
    that->last_active_screen_ = screen_nbr;

    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "LVGL lock failed for setBootScreenCb");
        return;
    }

    if (disable_boot_anim) {
        lv_scr_load(that->main_screen_);
        lvgl_port_unlock();
        DisplayDriver::setBootAnimCompletedCb(&that->lv_anim_);
        return;
    }

    if (!that->label_title_) {
        that->label_title_ = lv_label_create(that->main_screen_);

        lv_obj_set_style_text_font(that->label_title_, &lv_font_montserrat_14, 0);

        lv_label_set_long_mode(that->label_title_, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(that->label_title_, that->disp_->driver->hor_res);  // lvgl 9: disp->hor_res
        lv_obj_set_style_text_align(that->label_title_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(that->label_title_, LV_ALIGN_TOP_MID, 0, 0);

        that->label_text_ = lv_label_create(that->main_screen_);
        lv_obj_set_style_text_font(that->label_text_, &lv_font_montserrat_18, 0);
        lv_label_set_long_mode(that->label_text_, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(that->label_text_, that->disp_->driver->hor_res);  // lvgl 9: disp->hor_res
        lv_obj_set_style_text_align(that->label_text_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(that->label_text_, LV_ALIGN_BOTTOM_MID, 0, 0);

        lv_scr_load(that->main_screen_);
    }

    switch (screen_nbr) {
        case 0:
            lv_label_set_text(that->label_title_, "model");
            lv_label_set_text(that->label_text_, that->config_->getModel());
            break;
        case 1:
            lv_label_set_text(that->label_title_, "s/n");
            lv_label_set_text(that->label_text_, that->config_->getSerial());
            break;
        case 2:
            lv_label_set_text(that->label_title_, "hostname");
            lv_label_set_text(that->label_text_, that->config_->getHostName());
            break;
        case 3:
            lv_obj_clean(that->main_screen_);
            lv_obj_invalidate(that->main_screen_);
            that->label_title_ = nullptr;
            that->label_text_ = nullptr;
            // release early, don't hold lock while calling setBootAnimCompletedCb
            lvgl_port_unlock();
            DisplayDriver::setBootAnimCompletedCb(&that->lv_anim_);
            return;
        default:
            ESP_LOGW(TAG, "Ignoring invalid boot up screen number: %ld", screen_nbr);
    }

    lvgl_port_unlock();
}

void DisplayDriver::setBootAnimCompletedCb(lv_anim_t *anim) {
    ESP_LOGI(TAG, "Boot animation completed");

    DisplayDriver *that = static_cast<DisplayDriver *>(anim->user_data);
    LV_ASSERT(that);

    that->boot_up_ = false;

    // now start triggering the state machine event queue dispatching
    if (xTimerStart(that->event_queue_timer_, pdMS_TO_TICKS(1000)) != pdPASS) {
        ESP_LOGE(TAG, "Could not start event queue timer");
    }
}
