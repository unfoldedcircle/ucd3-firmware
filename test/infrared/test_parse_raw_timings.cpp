// SPDX-FileCopyrightText: Copyright (c) 2026 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtest/gtest.h"
#include "raw_timings.h"

class ParseRawTimingsTest : public ::testing::Test {
 protected:
    void TearDown() override {
        if (result_.buf != nullptr) {
            free(result_.buf);
            result_.buf = nullptr;
        }
    }

    RawParseResult result_ = {nullptr, 0, 38000, RawParseError::OK, 0};
};

// --- Happy path: unsigned input ---

TEST_F(ParseRawTimingsTest, UnsignedSimplePair) {
    const char *input = "9004 4500";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    EXPECT_EQ(result_.hz, 38000);
    ASSERT_EQ(result_.len, 2);
    EXPECT_EQ(result_.buf[0], 9004);
    EXPECT_EQ(result_.buf[1], 4500);
}

TEST_F(ParseRawTimingsTest, UnsignedMultiplePairs) {
    const char *input = "9004 4500 560 1690";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    EXPECT_EQ(result_.hz, 38000);
    ASSERT_EQ(result_.len, 4);
    EXPECT_EQ(result_.buf[0], 9004);
    EXPECT_EQ(result_.buf[1], 4500);
    EXPECT_EQ(result_.buf[2], 560);
    EXPECT_EQ(result_.buf[3], 1690);
}

// --- Happy path: signed input ---

TEST_F(ParseRawTimingsTest, SignedPositiveAndNegative) {
    const char *input = "+9004 -4500 +560 -1690";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    EXPECT_EQ(result_.hz, 38000);
    ASSERT_EQ(result_.len, 4);
    EXPECT_EQ(result_.buf[0], 9004);
    EXPECT_EQ(result_.buf[1], 4500);
    EXPECT_EQ(result_.buf[2], 560);
    EXPECT_EQ(result_.buf[3], 1690);
}

TEST_F(ParseRawTimingsTest, SignedAllNegative) {
    const char *input = "-100 -200";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 2);
    EXPECT_EQ(result_.buf[0], 100);
    EXPECT_EQ(result_.buf[1], 200);
}

TEST_F(ParseRawTimingsTest, SignedAllPositive) {
    const char *input = "+100 +200";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 2);
    EXPECT_EQ(result_.buf[0], 100);
    EXPECT_EQ(result_.buf[1], 200);
}

// --- Happy path: mixed signed/unsigned ---

TEST_F(ParseRawTimingsTest, MixedSignedUnsigned) {
    const char *input = "+9004 4500 -560 1690";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 4);
    EXPECT_EQ(result_.buf[0], 9004);
    EXPECT_EQ(result_.buf[1], 4500);
    EXPECT_EQ(result_.buf[2], 560);
    EXPECT_EQ(result_.buf[3], 1690);
}

// --- Happy path: odd count is valid ---

TEST_F(ParseRawTimingsTest, OddCountSingle) {
    const char *input = "9004";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 1);
    EXPECT_EQ(result_.buf[0], 9004);
}

TEST_F(ParseRawTimingsTest, OddCountThree) {
    const char *input = "9004 4500 560";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 3);
    EXPECT_EQ(result_.buf[0], 9004);
    EXPECT_EQ(result_.buf[1], 4500);
    EXPECT_EQ(result_.buf[2], 560);
}

TEST_F(ParseRawTimingsTest, OddCountFive) {
    const char *input = "9004 4500 560 1690 560";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 5);
    EXPECT_EQ(result_.buf[0], 9004);
    EXPECT_EQ(result_.buf[1], 4500);
    EXPECT_EQ(result_.buf[2], 560);
    EXPECT_EQ(result_.buf[3], 1690);
    EXPECT_EQ(result_.buf[4], 560);
}

// --- Frequency prefix: not present (default) ---

