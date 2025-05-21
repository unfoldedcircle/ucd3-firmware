// SPDX-FileCopyrightText: Copyright (c) 2023 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "config.h"
#include "service_ir.h"

/// @brief Send string buffer to client socket.
/// @param socket client socket
/// @param buf string buffer. Must be zero-terminated.
/// @return true if successful, false if send failed.
bool send_string_to_socket(const int socket, const char *buf);

/// GlobalCache iTach API emulation.
///
/// This allows using the Dock as IR emitter with 3rd party tools, e.g. with the Home Assistant Global Cache
/// integration. Only a subset of commands are implemented, and it does not work with all tools if they expect
/// a specific device model or API calls.
class GlobalCacheServer {
 public:
    /// @brief Create and start the GC service task.
    /// @param irService IR service instance.
    /// @param config Configuration instance.
    /// @param beacon Send beacon messages announcing the service.
    GlobalCacheServer(InfraredService *irService, Config *config, bool beacon = false);

 private:
    static void tcp_server_task(void *pvParameters);
    static void socket_task(void *pvParameters);
    static void beacon_task(void *param);

    InfraredService *m_irService;
    Config          *m_config;
};
