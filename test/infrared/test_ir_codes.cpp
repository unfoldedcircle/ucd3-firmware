// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "ir_codes.h"

TEST(IrCodesTest, ParseUint32WithNullInput) {
    int error;

    EXPECT_EQ(0, parseUint32(NULL));
    EXPECT_EQ(0, parseUint32(NULL, &error));
    EXPECT_EQ(1, error);
}

TEST(IrCodesTest, ParseUint32WithEmptyInput) {
    int error;

    EXPECT_EQ(0, parseUint32(""));
    EXPECT_EQ(0, parseUint32("", &error));
    EXPECT_EQ(1, error);
}

TEST(IrCodesTest, ParseUint32WithInvalidInput) {
    int error;

    EXPECT_EQ(0, parseUint32("foo"));
    EXPECT_EQ(0, parseUint32("foo", &error));
    EXPECT_EQ(1, error);
    EXPECT_EQ(0, parseUint32("42foo"));
    EXPECT_EQ(0, parseUint32("42foo", &error));
    EXPECT_EQ(1, error);
    EXPECT_EQ(0, parseUint32("1;"));
    EXPECT_EQ(0, parseUint32("1;", &error));
    EXPECT_EQ(1, error);
    EXPECT_EQ(0, parseUint32("0;", &error));
    EXPECT_EQ(1, error);
    EXPECT_EQ(0, parseUint32("4294967296", &error));
    EXPECT_EQ(1, error);
}

TEST(IrCodesTest, ParseUint32) {
    int error;

    EXPECT_EQ(0, parseUint32("0"));
    EXPECT_EQ(1, parseUint32("1"));
    EXPECT_EQ(0, parseUint32("0", &error));
    EXPECT_EQ(0, error);
    EXPECT_EQ(1, parseUint32("1", &error));
    EXPECT_EQ(0, error);
    EXPECT_EQ(4294967295, parseUint32("4294967295", &error));
    EXPECT_EQ(0, error);
}

TEST(IrCodesTest, BuildIRHexData) {
    struct IRHexData data;
    std::string      msg = "4;0x640C;15;1";
    EXPECT_EQ(true, buildIRHexData(msg, &data));
    EXPECT_EQ(4, data.protocol);
    EXPECT_EQ(0x640C, data.command);
    EXPECT_EQ(15, data.bits);
    EXPECT_EQ(1, data.repeat);
}

TEST(IrCodesTest, BuildIRHexDataEmptyString) {
    struct IRHexData data;
    std::string      msg;
    EXPECT_FALSE(buildIRHexData(msg, &data));
}

TEST(IrCodesTest, BuildIRHexDataInvalidSeparator) {
    struct IRHexData data;
    std::string      msg = "4,0x640C,15,0";
    EXPECT_FALSE(buildIRHexData(msg, &data));
}

TEST(IrCodesTest, BuildIRHexDataMissingProtocolValue) {
    struct IRHexData data;
    std::string      msg = ";0x640C;15;1";
    EXPECT_FALSE(buildIRHexData(msg, &data));
}

TEST(IrCodesTest, BuildIRHexDataMissingCommandValue) {
    struct IRHexData data;
    std::string      msg = "4;;15;1";
    EXPECT_FALSE(buildIRHexData(msg, &data));
}

TEST(IrCodesTest, BuildIRHexDataMissingBitsValue) {
    struct IRHexData data;
    std::string      msg = "4;0x640C;;1";
    EXPECT_FALSE(buildIRHexData(msg, &data));
}

TEST(IrCodesTest, BuildIRHexDataMissingRepeatValue) {
    struct IRHexData data;
    std::string      msg = "4;0x640C;15;";
    EXPECT_FALSE(buildIRHexData(msg, &data));
}

TEST(IrCodesTest, BuildIRHexDataMissingRepeat) {
    struct IRHexData data;
    std::string      msg = "4;0x640C;15";
    EXPECT_FALSE(buildIRHexData(msg, &data));
}

TEST(IrCodesTest, BuildIRHexDataInvalidProtocolValue) {
    struct IRHexData data;
    std::string      msg = "z;0x640C;15;1";
    EXPECT_FALSE(buildIRHexData(msg, &data));
}

TEST(IrCodesTest, BuildIRHexDataInvalidCommandValue) {
    struct IRHexData data;
    std::string      msg = "4;hello;15;1";
    EXPECT_FALSE(buildIRHexData(msg, &data));
}

TEST(IrCodesTest, BuildIRHexDataInvalidBitsValue) {
    struct IRHexData data;
    std::string      msg = "4;0x640C;2tt;1";
    EXPECT_FALSE(buildIRHexData(msg, &data));
}

TEST(IrCodesTest, BuildIRHexDataInvalidRepeatValue) {
    struct IRHexData data;
    std::string      msg = "4;0x640C;15;z1";
    EXPECT_FALSE(buildIRHexData(msg, &data));
}

TEST(IrCodesTest, BuildIRHexDataRepeatTooHigh) {
    struct IRHexData data;
    std::string      msg = "4;0x640C;15;20";
    EXPECT_EQ(true, buildIRHexData(msg, &data));
    msg = "4;0x640C;15;21";
    EXPECT_FALSE(buildIRHexData(msg, &data));
}

