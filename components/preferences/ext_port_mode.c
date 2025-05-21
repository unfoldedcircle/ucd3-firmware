// SPDX-FileCopyrightText: Copyright (c) 2025 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ext_port_mode.h"

#include <string.h>

const char *ExtPortMode_to_str(enum ExtPortMode mode) {
    switch (mode) {
        case AUTO:
            return "AUTO";
        case NOT_CONFIGURED:
            return "NONE";
        case IR_BLASTER:
            return "IR_BLASTER";
        case IR_EMITTER_MONO_PLUG:
            return "IR_EMITTER_MONO_PLUG";
        case IR_EMITTER_STEREO_PLUG:
            return "IR_EMITTER_STEREO_PLUG";
        case TRIGGER_5V:
            return "TRIGGER_5V";
        case RS232:
            return "RS232";
        default:
            return "UNKNOWN";
    }
}

enum ExtPortMode ExtPortMode_from_str(const char *mode) {
    if (strcmp(mode, "AUTO") == 0) {
        return AUTO;
    }
    if (strcmp(mode, "NONE") == 0) {
        return NOT_CONFIGURED;
    }
    if (strcmp(mode, "IR_BLASTER") == 0) {
        return IR_BLASTER;
    }
    if (strcmp(mode, "IR_EMITTER_MONO_PLUG") == 0) {
        return IR_EMITTER_MONO_PLUG;
    }
    if (strcmp(mode, "IR_EMITTER_STEREO_PLUG") == 0) {
        return IR_EMITTER_STEREO_PLUG;
    }
    if (strcmp(mode, "TRIGGER_5V") == 0) {
        return TRIGGER_5V;
    }
    if (strcmp(mode, "RS232") == 0) {
        return RS232;
    }
    return PORT_MODE_MAX;
}

const char *ExtPortMode_to_friendly_str(enum ExtPortMode mode) {
    switch (mode) {
        case AUTO:
            return "auto";
        case NOT_CONFIGURED:
            return "none";
        case IR_BLASTER:
            return "IR-Blaster";
        case IR_EMITTER_MONO_PLUG:
            return "IR-Emitter mono plug";
        case IR_EMITTER_STEREO_PLUG:
            return "IR-Emitter";
        case TRIGGER_5V:
            return "Trigger";
        case RS232:
            return "RS232";
        default:
            return "unknown";
    }
}