TEST_F(ParseRawTimingsTest, NoFrequencyDefaultValue) {
    const char *input = "3354 1700 380 452";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    EXPECT_EQ(result_.hz, 38000);
    ASSERT_EQ(result_.len, 4);
}

TEST_F(ParseRawTimingsTest, NoFrequencyWithLeadingWhitespace) {
    const char *input = "  3354 -1700 380 -452";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    EXPECT_EQ(result_.hz, 38000);
    ASSERT_EQ(result_.len, 4);
}

// --- Frequency prefix: present ---

TEST_F(ParseRawTimingsTest, LearnedRawCodes) {
    const char *input =
        "38000:3354 -1700 380 -452 386 -446 392 -1300 372 -450 394 -1294 374 -452 392 -1280 388 -448 388 -448 388 "
        "-1286 388 -448 388 -448 388 -1286 388 -1286 388 -448 388 -448 388 -448 388 -448 392 -446 392 -446 386 -1288 "
        "388 -450 392 -1284 384 -448 388 -1288 388 -450 388 -450 388 -450 388 -1288 388 -448 388 -1286 388 -448 388 "
        "-448 388 -454 384 -448 388 -448 388 -448 388 -448 388 -1286 388 -448 388 -1286 388 -448 388 -448 388 -448 388 "
        "-448 388 -448 388 -1286 388 -448 388 50000";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    EXPECT_EQ(result_.hz, 38000);
    ASSERT_EQ(result_.len, 100);
    EXPECT_EQ(result_.buf[0], 3354);
    EXPECT_EQ(result_.buf[1], 1700);
    EXPECT_EQ(result_.buf[2], 380);
    EXPECT_EQ(result_.buf[3], 452);
}

TEST_F(ParseRawTimingsTest, FrequencyPresent37000) {
    const char *input = "37000:3354 -1700 380 -452";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    EXPECT_EQ(result_.hz, 37000);
    ASSERT_EQ(result_.len, 4);
    EXPECT_EQ(result_.buf[0], 3354);
    EXPECT_EQ(result_.buf[1], 1700);
    EXPECT_EQ(result_.buf[2], 380);
    EXPECT_EQ(result_.buf[3], 452);
}

TEST_F(ParseRawTimingsTest, FrequencyPresent40000) {
    const char *input = "40000:3354 -1700 380 -452";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    EXPECT_EQ(result_.hz, 40000);
    ASSERT_EQ(result_.len, 4);
}

TEST_F(ParseRawTimingsTest, FrequencyPresentWithLeadingWhitespace) {
    const char *input = " 37000:3354 -1700 380 -452";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    EXPECT_EQ(result_.hz, 37000);
    ASSERT_EQ(result_.len, 4);
    EXPECT_EQ(result_.buf[0], 3354);
}

TEST_F(ParseRawTimingsTest, FrequencyPresentWithTrailingWhitespaceAfterColon) {
    const char *input = "40000: 3354 -1700 380 -452";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    EXPECT_EQ(result_.hz, 40000);
    ASSERT_EQ(result_.len, 4);
    EXPECT_EQ(result_.buf[0], 3354);
}

TEST_F(ParseRawTimingsTest, FrequencyPresentWithMultipleWhitespaceAfterColon) {
    const char *input = "36000:   3354 -1700 380 -452";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    EXPECT_EQ(result_.hz, 36000);
    ASSERT_EQ(result_.len, 4);
    EXPECT_EQ(result_.buf[0], 3354);
}

TEST_F(ParseRawTimingsTest, FrequencyPresentWithLeadingAndTrailingWhitespace) {
    const char *input = "  40000: 3354 -1700 380 -452  ";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    EXPECT_EQ(result_.hz, 40000);
    ASSERT_EQ(result_.len, 4);
}

TEST_F(ParseRawTimingsTest, FrequencyPresentWithTabAfterColon) {
    const char *input = "38000:\t3354 1700";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    EXPECT_EQ(result_.hz, 38000);
    ASSERT_EQ(result_.len, 2);
    EXPECT_EQ(result_.buf[0], 3354);
    EXPECT_EQ(result_.buf[1], 1700);
}

