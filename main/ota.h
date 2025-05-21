// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Quick and dirty blocking HTTPD server callback for uploading an OTA firmware image.
///
/// Attention: this blocks other server requests including WebSocket connections!
/// @param req http POST request with firmware file content.
/// @return ESP_OK if successful
esp_err_t on_ota_upload(httpd_req_t *req);

#ifdef __cplusplus
}
#endif