TEST(IrCodesTest, countValuesInCStrNullInput) {
    EXPECT_EQ(0, countValuesInCStr(NULL, ','));
}

TEST(IrCodesTest, countValuesInCStrEmptyInput) {
    EXPECT_EQ(0, countValuesInCStr("", ','));
}

TEST(IrCodesTest, countValuesInCStrWithoutSeparator) {
    EXPECT_EQ(1, countValuesInCStr("h", ','));
    EXPECT_EQ(1, countValuesInCStr("hi", ','));
    EXPECT_EQ(1, countValuesInCStr("hi there", ','));
}

TEST(IrCodesTest, countValuesInCStr) {
    EXPECT_EQ(2, countValuesInCStr("0,1", ','));
    EXPECT_EQ(3, countValuesInCStr("0,1,2", ','));
}

TEST(IrCodesTest, ProntoBufferToArrayEmptyInput) {
    uint16_t codeCount;
    EXPECT_EQ(prontoBufferToArray("", ',', &codeCount), nullptr);
}

TEST(IrCodesTest, ProntoBufferToArrayNotEnoughInput) {
    uint16_t codeCount;
    EXPECT_EQ(prontoBufferToArray("0000", ' ', &codeCount), nullptr);
    EXPECT_EQ(prontoBufferToArray("0000 0066", ' ', &codeCount), nullptr);
    EXPECT_EQ(prontoBufferToArray("0000 0066 0000", ' ', &codeCount), nullptr);
    EXPECT_EQ(prontoBufferToArray("0000 0066 0000 0001", ' ', &codeCount), nullptr);
    EXPECT_EQ(prontoBufferToArray("0000 0066 0000 0001 0050", ' ', &codeCount), nullptr);
}

TEST(IrCodesTest, ProntoBufferToArrayInputTooShort) {
    uint16_t codeCount;
    EXPECT_EQ(prontoBufferToArray("0000 0066 0000 0018 0050 0051", ' ', &codeCount), nullptr);
    EXPECT_EQ(prontoBufferToArray("0000 0066 0000 0002 0050 0051", ' ', &codeCount), nullptr);
}

TEST(IrCodesTest, ProntoBufferToArrayMinLength) {
    uint16_t codeCount;
    auto     buffer = prontoBufferToArray("0000 0066 0000 0001 0050 0051", ' ', &codeCount);
    EXPECT_EQ(6, codeCount);
    EXPECT_NE(buffer, nullptr);
    free(buffer);
}

TEST(IrCodesTest, ProntoBufferToArray) {
    uint16_t codeCount;
    int      memError;
    auto     buffer = prontoBufferToArray(
        "0000,0066,0000,0018,0050,0051,0015,008e,0051,0050,0015,008f,0014,008f,0050,0051,0050,0051,0015,05af,0051,0050,"
            "0015,008e,0051,0051,0014,008f,0015,008e,0050,0051,0051,0050,0015,05af,0051,0050,0015,008e,0051,0051,0015,008e,"
            "0015,008e,0050,0051,0051,0050,0015,0ff1",
        ',', &codeCount, &memError);
    EXPECT_EQ(0, memError);
    EXPECT_NE(buffer, nullptr);
    free(buffer);
}

TEST(IrCodesTest, GlobalCacheBufferToArrayEmptyInput) {
    uint16_t codeCount;
    EXPECT_EQ(globalCacheBufferToArray("", &codeCount), nullptr);
}

TEST(IrCodesTest, GlobalCacheBufferToArrayShort) {
    uint16_t codeCount;
    int      memError;
    auto     buffer = globalCacheBufferToArray(
        "38000,1,69,340,171,21,21,21,21,21,65,21,21,21,21,21,21,21,21,21,21,21,65,21,65,21,21,21,65,21,65,21,65,21,65,"
            "21,65,21,21,21,65,21,21,21,21,21,21,21,21,21,21,21,21,21,65,21,21,21,65,21,65,21,65,21,65,21,65,21,65,21,1555,"
            "340,86,21,3678",
        &codeCount, &memError);
    EXPECT_EQ(0, memError);
    EXPECT_NE(buffer, nullptr);
    EXPECT_EQ(75, codeCount);
    EXPECT_EQ(38000, buffer[0]);
    EXPECT_EQ(3678, buffer[codeCount - 1]);
    free(buffer);
}

TEST(IrCodesTest, GlobalCacheBufferToArrayFull) {
    uint16_t codeCount;
    int      memError;
    auto     buffer = globalCacheBufferToArray(
        "sendir,1:1,1,38000,1,69,340,171,21,21,21,21,21,65,21,21,21,21,21,21,21,21,21,21,21,65,21,65,21,21,21,65,21,65,"
            "21,65,21,65,21,65,21,21,21,65,21,21,21,21,21,21,21,21,21,21,21,21,21,65,21,21,21,65,21,65,21,65,21,65,21,65,"
            "21,65,21,1555,340,86,21,3678",
        &codeCount, &memError);
    EXPECT_EQ(0, memError);
    EXPECT_NE(buffer, nullptr);
    EXPECT_EQ(75, codeCount);
    EXPECT_EQ(38000, buffer[0]);
    EXPECT_EQ(3678, buffer[codeCount - 1]);
    free(buffer);
}
