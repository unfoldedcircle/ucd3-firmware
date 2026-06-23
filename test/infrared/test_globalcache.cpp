// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "globalcache.h"

TEST(GlobalCacheTest, parseGcRequest_nullInput) {
    const char *request = "blink";
    GCMsg       msg;
    EXPECT_EQ(17, parseGcRequest(request, nullptr));
    EXPECT_EQ(17, parseGcRequest(nullptr, &msg));
    EXPECT_EQ(17, parseGcRequest(nullptr, nullptr));
}

TEST(GlobalCacheTest, parseGcRequest_emptyInput) {
    const char *request = "";
    GCMsg       msg;
    EXPECT_EQ(0, parseGcRequest(request, &msg));
}

TEST(GlobalCacheTest, parseGcRequest_commandOnlyTooLong) {
    const char *request = "01234567890123456789";
    GCMsg       msg;
    EXPECT_EQ(1, parseGcRequest(request, &msg));
}

TEST(GlobalCacheTest, parseGcRequest_commandWithParamTooLong) {
    const char *request = "01234567890123456789,foobar";
    GCMsg       msg;
    EXPECT_EQ(1, parseGcRequest(request, &msg));
}

TEST(GlobalCacheTest, parseGcRequest_commandTooLong) {
    const char *request = "01234567890123456789,1:1,foo,bar";
    GCMsg       msg;
    EXPECT_EQ(1, parseGcRequest(request, &msg));
}

TEST(GlobalCacheTest, parseGcRequest_commandOnly) {
    const char *request = "blink";
    GCMsg       msg;
    EXPECT_EQ(0, parseGcRequest(request, &msg));
    EXPECT_STREQ("blink", msg.command);
    EXPECT_EQ(0, msg.module);
    EXPECT_EQ(0, msg.port);
}

TEST(GlobalCacheTest, parseGcRequest_commandAndModule) {
    const char *request = "stopir,1:3";
    GCMsg       msg;
    EXPECT_EQ(0, parseGcRequest(request, &msg));
    EXPECT_STREQ("stopir", msg.command);
    EXPECT_EQ(1, msg.module);
    EXPECT_EQ(3, msg.port);
    EXPECT_EQ(nullptr, msg.param);
}

TEST(GlobalCacheTest, parseGcRequest_commandAndParam) {
    const char *request = "blink,1";
    GCMsg       msg;
    EXPECT_EQ(0, parseGcRequest(request, &msg));
    EXPECT_STREQ("blink", msg.command);
    EXPECT_EQ(0, msg.module);
    EXPECT_EQ(0, msg.port);
    EXPECT_STREQ("1", msg.param);
}

TEST(GlobalCacheTest, parseGcRequest_full) {
    const char *request =
        "sendir,1:1,1,37000,1,1,128,64,16,16,16,16,16,48,16,16,16,48,16,16,16,48,16,16,16,16,16,48,16,16,16,16,16,48,"
        "16,48,16,16,16,16,16,16,16,16,16,16,16,16,16,48,16,16,16,48,16,16,16,48,16,16,16,16,16,16,16,48,16,48,16,16,"
        "16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,48,16,16,16,48,16,16,16,16,16,16,16,16,16,48,16,16,16,16,16,2765";
    GCMsg msg;
    EXPECT_EQ(0, parseGcRequest(request, &msg));
    EXPECT_STREQ("sendir", msg.command);
    EXPECT_EQ(1, msg.module);
    EXPECT_EQ(1, msg.port);
    EXPECT_STREQ(
        "1,37000,1,1,128,64,16,16,16,16,16,48,16,16,16,48,16,16,16,48,16,16,16,16,16,48,16,16,16,16,16,48,16,48,16,16,"
        "16,16,16,16,16,16,16,16,16,16,16,48,16,16,16,48,16,16,16,48,16,16,16,16,16,16,16,48,16,48,16,16,16,16,16,16,"
        "16,16,16,16,16,16,16,16,16,16,16,48,16,16,16,48,16,16,16,16,16,16,16,16,16,48,16,16,16,16,16,2765",
        msg.param);
}

