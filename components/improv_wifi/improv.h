// SPDX-FileCopyrightText: Copyright (c) 2024 https://github.com/improv-wifi/sdk-cpp contributors.
// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: Apache-2.0

// Stripped down, plain C version from https://github.com/improv-wifi/sdk-cpp

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum Error {
    ERROR_NONE = 0x00,
    ERROR_INVALID_RPC = 0x01,
    ERROR_UNKNOWN_RPC = 0x02,
    ERROR_UNABLE_TO_CONNECT = 0x03,
    ERROR_NOT_AUTHORIZED = 0x04,
    ERROR_UNKNOWN = 0xFF,
} improv_error_t;

typedef enum State {
    STATE_STOPPED = 0x00,
    STATE_AWAITING_AUTHORIZATION = 0x01,
    STATE_AUTHORIZED = 0x02,
    STATE_PROVISIONING = 0x03,
    STATE_PROVISIONED = 0x04,
} improv_state_t;

typedef enum Command {
    UNKNOWN = 0x00,
    WIFI_SETTINGS = 0x01,
    IDENTIFY = 0x02,
    GET_CURRENT_STATE = 0x02,
    GET_DEVICE_INFO = 0x03,
    GET_WIFI_NETWORKS = 0x04,
    // UC enhancement: set a device parameter, e.g. a friendly name or access token
    UC_SET_DEVICE_PARAM = 0xC8,
    BAD_CHECKSUM = 0xFF,
} improv_command_t;

typedef enum UcDeviceParam {
    FRIENDLY_NAME = 0x01,
    ACCESS_TOKEN = 0x02,
} uc_device_param_t;

static const uint8_t CAPABILITY_IDENTIFY = 0x01;
static const uint8_t IMPROV_SERIAL_VERSION = 1;

typedef enum ImprovSerialType {
    TYPE_CURRENT_STATE = 0x01,
    TYPE_ERROR_STATE = 0x02,
    TYPE_RPC = 0x03,
    TYPE_RPC_RESPONSE = 0x04
} improv_serial_type_t;

/// Maximum length in bytes of a WIFI SSID
#define WIFI_SSID_LEN 32
/// Maximum length in bytes of a WIFI password
#define WIFI_PWD_LEN 64

struct ImprovCommand {
    enum Command command;
    // SSID of target AP. HACK: zero terminated string! FIXME use byte array for ssid
    uint8_t ssid[WIFI_SSID_LEN + 1];
    // Password of target AP. HACK: zero terminated string! FIXME use byte array for password
    uint8_t password[WIFI_PWD_LEN + 1];
    // UC enhancement: optional device name string (zero terminated)
    char device_name[41];
    // UC enhancement: optional access token string (zero terminated)
    char device_token[41];
};

struct ImprovCommand parse_improv_data(const uint8_t* data, size_t length, bool check_checksum);
uint8_t* build_rpc_response(improv_command_t command, const char* datum[], uint8_t num_datum, bool add_checksum,
                            uint16_t* buf_length);

const char* get_state_str(improv_state_t state);
const char* get_error_str(improv_error_t error);

#ifdef __cplusplus
}
#endif