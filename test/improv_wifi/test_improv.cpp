// SPDX-FileCopyrightText: Copyright (c) 2025 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "improv.h"

TEST(ImprovWifiTest, ParseImprovData_WifiSettings) {
    uint8_t data[] = {WIFI_SETTINGS, 20,
                      // ssid
                      10, 'H', 'e', 'l', 'l', 'o', ' ', 'S', 'S', 'I', 'D',
                      // password
                      8, '1', '2', '3', '4', '5', '6', '7', '8', 18};

    struct ImprovCommand id = parse_improv_data(data, sizeof(data), true);

    EXPECT_EQ(WIFI_SETTINGS, id.command);
    EXPECT_STREQ("Hello SSID", (const char *)id.ssid);
    EXPECT_STREQ("12345678", (const char *)id.password);
    EXPECT_STREQ("", id.device_name);
    EXPECT_STREQ("", id.device_token);
}

TEST(ImprovWifiTest, ParseImprovData_WifiSettings_MaxSSIDandPwdLengths) {
    uint8_t data[] = {WIFI_SETTINGS, 98,
                      // ssid
                      32, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
                      '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2',
                      // password
                      64, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
                      '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2', '3', '4', '5', '6', '7', '8',
                      '9', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2', '3', '4', '5', '6', '7',
                      '8', '9', '0', '1', '2', '3', '4'};

    struct ImprovCommand id = parse_improv_data(data, sizeof(data), false);

    EXPECT_EQ(WIFI_SETTINGS, id.command);
    EXPECT_STREQ("12345678901234567890123456789012", (const char *)id.ssid);
    EXPECT_STREQ("1234567890123456789012345678901234567890123456789012345678901234", (const char *)id.password);
    EXPECT_STREQ("", id.device_name);
    EXPECT_STREQ("", id.device_token);
}

TEST(ImprovWifiTest, ParseImprovData_WifiSettings_InvalidChecksum) {
    uint8_t data[] = {WIFI_SETTINGS, 20,
                      // ssid
                      10, 'H', 'e', 'l', 'l', 'o', ' ', 'S', 'S', 'I', 'D',
                      // password
                      8, '1', '2', '3', '4', '5', '6', '7', '8', 19};

    struct ImprovCommand id = parse_improv_data(data, sizeof(data), true);

    EXPECT_EQ(BAD_CHECKSUM, id.command);
    // make sure all data is nulled
    EXPECT_STREQ("", (const char *)id.ssid);
    EXPECT_STREQ("", (const char *)id.password);
    EXPECT_STREQ("", id.device_name);
    EXPECT_STREQ("", id.device_token);
}

TEST(ImprovWifiTest, ParseImprovData_WifiSettings_PwdLengthLongerThanBuffer) {
    uint8_t data[] = {WIFI_SETTINGS, 20,
                      // ssid
                      10, 'H', 'e', 'l', 'l', 'o', ' ', 'S', 'S', 'I', 'D',
                      // password
                      9, '1', '2', '3', '4', '5', '6', '7', '8', 19};

    struct ImprovCommand id = parse_improv_data(data, sizeof(data), true);

    EXPECT_EQ(UNKNOWN, id.command);
    EXPECT_STREQ("", (const char *)id.ssid);
    EXPECT_STREQ("", (const char *)id.password);
    EXPECT_STREQ("", id.device_name);
    EXPECT_STREQ("", id.device_token);
}

TEST(ImprovWifiTest, ParseImprovData_WifiSettings_SSIDLengthTooLong) {
    uint8_t data[] = {WIFI_SETTINGS, 75,
                      // ssid
                      33, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
                      '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2', '3',
                      // password
                      40, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
                      '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2', '3', '4', '5', '6', '7', '8',
                      '9', '0',
                      // checksum
                      134};

    struct ImprovCommand id = parse_improv_data(data, sizeof(data), true);

    EXPECT_EQ(UNKNOWN, id.command);
    EXPECT_STREQ("", (const char *)id.ssid);
    EXPECT_STREQ("", (const char *)id.password);
    EXPECT_STREQ("", id.device_name);
    EXPECT_STREQ("", id.device_token);
}