TEST_F(ParseRawTimingsTest, FrequencyPresentNoWhitespaceAfterColon) {
    const char *input = "56000:3354 1700";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    EXPECT_EQ(result_.hz, 56000);
    ASSERT_EQ(result_.len, 2);
    EXPECT_EQ(result_.buf[0], 3354);
    EXPECT_EQ(result_.buf[1], 1700);
}

TEST_F(ParseRawTimingsTest, FrequencyZero) {
    const char *input = "0:3354 1700";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    EXPECT_EQ(result_.hz, 0);
    ASSERT_EQ(result_.len, 2);
}

TEST_F(ParseRawTimingsTest, FrequencyMaxUint16) {
    const char *input = "65535:3354 1700";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    EXPECT_EQ(result_.hz, 65535);
    ASSERT_EQ(result_.len, 2);
}

TEST_F(ParseRawTimingsTest, FrequencyWithSignedTimings) {
    const char *input = "37000:+3354 -1700 +380 -452";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    EXPECT_EQ(result_.hz, 37000);
    ASSERT_EQ(result_.len, 4);
    EXPECT_EQ(result_.buf[0], 3354);
    EXPECT_EQ(result_.buf[1], 1700);
}

TEST_F(ParseRawTimingsTest, FrequencyWithOddTimingCount) {
    const char *input = "37000:3354 1700 380";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    EXPECT_EQ(result_.hz, 37000);
    ASSERT_EQ(result_.len, 3);
}

// --- Error: invalid frequency ---

TEST_F(ParseRawTimingsTest, FrequencyExceedsUint16) {
    const char *input = "65536:3354 1700";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::INVALID_FREQUENCY);
    EXPECT_EQ(result_.buf, nullptr);
}

TEST_F(ParseRawTimingsTest, FrequencyFarExceedsUint16) {
    const char *input = "100000:3354 1700";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::INVALID_FREQUENCY);
    EXPECT_EQ(result_.buf, nullptr);
}

TEST_F(ParseRawTimingsTest, FrequencyNonNumeric) {
    const char *input = "abc:3354 1700";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::INVALID_FREQUENCY);
    EXPECT_EQ(result_.buf, nullptr);
}

TEST_F(ParseRawTimingsTest, FrequencyEmptyBeforeColon) {
    const char *input = ":3354 1700";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::INVALID_FREQUENCY);
    EXPECT_EQ(result_.buf, nullptr);
}

TEST_F(ParseRawTimingsTest, FrequencyWithColonButNoTimings) {
    const char *input = "38000:";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::EMPTY_INPUT);
    EXPECT_EQ(result_.buf, nullptr);
}

TEST_F(ParseRawTimingsTest, FrequencyWithColonAndOnlyWhitespace) {
    const char *input = "38000:   ";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::EMPTY_INPUT);
    EXPECT_EQ(result_.buf, nullptr);
}

// --- Whitespace variants ---

TEST_F(ParseRawTimingsTest, MultipleSpaces) {
    const char *input = "9004    4500";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 2);
    EXPECT_EQ(result_.buf[0], 9004);
    EXPECT_EQ(result_.buf[1], 4500);
}

TEST_F(ParseRawTimingsTest, TabSeparator) {
    const char *input = "9004\t4500";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 2);
    EXPECT_EQ(result_.buf[0], 9004);
    EXPECT_EQ(result_.buf[1], 4500);
}

TEST_F(ParseRawTimingsTest, MixedWhitespace) {
    const char *input = "9004 \t  4500\t\t560  1690";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 4);
    EXPECT_EQ(result_.buf[0], 9004);
    EXPECT_EQ(result_.buf[1], 4500);
    EXPECT_EQ(result_.buf[2], 560);
    EXPECT_EQ(result_.buf[3], 1690);
}

