// SPDX-FileCopyrightText: Copyright (c) 2025 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wifi_prov_cfg.h"

#include <string.h>

#include "config.h"

void set_device_parameters(struct ImprovCommand cmd) {
    Config &cfg = Config::instance();
    if (strlen(cmd.device_name) > 0) {
        cfg.setFriendlyName(cmd.device_name);
    }
    if (strlen(cmd.device_token) > 0) {
        cfg.setToken(cmd.device_token);
    }
}

const char *cfg_get_serial() {
    return Config::instance().getSerial();
}

const char *cfg_get_model() {
    return Config::instance().getModel();
}

const char *cfg_get_revision() {
    return Config::instance().getRevision();
}