TEST(GlobalCacheTest, parseGcRequest_outOfRangeModule) {
    GCMsg msg;
    // only module 1 is valid
    EXPECT_EQ(0, parseGcRequest("stopir,1:3", &msg));
    // out of range
    EXPECT_EQ(2, parseGcRequest("stopir,0:3", &msg));
    EXPECT_EQ(2, parseGcRequest("stopir,2:3", &msg));
}

TEST(GlobalCacheTest, parseGcRequest_invalidModule) {
    GCMsg msg;
    EXPECT_EQ(2, parseGcRequest("stopir,:3", &msg));
    EXPECT_EQ(2, parseGcRequest("stopir,a:3", &msg));
    EXPECT_EQ(2, parseGcRequest("stopir,:3,1", &msg));
    EXPECT_EQ(2, parseGcRequest("stopir,a:3,1", &msg));
}

TEST(GlobalCacheTest, parseGcRequest_outOfRangePort) {
    GCMsg msg;
    // valid range
    EXPECT_EQ(0, parseGcRequest("stopir,1:1", &msg));
    EXPECT_EQ(0, parseGcRequest("stopir,1:15", &msg));
    // out of range
    EXPECT_EQ(3, parseGcRequest("stopir,1:0", &msg));
    EXPECT_EQ(3, parseGcRequest("stopir,1:16", &msg));
}

TEST(GlobalCacheTest, parseGcRequest_invalidPort) {
    GCMsg msg;
    EXPECT_EQ(3, parseGcRequest("stopir,1:", &msg));
    EXPECT_EQ(3, parseGcRequest("stopir,1:,2", &msg));
    EXPECT_EQ(3, parseGcRequest("stopir,1:a", &msg));
    EXPECT_EQ(3, parseGcRequest("stopir,1:a,2", &msg));
}

class RawTimingsToGcSendirTest : public ::testing::Test {
 protected:
    void TearDown() override {
        if (result_ != nullptr) {
            free(result_);
            result_ = nullptr;
        }
    }

    char *result_ = nullptr;
};

// --- NULL and invalid input ---

TEST_F(RawTimingsToGcSendirTest, NullTimingsReturnsNull) {
    result_ = raw_timings_to_gc_sendir(nullptr, 4, 38000, "1:1", 1);
    EXPECT_EQ(result_, nullptr);
}

TEST_F(RawTimingsToGcSendirTest, ZeroLengthReturnsNull) {
    uint16_t timings[] = {100, 200};
    result_ = raw_timings_to_gc_sendir(timings, 0, 38000, "1:1", 1);
    EXPECT_EQ(result_, nullptr);
}

TEST_F(RawTimingsToGcSendirTest, ZeroFrequencyReturnsNull) {
    uint16_t timings[] = {100, 200};
    result_ = raw_timings_to_gc_sendir(timings, 2, 0, "1:1", 1);
    EXPECT_EQ(result_, nullptr);
}

TEST_F(RawTimingsToGcSendirTest, NullConnectorReturnsNull) {
    uint16_t timings[] = {100, 200};
    result_ = raw_timings_to_gc_sendir(timings, 2, 38000, nullptr, 1);
    EXPECT_EQ(result_, nullptr);
}

// --- Frequency range validation ---

TEST_F(RawTimingsToGcSendirTest, FrequencyBelowMinReturnsNull) {
    uint16_t timings[] = {100, 200};
    result_ = raw_timings_to_gc_sendir(timings, 2, 14999, "1:1", 1);
    EXPECT_EQ(result_, nullptr);
}

