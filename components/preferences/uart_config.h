// SPDX-FileCopyrightText: Copyright (c) 2022 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <string>

#include "driver/uart.h"

#define DEF_EXT_PORT_UART_CFG "9600:8N1"

/// @brief User configurable UART settings.
///
/// Convert from and to string representations in format "$BAUDRATE:$DATA_BITS$PARITY$STOP_BITS".
///
/// Internal representation is in IDF UART SDK format.
class UartConfig {
 public:
    UartConfig(int baud_rate, uart_word_length_t data_bits, uart_parity_t parity, uart_stop_bits_t stop_bits);

    static std::unique_ptr<UartConfig> defaultCfg();
    static std::unique_ptr<UartConfig> fromParams(int baud_rate, uint8_t data_bits, std::string parity,
                                                  std::string stop_bits);
    static std::unique_ptr<UartConfig> fromString(const char* cfg);

    std::string toString();

    uart_config_t toConfig();

 public:
    /// @brief Get number of data bits (5, 6, 7, or 8)
    uint8_t     dataBits();
    const char* parityAsString();
    const char* stopBitsAsString();

    // UART baud rate
    int baud_rate;  // int because of SDK definition
    // UART data bits length enum. This is NOT the number of bits!
    uart_word_length_t data_bits;
    // UART parity
    uart_parity_t parity;
    // UART stop bits length
    uart_stop_bits_t stop_bits;
};