TEST_F(ParseRawTimingsTest, NewlineSeparator) {
    const char *input = "9004\n4500";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 2);
    EXPECT_EQ(result_.buf[0], 9004);
    EXPECT_EQ(result_.buf[1], 4500);
}

TEST_F(ParseRawTimingsTest, CarriageReturnLinefeed) {
    const char *input = "9004\r\n4500";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 2);
    EXPECT_EQ(result_.buf[0], 9004);
    EXPECT_EQ(result_.buf[1], 4500);
}

TEST_F(ParseRawTimingsTest, NonBreakingSpace) {
    std::string input =
        "9004\xC2\xA0"
        "4500";
    result_ = parse_raw_timings(input.data(), input.size());
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 2);
    EXPECT_EQ(result_.buf[0], 9004);
    EXPECT_EQ(result_.buf[1], 4500);
}

TEST_F(ParseRawTimingsTest, MultipleNonBreakingSpaces) {
    std::string input =
        "9004\xC2\xA0\xC2\xA0"
        "4500";
    result_ = parse_raw_timings(input.data(), input.size());
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 2);
    EXPECT_EQ(result_.buf[0], 9004);
    EXPECT_EQ(result_.buf[1], 4500);
}

TEST_F(ParseRawTimingsTest, NbspMixedWithRegularSpaces) {
    std::string input = "9004 \xC2\xA0 4500";
    result_ = parse_raw_timings(input.data(), input.size());
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 2);
    EXPECT_EQ(result_.buf[0], 9004);
    EXPECT_EQ(result_.buf[1], 4500);
}

// --- Leading and trailing whitespace ---

TEST_F(ParseRawTimingsTest, LeadingWhitespace) {
    const char *input = "   9004 4500";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 2);
    EXPECT_EQ(result_.buf[0], 9004);
    EXPECT_EQ(result_.buf[1], 4500);
}

TEST_F(ParseRawTimingsTest, TrailingWhitespace) {
    const char *input = "9004 4500   ";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 2);
    EXPECT_EQ(result_.buf[0], 9004);
    EXPECT_EQ(result_.buf[1], 4500);
}

TEST_F(ParseRawTimingsTest, LeadingAndTrailingWhitespace) {
    const char *input = "\t  9004 4500  \t\n";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 2);
    EXPECT_EQ(result_.buf[0], 9004);
    EXPECT_EQ(result_.buf[1], 4500);
}

// --- Boundary values ---

TEST_F(ParseRawTimingsTest, ZeroValues) {
    const char *input = "0 0";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 2);
    EXPECT_EQ(result_.buf[0], 0);
    EXPECT_EQ(result_.buf[1], 0);
}

TEST_F(ParseRawTimingsTest, MaxUint16) {
    const char *input = "65535 65535";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 2);
    EXPECT_EQ(result_.buf[0], 65535);
    EXPECT_EQ(result_.buf[1], 65535);
}

TEST_F(ParseRawTimingsTest, SingleDigitValues) {
    const char *input = "1 2";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::OK);
    ASSERT_EQ(result_.len, 2);
    EXPECT_EQ(result_.buf[0], 1);
    EXPECT_EQ(result_.buf[1], 2);
}

// --- Error: empty input ---

TEST_F(ParseRawTimingsTest, EmptyString) {
    const char *input = "";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::EMPTY_INPUT);
    EXPECT_EQ(result_.buf, nullptr);
}

TEST_F(ParseRawTimingsTest, OnlyWhitespace) {
    const char *input = "   \t  \n  ";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::EMPTY_INPUT);
    EXPECT_EQ(result_.buf, nullptr);
}

TEST_F(ParseRawTimingsTest, OnlyNbsp) {
    std::string input = "\xC2\xA0\xC2\xA0";
    result_ = parse_raw_timings(input.data(), input.size());
    EXPECT_EQ(result_.error, RawParseError::EMPTY_INPUT);
    EXPECT_EQ(result_.buf, nullptr);
}