TEST_F(RawTimingsToGcSendirTest, FrequencyAtMin) {
    uint16_t timings[] = {100, 200};
    result_ = raw_timings_to_gc_sendir(timings, 2, 15000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_TRUE(strstr(result_, "sendir,1:1,1,15000,1,1,") != nullptr);
}

TEST_F(RawTimingsToGcSendirTest, FrequencyAtMax) {
    uint16_t timings[] = {100, 200};
    result_ = raw_timings_to_gc_sendir(timings, 2, 500000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_TRUE(strstr(result_, "sendir,1:1,1,500000,1,1,") != nullptr);
}

TEST_F(RawTimingsToGcSendirTest, FrequencyAboveMaxReturnsNull) {
    uint16_t timings[] = {100, 200};
    result_ = raw_timings_to_gc_sendir(timings, 2, 500001, "1:1", 1);
    EXPECT_EQ(result_, nullptr);
}

// --- Basic conversion: even count ---

TEST_F(RawTimingsToGcSendirTest, SimplePair38kHz) {
    // 9000 µs at 38kHz: round(9000 * 38000 / 1000000) = round(342.0) = 342
    // 4500 µs at 38kHz: round(4500 * 38000 / 1000000) = round(171.0) = 171
    uint16_t timings[] = {9000, 4500};
    result_ = raw_timings_to_gc_sendir(timings, 2, 38000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,38000,1,1,342,171\r");
}

TEST_F(RawTimingsToGcSendirTest, MultiplePairs38kHz) {
    // 560 µs: round(560 * 38000 / 1000000) = round(21.28) = 21
    // 1690 µs: round(1690 * 38000 / 1000000) = round(64.22) = 64
    uint16_t timings[] = {9000, 4500, 560, 1690};
    result_ = raw_timings_to_gc_sendir(timings, 4, 38000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,38000,1,1,342,171,21,64\r");
}

TEST_F(RawTimingsToGcSendirTest, SimplePair40kHz) {
    // 9000 µs at 40kHz: round(9000 * 40000 / 1000000) = round(360) = 360
    // 4500 µs at 40kHz: round(4500 * 40000 / 1000000) = round(180) = 180
    uint16_t timings[] = {9000, 4500};
    result_ = raw_timings_to_gc_sendir(timings, 2, 40000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,40000,1,1,360,180\r");
}

// --- Odd count: last value duplicated as OFF ---

TEST_F(RawTimingsToGcSendirTest, OddCountSingleValue) {
    // 560 µs at 38kHz = 21 periods. Duplicated as OFF.
    uint16_t timings[] = {560};
    result_ = raw_timings_to_gc_sendir(timings, 1, 38000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,38000,1,1,21,21\r");
}

TEST_F(RawTimingsToGcSendirTest, OddCountThreeValues) {
    uint16_t timings[] = {9000, 4500, 560};
    result_ = raw_timings_to_gc_sendir(timings, 3, 38000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,38000,1,1,342,171,21,21\r");
}

TEST_F(RawTimingsToGcSendirTest, OddCountFiveValues) {
    uint16_t timings[] = {9000, 4500, 560, 1690, 560};
    result_ = raw_timings_to_gc_sendir(timings, 5, 38000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,38000,1,1,342,171,21,64,21,21\r");
}

// --- Overflow handling: single overflow ---

TEST_F(RawTimingsToGcSendirTest, OverflowSingleOnMark) {
    // 70000 µs mark encoded as: [65535, 0, 4465, <space>]
    // Logical: 65535 + 4465 = 70000 µs
    // At 38kHz: round(70000 * 38000 / 1000000) = round(2660) = 2660
    uint16_t timings[] = {65535, 0, 4465, 4500};
    result_ = raw_timings_to_gc_sendir(timings, 4, 38000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,38000,1,1,2660,171\r");
}

TEST_F(RawTimingsToGcSendirTest, OverflowSingleOnSpace) {
    // 9000 µs mark, then 70000 µs space encoded as: [9000, 65535, 0, 4465]
    // But wait: overflow in space position means:
    // Index 0: 9000 (mark)
    // Index 1: 65535 (space start)
    // Index 2: 0 (skip mark)
    // Index 3: 4465 (space continuation)
    // Logical: mark=9000, space=65535+4465=70000
    uint16_t timings[] = {9000, 65535, 0, 4465};
    result_ = raw_timings_to_gc_sendir(timings, 4, 38000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,38000,1,1,342,2660\r");
}

// --- Overflow handling: double overflow ---

TEST_F(RawTimingsToGcSendirTest, OverflowDoubleOnMark) {
    // 140000 µs mark: [65535, 0, 65535, 0, 8930, <space>]
    // Logical: 65535 + 65535 + 8930 = 140000 µs
    // At 38kHz: round(140000 * 38000 / 1000000) = round(5320) = 5320
    uint16_t timings[] = {65535, 0, 65535, 0, 8930, 4500};
    result_ = raw_timings_to_gc_sendir(timings, 6, 38000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,38000,1,1,5320,171\r");
}

// --- Overflow handling: overflow followed by normal pairs ---

TEST_F(RawTimingsToGcSendirTest, OverflowFollowedByNormalPairs) {
    // Overflow mark + normal space, then normal pair
    uint16_t timings[] = {65535, 0, 4465, 4500, 560, 1690};
    result_ = raw_timings_to_gc_sendir(timings, 6, 38000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,38000,1,1,2660,171,21,64\r");
}

TEST_F(RawTimingsToGcSendirTest, NormalPairsFollowedByOverflow) {
    uint16_t timings[] = {560, 1690, 65535, 0, 4465, 4500};
    result_ = raw_timings_to_gc_sendir(timings, 6, 38000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,38000,1,1,21,64,2660,171\r");
}

// --- Overflow with odd logical count ---

TEST_F(RawTimingsToGcSendirTest, OverflowResultingInOddLogicalCount) {
    // Overflow mark + space + normal mark = 3 logical values (odd)
    uint16_t timings[] = {65535, 0, 4465, 4500, 560};
    result_ = raw_timings_to_gc_sendir(timings, 5, 38000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,38000,1,1,2660,171,21,21\r");
}

// --- Minimum value clamping ---

TEST_F(RawTimingsToGcSendirTest, VerySmallValueClampedToOne) {
    // 1 µs at 15000 Hz: round(1 * 15000 / 1000000) = round(0.015) = 0 → clamped to 1
    uint16_t timings[] = {1, 1};
    result_ = raw_timings_to_gc_sendir(timings, 2, 15000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,15000,1,1,1,1\r");
}

TEST_F(RawTimingsToGcSendirTest, ZeroTimingClampedToOne) {
    // A non-overflow 0 at the start should result in period=0 → clamped to 1
    // Note: a 0 at position 0 is not an overflow skip, it's just a zero-duration pulse.
    // Actually, a 0 at an even index (mark) that is NOT preceded by a value
    // is a real 0 value, not a skip.
    // Hmm - this depends on interpretation. The skip logic triggers when
    // timings_us[i] == 0 and i+1 < timings_len, meaning the NEXT value continues.
    // At index 0: 0 is just a timing value.
    uint16_t timings[] = {0, 200};
    result_ = raw_timings_to_gc_sendir(timings, 2, 38000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    // 0 µs → 0 periods → clamped to 1
    // 200 µs → round(200 * 38000 / 1000000) = round(7.6) = 8
    EXPECT_STREQ(result_, "sendir,1:1,1,38000,1,1,1,8\r");
}

// --- Connector address variants ---

TEST_F(RawTimingsToGcSendirTest, ConnectorAddress1_2) {
    uint16_t timings[] = {560, 1690};
    result_ = raw_timings_to_gc_sendir(timings, 2, 38000, "1:2", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_TRUE(strstr(result_, "sendir,1:2,") != nullptr);
}

TEST_F(RawTimingsToGcSendirTest, ConnectorAddress1_3) {
    uint16_t timings[] = {560, 1690};
    result_ = raw_timings_to_gc_sendir(timings, 2, 38000, "1:3", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_TRUE(strstr(result_, "sendir,1:3,") != nullptr);
}

// --- ID variants ---

TEST_F(RawTimingsToGcSendirTest, IdZero) {
    uint16_t timings[] = {560, 1690};
    result_ = raw_timings_to_gc_sendir(timings, 2, 38000, "1:1", 0);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,0,38000,1,1,21,64\r");
}

TEST_F(RawTimingsToGcSendirTest, IdMax) {
    uint16_t timings[] = {560, 1690};
    result_ = raw_timings_to_gc_sendir(timings, 2, 38000, "1:1", 65535);
    ASSERT_NE(result_, nullptr);
    EXPECT_TRUE(strstr(result_, "sendir,1:1,65535,38000,1,1,") != nullptr);
}

// --- High frequency conversion ---

TEST_F(RawTimingsToGcSendirTest, HighFrequency455kHz) {
    // 100 µs at 455000 Hz: round(100 * 455000 / 1000000) = round(45.5) = 46
    // 200 µs at 455000 Hz: round(200 * 455000 / 1000000) = round(91.0) = 91
    uint16_t timings[] = {100, 200};
    result_ = raw_timings_to_gc_sendir(timings, 2, 455000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,455000,1,1,46,91\r");
}

// --- Typical NEC protocol pattern ---

TEST_F(RawTimingsToGcSendirTest, TypicalNecLeaderAndBits) {
    // NEC leader: 9000 µs mark, 4500 µs space
    // Bit 1: 560 µs mark, 1690 µs space
    // Bit 0: 560 µs mark, 560 µs space
    // Final: 560 µs mark, 560 µs space (stop bit)
    uint16_t timings[] = {9000, 4500, 560, 1690, 560, 560, 560, 560};
    result_ = raw_timings_to_gc_sendir(timings, 8, 38000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,38000,1,1,342,171,21,64,21,21,21,21\r");
}

// --- UINT16_MAX as a normal timing (no overflow, no skip follows) ---

TEST_F(RawTimingsToGcSendirTest, Uint16MaxAsNormalTiming) {
    // 65535 µs at 38kHz: round(65535 * 38000 / 1000000) = round(2490.33) = 2490
    // Next value is NOT 0, so no overflow accumulation.
    uint16_t timings[] = {65535, 65535};
    result_ = raw_timings_to_gc_sendir(timings, 2, 38000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,38000,1,1,2490,2490\r");
}

// --- Overflow: 0 only triggers skip if next element exists ---

TEST_F(RawTimingsToGcSendirTest, ZeroAsLastElementIsNotSkip) {
    // [560, 0] - the 0 is at position 1 (last element).
    // The skip condition requires i+1 < timings_len, which is false for last element.
    // So 0 is a normal space value → clamped to 1 period.
    uint16_t timings[] = {560, 0};
    result_ = raw_timings_to_gc_sendir(timings, 2, 38000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,38000,1,1,21,1\r");
}

TEST_F(RawTimingsToGcSendirTest, ZeroAsSecondToLastIsNotSkip) {
    // [560, 1690, 0] - 3 elements.
    // Index 2: value is 0. Check: i+1 (=3) < timings_len (=3)? No → not a skip.
    // So it's a logical pulse with 0 µs → clamped to 1.
    // Logical count = 3 (odd) → last value (1) duplicated.
    uint16_t timings[] = {560, 1690, 0};
    result_ = raw_timings_to_gc_sendir(timings, 3, 38000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,38000,1,1,21,64,1,1\r");
}

// --- Multiple consecutive overflows in different positions ---

TEST_F(RawTimingsToGcSendirTest, OverflowMarkThenOverflowSpace) {
    // Mark overflow: [65535, 0, 4465] = 70000 µs mark
    // Space overflow: [65535, 0, 4465] = 70000 µs space
    // At 38kHz: round(70000 * 38000 / 1000000) = 2660
    uint16_t timings[] = {65535, 0, 4465, 65535, 0, 4465};
    result_ = raw_timings_to_gc_sendir(timings, 6, 38000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,38000,1,1,2660,2660\r");
}

// --- Rounding behavior ---

TEST_F(RawTimingsToGcSendirTest, RoundingDown) {
    // 550 µs at 38kHz: round(550 * 38000 / 1000000) = round(20.9) = 21
    uint16_t timings[] = {550, 550};
    result_ = raw_timings_to_gc_sendir(timings, 2, 38000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,38000,1,1,21,21\r");
}

TEST_F(RawTimingsToGcSendirTest, RoundingUp) {
    // 540 µs at 38kHz: round(540 * 38000 / 1000000) = round(20.52) = 21
    // 530 µs at 38kHz: round(530 * 38000 / 1000000) = round(20.14) = 20
    uint16_t timings[] = {540, 530};
    result_ = raw_timings_to_gc_sendir(timings, 2, 38000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    EXPECT_STREQ(result_, "sendir,1:1,1,38000,1,1,21,20\r");
}

TEST_F(RawTimingsToGcSendirTest, ExactHalfRoundsUp) {
    // Need a value where µs * freq / 1000000 = X.5
    // At 40000 Hz, period = 25 µs. 37 µs → 37/25 = 1.48 → 1. Not .5.
    // 25 µs * 2.5 = 62.5 µs → round(62.5 * 40000 / 1000000) = round(2.5) = 3 (banker's rounds to 2 in some impl.)
    // Use a known case: 500 µs at 36000 Hz → round(500*36000/1000000) = round(18.0) = 18
    // Better: 750 µs at 20000 Hz → round(750*20000/1000000) = round(15.0) = 15
    // For exact .5: 25 µs at 20000 Hz → round(25*20000/1000000) = round(0.5) = 1 (round half up)
    uint16_t timings[] = {25, 75};
    // 25 µs at 20000 Hz: round(0.5) = 1 (with round() from cmath)
    // 75 µs at 20000 Hz: round(1.5) = 2
    result_ = raw_timings_to_gc_sendir(timings, 2, 20000, "1:1", 1);
    ASSERT_NE(result_, nullptr);
    // C round() rounds .5 away from zero → 1 and 2
    EXPECT_STREQ(result_, "sendir,1:1,1,20000,1,1,1,2\r");
}

// --- Large realistic signal (even count, no overflow) ---

TEST_F(RawTimingsToGcSendirTest, LargerSignalNoOverflow) {
    // Simplified NEC-like: leader + 4 bit-pairs + stop
    uint16_t timings[] = {
        9000, 4500,  // leader
        560,  1690,  // bit 1
        560,  560,   // bit 0
        560,  1690,  // bit 1
        560,  560,   // bit 0
        560,  40000  // stop (long space)
    };
    result_ = raw_timings_to_gc_sendir(timings, 12, 38000, "1:1", 42);
    ASSERT_NE(result_, nullptr);
    // Verify header
    EXPECT_TRUE(strstr(result_, "sendir,1:1,42,38000,1,1,") != nullptr);
    // Count commas to verify 12 values after header (6 fields in header + 12 data = 17 commas total)
    int commas = 0;
    for (char *p = result_; *p; p++) {
        if (*p == ',') commas++;
    }
    // "sendir,1:1,42,38000,1,1,v1,v2,...,v12" → 5 header commas + 12 value commas - 1 = 16
    // Actually: sendir | 1:1 | 42 | 38000 | 1 | 1 | v1 | v2 | ... | v12
    //           ,       ,     ,     ,       ,   ,   ,    ,          ,
    // = 5 (header seps) + 11 (between 12 values) = but values start with comma too
    // Format: "sendir,1:1,42,38000,1,1,342,171,21,64,21,21,21,64,21,21,21,1520"
    // Commas: let's just count = 17
    EXPECT_EQ(commas, 17);
}
