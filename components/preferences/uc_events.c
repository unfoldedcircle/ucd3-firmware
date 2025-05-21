// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "uc_events.h"

#include "esp_check.h"
#include "esp_event.h"

const char* uc_event_id_to_string(uc_event_id_t event_id) {
    switch (event_id) {
        case UC_ACTION_RESET:
            return "RESET";
        case UC_ACTION_IDENTIFY:
            return "IDENTIFY";
        case UC_EVENT_BUTTON_CLICK:
            return "BUTTON_CLICK";
        case UC_EVENT_BUTTON_DOUBLE_CLICK:
            return "BUTTON_DOUBLE_CLICK";
        case UC_EVENT_BUTTON_LONG_PRESS_START:
            return "BUTTON_LONG_PRESS_START";
        case UC_EVENT_BUTTON_LONG_PRESS_UP:
            return "BUTTON_LONG_PRESS_UP";
        case UC_EVENT_IMPROV_START:
            return "IMPROV_START";
        case UC_EVENT_IMPROV_AUTH_REQUIRED:
            return "IMPROV_AUTH_REQUIRED";
        case UC_EVENT_IMPROV_AUTHORIZED:
            return "IMPROV_AUTHORIZED";
        case UC_EVENT_IMPROV_PROVISIONING:
            return "IMPROV_PROVISIONING";
        case UC_EVENT_IMPROV_END:
            return "IMPROV_END";
        case UC_EVENT_ERROR:
            return "ERROR";
        case UC_EVENT_CONNECTING:
            return "CONNECTING";
        case UC_EVENT_CONNECTED:
            return "CONNECTED";
        case UC_EVENT_DISCONNECTED:
            return "DISCONNECTED";
        case UC_EVENT_CHARGING_ON:
            return "CHARGING_ON";
        case UC_EVENT_CHARGING_OFF:
            return "CHARGING_OFF";
        case UC_EVENT_OVER_CURRENT:
            return "OVER_CURRENT";
        case UC_EVENT_IR_LEARNING_START:
            return "IR_LEARNING_START";
        case UC_EVENT_IR_LEARNING_OK:
            return "IR_LEARNING_OK";
        case UC_EVENT_IR_LEARNING_FAIL:
            return "IR_LEARNING_FAIL";
        case UC_EVENT_IR_LEARNING_STOP:
            return "IR_LEARNING_STOP";
        case UC_EVENT_OTA_START:
            return "OTA_START";
        case UC_EVENT_OTA_PROGRESS:
            return "OTA_PROGRESS";
        case UC_EVENT_OTA_SUCCESS:
            return "OTA_SUCCESS";
        case UC_EVENT_OTA_FAIL:
            return "OTA_FAIL";
        case UC_EVENT_REBOOT:
            return "REBOOT";
        case UC_EVENT_EXT_PORT_MODE:
            return "EXT_PORT_MODE";
    }
    return "?";
}

void uc_fatal_error_check(esp_err_t ret, uc_errors_t uc_error) {
    if (ret != ESP_OK) {
        uc_event_error_t event = {.error = uc_error, .esp_err = ret, .fatal = true};
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            esp_event_post(UC_DOCK_EVENTS, UC_EVENT_ERROR, &event, sizeof(event), pdMS_TO_TICKS(200)));
        // TODO abort & reboot?
    }
}

void uc_error_check(esp_err_t ret, uc_errors_t uc_error) {
    if (ret != ESP_OK) {
        uc_event_error_t event = {.error = uc_error, .esp_err = ret, .fatal = false};
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            esp_event_post(UC_DOCK_EVENTS, UC_EVENT_ERROR, &event, sizeof(event), pdMS_TO_TICKS(200)));
    }
}
