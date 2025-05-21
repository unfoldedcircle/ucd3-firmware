// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "network.h"

#ifdef __cplusplus
extern "C" {
#endif

// TODO use a class with a destructor to free allocated mem?
struct queue_message {
    uint8_t                        event;
    char                          *ssid;
    char                          *password;
    wifi_event_sta_disconnected_t *sta_disconnected_event;
};

// --- internal events --------------------------------------------------------
/// @brief Start network state machine. Must be called after SM creation.
void trigger_start_event(void);
/// @brief Initialization failed.
void trigger_init_fail_event(void);
/// @brief Initialization succeeded.
void trigger_init_success_event(void);
/// @brief Switch from WiFi to ethernet.
void trigger_eth_fallback_event(void);
/// @brief Timer expired.
void trigger_timer_event(void);
/// @brief Start WiFi provisioning.
void trigger_configure_wifi_event(void);

void trigger_improv_authorized_timeout_event(void);
void trigger_improv_ble_connect_event(void);
void trigger_improv_ble_disconnect_event(void);
// ----------------------------------------------------------------------------

void network_start_stop_dhcp_client(esp_netif_t *netif, bool start);

void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void network_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

esp_err_t init_sntp(void);

void network_set_hostname(esp_netif_t *interface);

#ifdef __cplusplus
}
#endif
