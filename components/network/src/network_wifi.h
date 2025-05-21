// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Inspired by https://github.com/Mair/esp32-course

// TODO refactor into a cpp class

#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

// /// @brief Function callback for WiFi events.
// typedef void (*wifi_connect_event_handler)(wifi_connect_event_t event);

/// @brief Initialize wifi stack.
/// @return esp_netif_t pointer to initialized WiFi network interface or NULL if failed.
esp_netif_t* network_wifi_start();

esp_err_t network_wifi_connect(const char* ssid, const char* password);

void network_wifi_clear_config();

/// @brief Save active STA settings (ssid & password) to our configuration.
void network_wifi_save_config();

// void wifi_connect_init(wifi_connect_event_handler connect_handler);

/// @brief Establish a WiFi STA connection to the stored configuration in NVS
/// @return ESP_OK if successful
esp_err_t wifi_connect_configured_sta();

/// @brief Establish a WiFi STA connection.
/// @param ssid Network name
/// @param password Password
/// @return ESP_OK if successful
esp_err_t wifi_connect_sta(uint8_t ssid[32], uint8_t password[64]);

/// @brief Setup a WiFi access point.
/// @param ssid Network name
/// @param password Password
/// @return ESP_OK if successful
esp_err_t wifi_connect_ap(uint8_t ssid[32], uint8_t password[64]);

/// @brief Stop WiFi stack and destroy allocated resources.
void wifi_disconnect(void);

#ifdef __cplusplus
}
#endif