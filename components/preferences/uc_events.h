// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_event_base.h"
#include "esp_netif_ip_addr.h"

#include "ext_port_mode.h"
#include "uc_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UC Dock events used to trigger actions like showing UI screens.
 */
typedef enum {
    // TODO UC_ACTION_REBOOT ?,
    /// Request a factory reset. This is **not** an event, but an action request to perform a reset!
    UC_ACTION_RESET = 1,
    /// Device identification request.
    UC_ACTION_IDENTIFY,
    /// Single button click event. Used to approve improv-wifi (if active), or to iterate through information screens.
    UC_EVENT_BUTTON_CLICK,
    /// Double button click event. Not used at the moment - mapped to UC_EVENT_BUTTON_CLICK
    UC_EVENT_BUTTON_DOUBLE_CLICK,
    /// Long button press start event. Uses event payload `uc_event_button_long_t`.
    UC_EVENT_BUTTON_LONG_PRESS_START,
    /// Long button press release event. Uses event payload `uc_event_button_long_t`.
    UC_EVENT_BUTTON_LONG_PRESS_UP,
    /// improv-wifi event: started, or BLE disconnected
    UC_EVENT_IMPROV_START,
    /// improv-wifi event: BLE connected, authorization required
    UC_EVENT_IMPROV_AUTH_REQUIRED,
    /// improv-wifi event: authorized by user, waiting for wifi provisioning data
    UC_EVENT_IMPROV_AUTHORIZED,
    /// improv-wifi event: wifi provisioning active
    UC_EVENT_IMPROV_PROVISIONING,
    /// improv-wifi event: wifi successfully provisioned
    UC_EVENT_IMPROV_END,
    /// Error condition, e.g. device initialization failed
    UC_EVENT_ERROR,
    // --- Network events, emitted by the network state-machine
    UC_EVENT_CONNECTING,    // Start connecting to ethernet or a WiFi AP. Uses event payload `uc_event_network_state_t`.
    UC_EVENT_CONNECTED,     // ETH or WiFi connected. Uses event payload `uc_event_network_state_t`.
    UC_EVENT_DISCONNECTED,  // previously active ETH or WiFi connection is disconnected. Uses event payload
                            // `uc_event_network_state_t`.
    /// Remote device charging started event.
    UC_EVENT_CHARGING_ON,
    /// Remote device charging stopped event.
    UC_EVENT_CHARGING_OFF,
    UC_EVENT_OVER_CURRENT,  // TODO not yet fully implemented
    /// IR learning mode started event.
    UC_EVENT_IR_LEARNING_START,
    /// Sucessfully learned an IR code event. Uses event payload `uc_event_ir_t`.
    UC_EVENT_IR_LEARNING_OK,
    /// Failed to learn / decode an IR code event.
    UC_EVENT_IR_LEARNING_FAIL,  // TODO not yet fully implemented!
    /// IR learning mode stopped event.
    UC_EVENT_IR_LEARNING_STOP,
    /// Firmware update started event.
    UC_EVENT_OTA_START,
    /// Firmware update progress event.  Uses event payload `uc_event_ota_progress_t`.
    UC_EVENT_OTA_PROGRESS,
    /// Firmware update successfully completed event. Device will auto-restart soon after this event is emitted!
    UC_EVENT_OTA_SUCCESS,
    /// Firmware update failed event.
    UC_EVENT_OTA_FAIL,
    /// Device is rebooting within the next second event notification.
    UC_EVENT_REBOOT,
    /// External port configuration mode change event. Uses event payload `uc_event_ext_port_mode_t`.
    UC_EVENT_EXT_PORT_MODE,
} uc_event_id_t;

const char* uc_event_id_to_string(uc_event_id_t event_id);

/// @brief Event structure for UC_EVENT_BUTTON_LONG_PRESS_UP
typedef struct {
    /// Button holding time in ms
    uint16_t holdtime;
} uc_event_button_long_t;

/// @brief Event structure for UC_EVENT_ERROR
typedef struct {
    /// UC error code
    uc_errors_t error;
    /// optional ESP error code
    esp_err_t esp_err;
    /// Fatal error: dock operation not possible, further processing stopped. Reboot required.
    bool fatal;
} uc_event_error_t;

typedef enum {
    WIFI = 2,
    ETHERNET = 3,
} network_kind_t;

typedef struct {
    network_kind_t connection;
    bool           eth_link;
    // SSID of AP
    uint8_t ssid[33];
    // Signal strength of AP. Set to 0x00 if ethernet is connected.
    // Note that in some rare cases where signal strength is very strong, rssi values can be slightly positive
    int8_t        rssi;
    esp_ip_addr_t ip;
} uc_event_network_state_t;

typedef struct {
    /// UC error code
    uc_errors_t error;
    int16_t     decode_type;  // NEC, SONY, RC5, UNKNOWN. decode_type_t from IRremoteESP8266
    uint64_t    value;        // Decoded value
    uint32_t    address;      // Decoded device address.
    uint32_t    command;      // Decoded command.
} uc_event_ir_t;

/// @brief Event structure for UC_EVENT_OTA_PROGRESS
typedef struct {
    uint8_t percent;
} uc_event_ota_progress_t;

/// @brief Event structure for UC_EVENT_EXT_PORT_MODE
typedef struct {
    uint8_t         port;
    ext_port_mode_t mode;
    ext_port_mode_t active_mode;
    esp_err_t       state;
    // UART configuraiton in format "$BAUDRATE:$DATA_BITS$PARITY$STOP_BITS"
    char uart_cfg[16];
} uc_event_ext_port_mode_t;

ESP_EVENT_DECLARE_BASE(UC_DOCK_EVENTS);

void uc_fatal_error_check(esp_err_t ret, uc_errors_t uc_error);

void uc_error_check(esp_err_t ret, uc_errors_t uc_error);

#ifdef __cplusplus
}
#endif
