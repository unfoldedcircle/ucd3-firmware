// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>
#include <string.h>

#include "string_util.h"

TEST(StringUtilTest, PrintableStringWithAnEmptySSID) {
    char   ssid[33];
    size_t len = sizeof(ssid);
    memset(ssid, 0, len);

    EXPECT_EQ("", toPrintableString((uint8_t *)ssid, len));
}

TEST(StringUtilTest, PrintableString) {
    const char *buf = "0123456789";
    size_t      len = strlen(buf);

    EXPECT_EQ("0123456789", toPrintableString((uint8_t *)buf, len));
}

TEST(StringUtilTest, PrintableStringWithNonPrintableChars) {
    const char buf[] = {1, 2, 3, '\r', '\n', '\t'};
    size_t     len = sizeof(buf);

    EXPECT_EQ("\\x01\\x02\\x03\\r\\n\\t", toPrintableString((uint8_t *)buf, len));
}

TEST(StringUtilTest, PrintableStringWithNullCharacter) {
    const char buf[] = {1, 2, 3, 0, '\r', '\n', '\t'};
    size_t     len = sizeof(buf);

    EXPECT_EQ("\\x01\\x02\\x03", toPrintableString((uint8_t *)buf, len));
}

TEST(StringUtilTest, PrintableStringWithEmojis) {
    const char buf[] = "hello 😎🚀👍";
    size_t     len = strlen(buf);

    EXPECT_EQ("hello \\xf0\\x9f\\x98\\x8e\\xf0\\x9f\\x9a\\x80\\xf0\\x9f\\x91\\x8d",
              toPrintableString((uint8_t *)buf, len));
}

TEST(StringUtilTest, ReplacecharWithNullInput) {
    EXPECT_EQ(0, replacechar(nullptr, 'a', 'b'));
}

TEST(StringUtilTest, ReplacecharWithEmptyInput) {
    char buf[1];
    buf[0] = 0;
    EXPECT_EQ(0, replacechar(buf, 'a', 'b'));
    EXPECT_EQ(0, buf[0]);
}

TEST(StringUtilTest, ReplacecharNoMatch) {
    char buf[20];
    snprintf(buf, sizeof(buf), "foobar");
    EXPECT_EQ(0, replacechar(buf, 'c', 'd'));
    EXPECT_STREQ("foobar", buf);
}

TEST(StringUtilTest, ReplacecharSingleMatch) {
    char buf[20];
    snprintf(buf, sizeof(buf), "foobar");
    EXPECT_EQ(1, replacechar(buf, 'r', 's'));
    EXPECT_STREQ("foobas", buf);

    EXPECT_EQ(1, replacechar(buf, 'f', 'r'));
    EXPECT_STREQ("roobas", buf);

    EXPECT_EQ(1, replacechar(buf, 'b', 'r'));
    EXPECT_STREQ("rooras", buf);
}

TEST(StringUtilTest, ReplacecharMultiMatch) {
    char buf[20];
    snprintf(buf, sizeof(buf), "foobar");
    EXPECT_EQ(2, replacechar(buf, 'o', 'u'));
    EXPECT_STREQ("fuubar", buf);
}
