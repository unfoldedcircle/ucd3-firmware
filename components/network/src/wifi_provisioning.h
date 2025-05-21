// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdlib.h>

#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize Improv WiFi
 */
void init_improv(void);

/**
 * Start Improv WiFi.
 *
 * Needs to be called after NimBLE initialization for BLE service setup and advertising.
 *
 * FIXME Quick and dirty, requires refactoring if other BLE services are introduced.
 */
esp_err_t start_improv(void);

void improv_set_authorized();
void improv_set_provisioned();
void on_wifi_connect_timeout();

// void handle_wifi_event(wifi_connect_event_t event);

#ifdef __cplusplus
}
#endif
