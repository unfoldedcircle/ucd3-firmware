// SPDX-FileCopyrightText: Copyright (c) 2026 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "board.h"

#include <ctype.h>
#include <soc/efuse_reg.h>

#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_log.h"

#include "efuse_user.h"

static int s_revision = 4;
static int s_uart_print_ctrl_val = 0;

// SPI chipselect for KSZ8851SNL ethernet ic, LOW -> enable communication to KSZ8851SNL
// - r3: GPIO_NUM_6
// - r4: GPIO_NUM_6
// - r6: GPIO_NUM_14
static gpio_num_t s_spi_cs_pin = GPIO_NUM_6;
// INPUT, measures low side via 0.1 Ohm current to remote
// - r3: GPIO_NUM_14
// - r4: GPIO_NUM_14
// - r6: GPIO_NUM_6
static gpio_num_t s_charging_current_pin = GPIO_NUM_14;
// GPIO pin of the internal top IR output
// - r3: GPIO_NUM_7
// - r4: GPIO_NUM_7, OUTPUT,OPEN DRAIN. physically pulled up to 2.4V
// - r6: tied to IR side output
static gpio_num_t s_ir_send_pin_int_top = GPIO_NUM_7;
// Charger measurement ADC unit
// - r3: ADC_UNIT_2
// - r4: ADC_UNIT_2
// - r6: ADC_UNIT_1
static adc_unit_t s_charging_current_adc_unit = ADC_UNIT_2;
// Charger measurement ADC channel
// - r3: ADC_CHANNEL_3
// - r4: ADC_CHANNEL_3
// - r6: ADC_CHANNEL_5
static adc_channel_t s_charging_current_adc_ch = ADC_CHANNEL_3;

static const char *const TAG = "BOARD";

static int read_efuse_revision() {
    char revision[4] = {0};
    esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA_DOCK_HW_REV, &revision,
                              ESP_EFUSE_USER_DATA_DOCK_HW_REV[0]->bit_count);
    int rev = atoi(revision);
    if (rev < 3 || rev > 6) {
        ESP_LOGW(TAG,
                 "Invalid or no revision found in eFuse (%d): the device might not work properly! Using firmware "
                 "revision: %s",
                 rev, UCD_HW_REVISION_NAME);
        return atoi(UCD_HW_REVISION_NAME);
    }

    ESP_LOGI(TAG, "hw revision: %d, fw revision: %s", rev, UCD_HW_REVISION_NAME);
    return rev;
}

static uint8_t read_uart_print_ctrl_val(void) {
    uint8_t uart_print_ctrl_val = 0;
    uint8_t write_disabled = 0;

    // UART_PRINT_CONTROL is 2 bits
    esp_err_t err = esp_efuse_read_field_blob(ESP_EFUSE_UART_PRINT_CONTROL, &uart_print_ctrl_val, 2);
    esp_err_t err_dis = esp_efuse_read_field_blob(ESP_EFUSE_WR_DIS_UART_PRINT_CONTROL, &write_disabled, 1);

    if (err == ESP_OK && err_dis == ESP_OK) {
        if (uart_print_ctrl_val != 3) {
            ESP_LOGI(TAG,
                     "ROM Boot log is enabled (%d, %d). Disabling port1 RS232 mode to prevent sending invalid data on "
                     "startup",
                     uart_print_ctrl_val, write_disabled);
        }
    } else {
        ESP_LOGE(TAG, "Error reading eFuse configuration values: 0x%x, 0x%x", err, err_dis);
    }

    return uart_print_ctrl_val;
}

void board_init_revision(void) {
    s_revision = read_efuse_revision();

    if (CONFIG_UCD_ETH_SPI_CS0_GPIO >= 0) {
        s_spi_cs_pin = CONFIG_UCD_ETH_SPI_CS0_GPIO;
    } else if (s_revision == 6) {
        s_spi_cs_pin = GPIO_NUM_14;
    }

    if (s_revision == 6) {
        s_charging_current_pin = GPIO_NUM_6;
        s_charging_current_adc_unit = ADC_UNIT_1;
        s_charging_current_adc_ch = ADC_CHANNEL_5;
        // tied to IR side output on rev6+
        s_ir_send_pin_int_top = IR_SEND_PIN_INT_SIDE;
    }

    s_uart_print_ctrl_val = read_uart_print_ctrl_val();
}

int board_get_revision(void) {
    return s_revision;
}

gpio_num_t board_get_spi_cs(void) {
    return s_spi_cs_pin;
}

gpio_num_t board_get_ir_send_pin_int_top(void) {
    return s_ir_send_pin_int_top;
}

gpio_num_t board_get_poe_switch_pin(void) {
    if (s_revision == 6) {
        return GPIO_NUM_7;
    } else {
        return GPIO_NUM_NC;
    }
}

gpio_num_t board_get_charging_current_pin(void) {
    return s_charging_current_pin;
}

adc_unit_t board_get_charging_current_adc_unit(void) {
    return s_charging_current_adc_unit;
}

adc_channel_t board_get_charging_current_adc_ch(void) {
    return s_charging_current_adc_ch;
}

bool board_is_switch_ext_inverted(void) {
    return s_revision == 3 ? true : false;
}

gpio_mode_t board_switch_ext_gpio_mode(void) {
    return s_revision == 3 ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT;
}

bool board_port_supports_rs232(uint8_t port) {
    if (port < 1 || port > EXTERNAL_PORT_COUNT) {
        return false;
    }
    // port1 shares UART output of ROM boot logs
    if (port == 1) {
        // only allow RS232 if ROM boot log is muted
        return s_uart_print_ctrl_val == 3;
    }
    return true;
}
