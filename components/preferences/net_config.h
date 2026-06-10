// SPDX-FileCopyrightText: Copyright (c) 2025 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdlib.h>

#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool                dhcp;  // true = DHCP, false = static
    esp_netif_ip_info_t ip;    // numeric IPv4 ip/netmask/gw
} iface_net_cfg_t;

typedef struct {
    iface_net_cfg_t eth;   // Ethernet config
    iface_net_cfg_t wifi;  // WiFi STA config
} network_cfg_t;

#ifdef __cplusplus
}
#endif
