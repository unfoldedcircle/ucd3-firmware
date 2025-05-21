// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// @brief GATT register callback function for logging resource registrations (service, characteristic, or descriptor).
void log_gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);

void log_ble_gap_event(struct ble_gap_event *event, void *arg);

#ifdef __cplusplus
}
#endif