TEST_F(ParseRawTimingsTest, NullPointerZeroLength) {
    result_ = parse_raw_timings(nullptr, 0);
    EXPECT_EQ(result_.error, RawParseError::EMPTY_INPUT);
    EXPECT_EQ(result_.buf, nullptr);
}

// --- Error: invalid token ---

TEST_F(ParseRawTimingsTest, InvalidTokenLetters) {
    const char *input = "9004 abc";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::INVALID_TOKEN);
    EXPECT_EQ(result_.error_token_index, 1);
    EXPECT_EQ(result_.buf, nullptr);
}

TEST_F(ParseRawTimingsTest, InvalidTokenFirstPosition) {
    const char *input = "xyz 4500";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::INVALID_TOKEN);
    EXPECT_EQ(result_.error_token_index, 0);
    EXPECT_EQ(result_.buf, nullptr);
}

TEST_F(ParseRawTimingsTest, InvalidTokenSignOnly) {
    const char *input = "+ 4500";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::INVALID_TOKEN);
    EXPECT_EQ(result_.error_token_index, 0);
    EXPECT_EQ(result_.buf, nullptr);
}

TEST_F(ParseRawTimingsTest, InvalidTokenMinusOnly) {
    const char *input = "- 4500";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::INVALID_TOKEN);
    EXPECT_EQ(result_.error_token_index, 0);
    EXPECT_EQ(result_.buf, nullptr);
}

TEST_F(ParseRawTimingsTest, InvalidTokenSignFollowedByLetter) {
    const char *input = "+abc 4500";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::INVALID_TOKEN);
    EXPECT_EQ(result_.error_token_index, 0);
    EXPECT_EQ(result_.buf, nullptr);
}

TEST_F(ParseRawTimingsTest, InvalidTokenSpecialChars) {
    const char *input = "9004 #4500";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::INVALID_TOKEN);
    EXPECT_EQ(result_.error_token_index, 1);
    EXPECT_EQ(result_.buf, nullptr);
}

TEST_F(ParseRawTimingsTest, InvalidTokenMiddlePosition) {
    const char *input = "9004 4500 bad 1690";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::INVALID_TOKEN);
    EXPECT_EQ(result_.error_token_index, 2);
    EXPECT_EQ(result_.buf, nullptr);
}

// --- Error: value out of range ---

TEST_F(ParseRawTimingsTest, ValueExceedsUint16) {
    const char *input = "9004 65536";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::VALUE_OUT_OF_RANGE);
    EXPECT_EQ(result_.error_token_index, 1);
    EXPECT_EQ(result_.buf, nullptr);
}

TEST_F(ParseRawTimingsTest, ValueFarExceedsUint16) {
    const char *input = "9004 100000";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::VALUE_OUT_OF_RANGE);
    EXPECT_EQ(result_.error_token_index, 1);
    EXPECT_EQ(result_.buf, nullptr);
}

TEST_F(ParseRawTimingsTest, ValueOverflowFirstToken) {
    const char *input = "70000 4500";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::VALUE_OUT_OF_RANGE);
    EXPECT_EQ(result_.error_token_index, 0);
    EXPECT_EQ(result_.buf, nullptr);
}

TEST_F(ParseRawTimingsTest, SignedValueExceedsUint16) {
    const char *input = "+9004 -65536";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::VALUE_OUT_OF_RANGE);
    EXPECT_EQ(result_.error_token_index, 1);
    EXPECT_EQ(result_.buf, nullptr);
}

TEST_F(ParseRawTimingsTest, VeryLargeNumber) {
    const char *input = "9004 99999999999";
    result_ = parse_raw_timings(input, strlen(input));
    EXPECT_EQ(result_.error, RawParseError::VALUE_OUT_OF_RANGE);
    EXPECT_EQ(result_.error_token_index, 1);
    EXPECT_EQ(result_.buf, nullptr);
}
