// SPDX-FileCopyrightText: Copyright (c) 2023 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "globalcache.h"

uint8_t parseGcRequest(const char *request, GCMsg *msg) {
    if (request == nullptr || msg == nullptr) {
        return false;
    }

    // command
    auto        len = sizeof(msg->command);
    const char *next = strchr(request, ',');
    if (next == NULL) {
        // simple command without module:port or parameters
        auto cmdLen = strlen(request);
        if (cmdLen >= len) {
            return 1;  // command name too long: unknown command
        }
        auto count = std::min(len, cmdLen);
        strncpy(msg->command, request, count);
        msg->command[count] = 0;
        msg->module = 0;
        msg->port = 0;
        msg->param = nullptr;
        return 0;
    }

    if ((next - request) >= len) {
        return 1;  // command name too long: unknown command
    }
    strncpy(msg->command, request, next - request);
    msg->command[next - request] = 0;

    // check for:  <module>:<port>,<param(s)>
    const char *current = next + 1;
    next = strchr(current, ',');
    if (next == NULL) {
        // no comma, check for: <module>:<port>
        next = strchr(current, ':');
        if (next == NULL) {
            msg->module = 0;
            msg->port = 0;
            msg->param = current;
            return 0;
        }
    }
    auto module = atoi(current);
    if (module != 1) {
        return 2;  // invalid module address
    }
    msg->module = module;

    next = strchr(current, ':');
    if (next == NULL) {
        return 3;  // invalid port address
    }
    current = next + 1;
    auto port = atoi(current);
    if (port < 1 || port > 15) {
        return 3;  // invalid port address
    }
    msg->port = port;

    // param(s)
    next = strchr(current, ',');
    if (next == NULL) {
        msg->param = nullptr;
    } else {
        msg->param = next + 1;
    }

    return 0;
}
