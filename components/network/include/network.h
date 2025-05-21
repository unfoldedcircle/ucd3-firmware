// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdlib.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief simplified reason codes for a lost connection.
 *
 * TODO define all status enums when we integrate the LCD & status LED. This was just copied from Squeezbox light!
 *
 * esp-idf maintains a big list of reason codes which in practice are useless for most typical application.
 * Defined at:
 * https://github.com/sle118/squeezelite-esp32/blob/769ff99f7df8e9d90bcc7c0a1e694809d89831ed/components/wifi-manager/network_manager.h#L253
 *
 * UPDATE_CONNECTION_OK  - Web UI expects this when attempting to connect to a new access point succeeds
 * UPDATE_FAILED_ATTEMPT - Web UI expects this when attempting to connect to a new access point fails
 * UPDATE_USER_DISCONNECT = 2,
 * UPDATE_LOST_CONNECTION = 3,
 * UPDATE_FAILED_ATTEMPT_AND_RESTORE - Web UI expects this when attempting to connect to a new access point fails and
 * previous connection is restored
 * UPDATE_ETHERNET_CONNECTED = 5
 *
 * TODO define codes for UCD & display state-machine. Make it private?
 */
typedef enum update_reason_code_t {
    UPDATE_WIFI_CONNECTED = 0,              // relates to GOT_IP (from STA)
    UPDATE_FAILED_ATTEMPT = 1,              //
    UPDATE_USER_DISCONNECT = 2,             // TODO required for UCD?
    UPDATE_LOST_CONNECTION = 3,             //
    UPDATE_FAILED_ATTEMPT_AND_RESTORE = 4,  // TOO required for UCD?
    UPDATE_ETH_CONNECTED = 5,               // relates to ETH_GOT_IP
    UPDATE_ETH_LINK_DOWN,                   // relates to ETHERNET_EVENT_DISCONNECTED
    UPDATE_ETH_LINK_UP,                     // relates to ETHERNET_EVENT_CONNECTED
    UPDATE_ETH_CONNECTING,
    UPDATE_WIFI_CONNECTING,
    UPDATE_WIFI_PROVISIONING
} update_reason_code_t;

esp_err_t network_start(void);

// --- Public state machine events --------------------------------------------

/// @brief Ethernet link is up.
void trigger_link_up_event(void);
/// @brief Ethernet link is down.
void trigger_link_down_event(void);
/// @brief Ethernet interface got an IP address.
void trigger_eth_got_ip_event(void);
/// @brief WiFi interface is connected to access point.
void trigger_connected_event(void);
/// @brief WiFi interface got an IP address.
void trigger_wifi_got_ip_event(void);
/// @brief Connect WiFi to given access point.
// TODO use byte arrays for ssid & password
void trigger_connect_to_ap_event(const char* ssid, const char* password);
/// @brief WiFi interface lost connection to access point.
void trigger_lost_connection_event(wifi_event_sta_disconnected_t* disconnected_event);
/// @brief Remove WiFi station configuration. Disconnects from access point and deletes WiFi configuration.
void trigger_delete_wifi_event(void);
/// @brief Physical button on device was pressed.
void trigger_button_press_event(void);
/// @brief Perform a device restart.
void trigger_reboot_event(void);

// ----------------------------------------------------------------------------

/// @brief Check ethernet interface link status.
/// @return true if ethernet interface link is up.
bool is_eth_link_up(void);

/// @brief Check if ethernet interface is connected and has an IP address.
bool is_eth_connected(void);

/// @brief Check if wifi interface is enabled.
bool is_wifi_up(void);

bool network_is_interface_connected(esp_netif_t* interface);

esp_err_t network_get_ip_info_for_netif(esp_netif_t* netif, esp_netif_ip_info_t* ipInfo);

/// @brief Initialize ethernet PWM status leds.
/// @return ESP_OK if successful
esp_err_t eth_pwm_led_init(void);

/// @brief Set brightness of ethernet status leds.
/// @param value brighness value: 0 (off) - 255 (brightest)
/// @return ESP_OK if successful
esp_err_t set_eth_led_brightness(int value);

#ifdef __cplusplus
}
#endif
