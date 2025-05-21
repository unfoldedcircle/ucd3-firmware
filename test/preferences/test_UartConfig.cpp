// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "uart_config.h"

TEST(UartConfigTest, fromStringReturnsNullWithNullOrEmptyInput) {
    EXPECT_EQ(nullptr, UartConfig::fromString(nullptr));
    EXPECT_EQ(nullptr, UartConfig::fromString(""));
}

TEST(UartConfigTest, fromStringReturnsNullWithInvalidBaudrate) {
    EXPECT_EQ(nullptr, UartConfig::fromString(":8N1"));
    EXPECT_EQ(nullptr, UartConfig::fromString("0:8N1"));
    EXPECT_EQ(nullptr, UartConfig::fromString("1:8N1"));
    EXPECT_EQ(nullptr, UartConfig::fromString("299:8N1"));
    EXPECT_EQ(nullptr, UartConfig::fromString("115201:8N1"));
    EXPECT_EQ(nullptr, UartConfig::fromString("10123456:8N1"));
}

TEST(UartConfigTest, fromStringReturnsNullWithInvalidDataBits) {
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:0N1"));
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:1N1"));
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:2N1"));
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:3N1"));
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:4N1"));
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:9N1"));
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:10N1"));
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:100N1"));
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:10000000000N1"));
}

TEST(UartConfigTest, fromStringReturnsNullWithInvalidParity) {
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:801"));
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:811"));
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:821"));
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:831"));
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:8D1"));
}

TEST(UartConfigTest, fromStringReturnsNullWithInvalidStopBits) {
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:8N0"));
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:8N0"));
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:8N0"));
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:8N3"));
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:8N2.5"));
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:8N0.5"));
    EXPECT_EQ(nullptr, UartConfig::fromString("9600:8NZ"));
}

TEST(UartConfigTest, fromString) {
    auto cfg = UartConfig::fromString("9600:8N1");
    EXPECT_EQ(9600, cfg->baud_rate);
    EXPECT_EQ(UART_DATA_8_BITS, cfg->data_bits);
    EXPECT_EQ(UART_PARITY_DISABLE, cfg->parity);
    EXPECT_EQ(UART_STOP_BITS_1, cfg->stop_bits);
}

TEST(UartConfigTest, fromStringWithMinBaudrate) {
    auto cfg = UartConfig::fromString("300:8N1");
    EXPECT_EQ(300, cfg->baud_rate);
}

TEST(UartConfigTest, fromStringWithMaxBaudrate) {
    auto cfg = UartConfig::fromString("115200:8N1");
    EXPECT_EQ(115200, cfg->baud_rate);
}

TEST(UartConfigTest, fromParams) {
    auto cfg = UartConfig::fromParams(19200, 8, "none", "1");
    EXPECT_EQ(19200, cfg->baud_rate);
    EXPECT_EQ(UART_DATA_8_BITS, cfg->data_bits);
    EXPECT_EQ(UART_PARITY_DISABLE, cfg->parity);
    EXPECT_EQ(UART_STOP_BITS_1, cfg->stop_bits);

    cfg = UartConfig::fromParams(38400, 7, "odd", "1.5");
    EXPECT_EQ(38400, cfg->baud_rate);
    EXPECT_EQ(UART_DATA_7_BITS, cfg->data_bits);
    EXPECT_EQ(UART_PARITY_ODD, cfg->parity);
    EXPECT_EQ(UART_STOP_BITS_1_5, cfg->stop_bits);

    cfg = UartConfig::fromParams(38400, 6, "even", "2");
    EXPECT_EQ(38400, cfg->baud_rate);
    EXPECT_EQ(UART_DATA_6_BITS, cfg->data_bits);
    EXPECT_EQ(UART_PARITY_EVEN, cfg->parity);
    EXPECT_EQ(UART_STOP_BITS_2, cfg->stop_bits);
}

TEST(UartConfigTest, toStringBaudrate) {
    UartConfig cfg(300, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_1);
    EXPECT_EQ("300:8N1", cfg.toString());
}

TEST(UartConfigTest, toStringStopBits) {
    EXPECT_EQ("9600:8N1", UartConfig(9600, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_1).toString());
    EXPECT_EQ("9600:8N1.5", UartConfig(9600, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_1_5).toString());
    EXPECT_EQ("9600:8N2", UartConfig(9600, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_2).toString());
}

TEST(UartConfigTest, toStringParity) {
    EXPECT_EQ("9600:8N1", UartConfig(9600, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_1).toString());
    EXPECT_EQ("9600:8E1.5", UartConfig(9600, UART_DATA_8_BITS, UART_PARITY_EVEN, UART_STOP_BITS_1_5).toString());
    EXPECT_EQ("9600:8O2", UartConfig(9600, UART_DATA_8_BITS, UART_PARITY_ODD, UART_STOP_BITS_2).toString());
}

TEST(UartConfigTest, toStringDataBits) {
    EXPECT_EQ("9600:5N1", UartConfig(9600, UART_DATA_5_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_1).toString());
    EXPECT_EQ("9600:6E1.5", UartConfig(9600, UART_DATA_6_BITS, UART_PARITY_EVEN, UART_STOP_BITS_1_5).toString());
    EXPECT_EQ("9600:7O2", UartConfig(9600, UART_DATA_7_BITS, UART_PARITY_ODD, UART_STOP_BITS_2).toString());
    EXPECT_EQ("9600:8O2", UartConfig(9600, UART_DATA_8_BITS, UART_PARITY_ODD, UART_STOP_BITS_2).toString());
}

TEST(UartConfigTest, toString) {
    UartConfig cfg(9600, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_1);
    EXPECT_EQ("9600:8N1", cfg.toString());
}
