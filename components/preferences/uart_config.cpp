// SPDX-FileCopyrightText: Copyright (c) 2022 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "uart_config.h"

#include <string.h>

#include "board.h"

UartConfig::UartConfig(int baud_rate, uart_word_length_t data_bits, uart_parity_t parity, uart_stop_bits_t stop_bits)
    : baud_rate(baud_rate), data_bits(data_bits), parity(parity), stop_bits(stop_bits) {}

std::unique_ptr<UartConfig> UartConfig::defaultCfg() {
    return std::make_unique<UartConfig>(9600, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_1);
}

std::unique_ptr<UartConfig> UartConfig::fromParams(int baud_rate, uint8_t data_bits, std::string parity,
                                                   std::string stop_bits) {
    if (baud_rate == 0) {
        baud_rate = 9600;
    }
    if (baud_rate < 300 || baud_rate > MAX_UART_BITRATE) {
        return nullptr;
    }

    if (data_bits == 0) {
        data_bits = 8;
    }
    if (data_bits < 5 || data_bits > 8) {
        return nullptr;
    }
    uart_word_length_t uart_data_bits = static_cast<uart_word_length_t>(data_bits - 5);

    uart_parity_t uart_parity;
    if (parity == "none") {
        uart_parity = UART_PARITY_DISABLE;
    } else if (parity == "even") {
        uart_parity = UART_PARITY_EVEN;
    } else if (parity == "odd") {
        uart_parity = UART_PARITY_ODD;
    } else {
        return nullptr;
    }

    uart_stop_bits_t uart_stop_bits;
    if (stop_bits == "1") {
        uart_stop_bits = UART_STOP_BITS_1;
    } else if (stop_bits == "1.5") {
        uart_stop_bits = UART_STOP_BITS_1_5;
    } else if (stop_bits == "2") {
        uart_stop_bits = UART_STOP_BITS_2;
    } else {
        return nullptr;
    }

    return std::make_unique<UartConfig>(baud_rate, uart_data_bits, uart_parity, uart_stop_bits);
}

std::unique_ptr<UartConfig> UartConfig::fromString(const char* cfg) {
    // max length is a baudrate in the single megabit with 1.5 stop bits: "1500000:8N1.5"
    if (!cfg || strlen(cfg) > 13) {
        return nullptr;
    }

    int  baud_rate;
    int  data;
    char par;
    // safe to use sscanf: max input length check above with big enough buffer
    char stop[10];
    if (sscanf(cfg, "%d:%1d%c%3s", &baud_rate, &data, &par, stop) != 4) {
        return nullptr;
    }

    if (baud_rate < 300 || baud_rate > MAX_UART_BITRATE) {
        return nullptr;
    }

    uart_word_length_t data_bits;
    uart_parity_t      parity;
    uart_stop_bits_t   stop_bits;

    switch (data) {
        case 5:
            data_bits = UART_DATA_5_BITS;
            break;
        case 6:
            data_bits = UART_DATA_6_BITS;
            break;
        case 7:
            data_bits = UART_DATA_7_BITS;
            break;
        case 8:
            data_bits = UART_DATA_8_BITS;
            break;
        default:
            return nullptr;
    }

    switch (par) {
        case 'n':
        case 'N':
            parity = UART_PARITY_DISABLE;
            break;
        case 'e':
        case 'E':
            parity = UART_PARITY_EVEN;
            break;
        case 'o':
        case 'O':
            parity = UART_PARITY_ODD;
            break;
        default:
            return nullptr;
    }

    if (strncmp(stop, "1", sizeof(stop)) == 0) {
        stop_bits = UART_STOP_BITS_1;
    } else if (strncmp(stop, "1.5", sizeof(stop)) == 0) {
        stop_bits = UART_STOP_BITS_1_5;
    } else if (strncmp(stop, "2", sizeof(stop)) == 0) {
        stop_bits = UART_STOP_BITS_2;
    } else {
        return nullptr;
    }

    return std::make_unique<UartConfig>(baud_rate, data_bits, parity, stop_bits);
}

std::string UartConfig::toString() {
    uint8_t bits = dataBits();

    char par;
    switch (parity) {
        case UART_PARITY_EVEN:
            par = 'E';
            break;
        case UART_PARITY_ODD:
            par = 'O';
            break;
        default:
            par = 'N';
    }

    const char* stop = stopBitsAsString();

    // std::format in C++ 20 would be nice, but still not available on some systems :-(
    char buff[20];
    snprintf(buff, sizeof(buff), "%d:%d%c%s", baud_rate, bits, par, stop);

    return buff;
}

uart_config_t UartConfig::toConfig() {
    return {.baud_rate = static_cast<int>(baud_rate),
            .data_bits = data_bits,
            .parity = parity,
            .stop_bits = stop_bits,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 0,
            .source_clk = UART_SCLK_DEFAULT,
            .flags{
                .allow_pd = 0,
                .backup_before_sleep = 0,
            }};
}

uint8_t UartConfig::dataBits() {
    switch (data_bits) {
        case UART_DATA_5_BITS:
            return 5;
        case UART_DATA_6_BITS:
            return 6;
        case UART_DATA_7_BITS:
            return 7;
        default:
            return 8;
    }
}

const char* UartConfig::parityAsString() {
    switch (parity) {
        case UART_PARITY_EVEN:
            return "even";
        case UART_PARITY_ODD:
            return "odd";
        default:
            return "none";
    }
}

const char* UartConfig::stopBitsAsString() {
    switch (stop_bits) {
        case UART_STOP_BITS_1_5:
            return "1.5";
        case UART_STOP_BITS_2:
            return "2";
        default:
            return "1";
    }
}
