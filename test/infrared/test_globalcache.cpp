// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "globalcache.h"

TEST(GlobalCacheTest, parseGcRequest_nullInput) {
    const char *request = "blink";
    GCMsg       msg;
    EXPECT_EQ(0, parseGcRequest(request, nullptr));
    EXPECT_EQ(0, parseGcRequest(nullptr, &msg));
    EXPECT_EQ(0, parseGcRequest(nullptr, nullptr));
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