TEST(ImprovWifiTest, ParseImprovData_DeviceParam_InvalidChecksum) {
    uint8_t data[] = {UC_SET_DEVICE_PARAM, 22,
                      // param
                      11, FRIENDLY_NAME, 'H', 'e', 'l', 'l', 'o', ' ', 't', 'e', 's', 't',
                      // param
                      9, ACCESS_TOKEN, '1', '2', '3', '4', '5', '6', '7', '8', 10};

    struct ImprovCommand id = parse_improv_data(data, sizeof(data), true);

    EXPECT_EQ(BAD_CHECKSUM, id.command);
    EXPECT_STREQ("", (const char *)id.ssid);
    EXPECT_STREQ("", (const char *)id.password);
    EXPECT_STREQ("", id.device_name);
    EXPECT_STREQ("", id.device_token);
}

TEST(ImprovWifiTest, ParseImprovData_DeviceParam_IgnoreInvalidChecksum) {
    uint8_t data[] = {UC_SET_DEVICE_PARAM, 22,
                      // param
                      11, FRIENDLY_NAME, 'H', 'e', 'l', 'l', 'o', ' ', 't', 'e', 's', 't',
                      // param
                      9, ACCESS_TOKEN, '1', '2', '3', '4', '5', '6', '7', '8'};

    struct ImprovCommand id = parse_improv_data(data, sizeof(data), false);

    EXPECT_EQ(UC_SET_DEVICE_PARAM, id.command);
}

TEST(ImprovWifiTest, ParseImprovData_DeviceParam) {
    uint8_t data[] = {UC_SET_DEVICE_PARAM, 22,
                      // param
                      11, FRIENDLY_NAME, 'H', 'e', 'l', 'l', 'o', ' ', 't', 'e', 's', 't',
                      // param
                      9, ACCESS_TOKEN, '1', '2', '3', '4', '5', '6', '7', '8', 109};

    struct ImprovCommand id = parse_improv_data(data, sizeof(data), true);

    EXPECT_EQ(UC_SET_DEVICE_PARAM, id.command);
    EXPECT_STREQ("Hello test", id.device_name);
    EXPECT_STREQ("12345678", id.device_token);
}

TEST(ImprovWifiTest, ParseImprovData_DeviceParam_FriendlyNameOnly) {
    uint8_t data[] = {UC_SET_DEVICE_PARAM, 12,
                      // param
                      11, FRIENDLY_NAME, 'H', 'e', 'l', 'l', 'o', ' ', 't', 'e', 's', 't', 180};

    struct ImprovCommand id = parse_improv_data(data, sizeof(data), true);

    EXPECT_EQ(UC_SET_DEVICE_PARAM, id.command);
    EXPECT_STREQ("Hello test", id.device_name);
    EXPECT_STREQ("", id.device_token);
}

TEST(ImprovWifiTest, ParseImprovData_DeviceParam_WithUnknownParams) {
    uint8_t data[] = {UC_SET_DEVICE_PARAM, 27, 1, 0xFF,
                      // param
                      11, FRIENDLY_NAME, 'H', 'e', 'l', 'l', 'o', ' ', 't', 'e', 's', 't',
                      // unknown param
                      2, 0xFE, 0,
                      // param
                      9, ACCESS_TOKEN, '1', '2', '3', '4', '5', '6', '7', '8'};

    struct ImprovCommand id = parse_improv_data(data, sizeof(data), false);

    EXPECT_EQ(UC_SET_DEVICE_PARAM, id.command);
    EXPECT_STREQ("Hello test", id.device_name);
    EXPECT_STREQ("12345678", id.device_token);
}

TEST(ImprovWifiTest, ParseImprovData_DeviceParam_InvalidParamLength) {
    uint8_t data[] = {UC_SET_DEVICE_PARAM, 22,
                      // param
                      11, FRIENDLY_NAME, 'H', 'e', 'l', 'l', 'o', ' ', 't', 'e', 's', 't',
                      // param
                      10, ACCESS_TOKEN, '1', '2', '3', '4', '5', '6', '7', '8'};

    struct ImprovCommand id = parse_improv_data(data, sizeof(data), false);

    EXPECT_EQ(UNKNOWN, id.command);
    EXPECT_STREQ("", id.device_token);
}
