// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EventParameter.h"

#include "DisplaySm.h"
#include "IRutils.h"
#include "ext_port_mode.h"

EventParameter::EventParameter(ui_event_queue_message* event) : icon_(UI_ICON_NONE), value_(0), fatal_error_(false) {
    DisplaySm::EventId eventId = static_cast<DisplaySm::EventId>(event->event_id);

    switch (eventId) {
        case DisplaySm::EventId::DO:
            // ignore, TODO clean previous parameter?
            break;
        case DisplaySm::EventId::BUTTON_CLICK:
            break;
        case DisplaySm::EventId::CHARGING_OFF:
            icon_ = UI_ICON_NOT_CHARGING;
            break;
        case DisplaySm::EventId::CHARGING_ON:
            icon_ = UI_ICON_CHARGING;
            break;
        case DisplaySm::EventId::FACTORY_RESET:
            break;
        case DisplaySm::EventId::ERROR: {
            icon_ = UI_ICON_ERROR;
            uc_event_error_t* event_error = static_cast<uc_event_error_t*>(event->event_data);
            if (event_error) {
                title_ = std::to_string(event_error->error);
                if (event_error->esp_err != ESP_FAIL) {
                    message_ = std::to_string(event_error->esp_err);
                }
                fatal_error_ = event_error->fatal;
            }
            break;
        }
        case DisplaySm::EventId::CONNECTING: {
            uc_event_network_state_t* event_net_state = static_cast<uc_event_network_state_t*>(event->event_data);
            if (event_net_state) {
                icon_ = event_net_state->connection == ETHERNET ? UI_ICON_ETHERNET : UI_ICON_WIFI;
                title_ = event_net_state->connection == ETHERNET ? "ETH" : "WiFi";
                message_ = "mode";
                value_ = event_net_state->connection;
            }
            break;
        }
        case DisplaySm::EventId::CONNECTED: {
            uc_event_network_state_t* event_net_state = static_cast<uc_event_network_state_t*>(event->event_data);
            if (event_net_state) {
                char buf[64];
                if (event_net_state->ip.type == ESP_IPADDR_TYPE_V4) {
                    snprintf(buf, sizeof(buf), IPSTR, IP2STR(&event_net_state->ip.u_addr.ip4));
                } else {
                    snprintf(buf, sizeof(buf), IPV6STR, IPV62STR(event_net_state->ip.u_addr.ip6));
                }
                icon_ = event_net_state->connection == ETHERNET ? UI_ICON_ETHERNET : UI_ICON_WIFI;
                title_ = "DHCP";
                message_ = buf;
                value_ = event_net_state->connection;
            }
            break;
        }
        case DisplaySm::EventId::LOST_CONNECTION: {
            icon_ = UI_ICON_WIFI_ERROR;
            uc_event_network_state_t* event_net_state = static_cast<uc_event_network_state_t*>(event->event_data);
            if (event_net_state) {
                icon_ = UI_ICON_WIFI_ERROR;
                value_ = event_net_state->connection;
            }
            break;
        }
        case DisplaySm::EventId::BUTTON_LONG_PRESS_START: {
            uc_event_button_long_t* event_btn = static_cast<uc_event_button_long_t*>(event->event_data);
            icon_ = UI_ICON_RESET;
            value_ = event_btn->holdtime;
            break;
        }
        case DisplaySm::EventId::BUTTON_LONG_PRESS_UP: {
            uc_event_button_long_t* event_btn = static_cast<uc_event_button_long_t*>(event->event_data);
            value_ = event_btn->holdtime;
            break;
        }
        case DisplaySm::EventId::IDENTIFY:
            // TODO dedicated icon
            icon_ = UI_ICON_OK;
            break;
        case DisplaySm::EventId::IMPROV_START:
            break;
        case DisplaySm::EventId::IMPROV_AUTH_REQUIRED:
            icon_ = UI_ICON_PRESS;
            break;
        case DisplaySm::EventId::IMPROV_AUTHORIZED:
            break;
        case DisplaySm::EventId::IMPROV_PROVISIONING:
            break;
        case DisplaySm::EventId::IMPROV_END:
            break;
        case DisplaySm::EventId::IR_LEARNING_FAILED: {
            icon_ = UI_ICON_FAILED;
            uc_event_ir_t* event_ir = static_cast<uc_event_ir_t*>(event->event_data);
            if (event_ir) {
                switch (event_ir->error) {
                    case UC_ERROR_IR_LEARN_UNKNOWN:
                        message_ = "UNKNOWN";
                        break;
                    case UC_ERROR_IR_LEARN_INVALID:
                        message_ = "INVALID";
                        break;
                    case UC_ERROR_IR_LEARN_OVERFLOW:
                        message_ = "OVERFLOW";
                        break;
                    default:
                        message_ = "FAILED";
                }
            }
            break;
        }
        case DisplaySm::EventId::IR_LEARNING_OK: {
            icon_ = UI_ICON_OK;
            uc_event_ir_t* event_ir = static_cast<uc_event_ir_t*>(event->event_data);
            if (event_ir) {
                message_ = typeToString(static_cast<decode_type_t>(event_ir->decode_type));
            }
            break;
        }
        case DisplaySm::EventId::IR_LEARNING_START:
            icon_ = UI_ICON_IR_LEARNING;
            break;
        case DisplaySm::EventId::IR_LEARNING_STOP:
            break;
        case DisplaySm::EventId::OTA_FAIL:
            icon_ = UI_ICON_OTA_FAILED;
            break;
        case DisplaySm::EventId::OTA_PROGRESS: {
            icon_ = UI_ICON_OTA;
            uc_event_ota_progress_t* event_ota = static_cast<uc_event_ota_progress_t*>(event->event_data);
            if (event_ota) {
                value_ = event_ota->percent;
                title_ = std::to_string(event_ota->percent);
                title_ += "%";
            }
            break;
        }
        case DisplaySm::EventId::OTA_START:
            icon_ = UI_ICON_OTA;
            break;
        case DisplaySm::EventId::OTA_SUCCESS:
            icon_ = UI_ICON_OTA_OK;
            break;
        case DisplaySm::EventId::TIMER:
            break;
        case DisplaySm::EventId::REBOOT:
            break;
        case DisplaySm::EventId::EXT_PORT_MODE: {
            char buff[20];

            uc_event_ext_port_mode_t* port_mode = static_cast<uc_event_ext_port_mode_t*>(event->event_data);
            value_ = port_mode->port;
            snprintf(buff, sizeof(buff), "Port %d%s", port_mode->port,
                     port_mode->mode == ExtPortMode::AUTO ? " (auto)" : "");
            title_ = buff;

            if (port_mode->state == ESP_OK) {
                icon_ = UI_ICON_OK;
                message_ = ExtPortMode_to_friendly_str(port_mode->active_mode);
                if (port_mode->active_mode == ExtPortMode::RS232) {
                    message_ += " ";
                    message_ += port_mode->uart_cfg;
                }

            } else {
                icon_ = UI_ICON_ERROR;
                message_ = "ERROR ";
                message_ += ExtPortMode_to_friendly_str(port_mode->mode);
            }
            break;
        }
    }
}
