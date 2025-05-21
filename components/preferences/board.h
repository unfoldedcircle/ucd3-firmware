// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Board configuration definition.
// All common definitions are defined here, board specific definitions are in dedicated header files. See end of file.

#pragma once

#include "esp_bit_defs.h"
#include "soc/gpio_num.h"

#include "sdkconfig.h"

#define STRINGIFY(s) STRINGIFY1(s)
#define STRINGIFY1(s) #s

// number of external ports
#define EXTERNAL_PORT_COUNT 2
// external port support for IR blasters: bit flags, bit 0 == port 1, bit 1 == port 2 etc.
#define EXTERNAL_IR_BLASTER_SUPPORT (BIT0 | BIT1)
// external port support for mono-tip IR emitters: bit flags, bit 0 == port 1, bit 1 == port 2 etc.
#define EXTERNAL_IR_EMITTER_MONO_SUPPORT (BIT0 | BIT1)
// external port support for NEW stereo-tip IR emitters: bit flags, bit 0 == port 1, bit 1 == port 2 etc.
#define EXTERNAL_IR_EMITTER_STEREO_SUPPORT (BIT0 | BIT1)
// external port support for 5V triggers: bit flags, bit 0 == port 1, bit 1 == port 2 etc.
#define EXTERNAL_5V_TRIGGER_SUPPORT (BIT0 | BIT1)
// external port support for RS232: bit flags, bit 0 == port 1, bit 1 == port 2 etc.
#define EXTERNAL_RS232_SUPPORT      (BIT0 | BIT1)

#define CHARGE_LED_PWM        GPIO_NUM_40   // turn on leds in charging hole, OUTPUT, PULL DOWN to avoid false on
// when ESP32 goes haywire it could light up LEDs, or when flashing

#define BUTTON_PIN            GPIO_NUM_0    // INPUT, has physical PULLUP
#define IR_RECEIVE_ANALOG     GPIO_NUM_1    // INPUT, has physical PULLUP, IR sensor pulls low, usable for receiving 
//   different frequencies with esp32 RMT peripheral
#define SPEAKER_GPIO          GPIO_NUM_3    // two functions: 1. physically pulled to 1/2 input vcc, measure input voltage when input (attentuation should be set to 10db)
// 2. function: turnoff speaker amplifier when use as output low
#define SCL                   GPIO_NUM_4    // OPEN DRAIN OUTPUT, physically pulled high
#define SDA                   GPIO_NUM_5    // OPEN DRAIN OUTPUT, physically pulled high
#define SPI_CS                GPIO_NUM_6    // chipselect for W5500 ethernet ic, LOW -> enable communication to w5500

#define I2S_BCLK              GPIO_NUM_8    // OUTPUT
#define I2S_SPEAKER_DATA      GPIO_NUM_16   // OUTPUT
#define I2S_WS                GPIO_NUM_17   // OUTPUT
#define I2S_MIC_DATA          GPIO_NUM_18   // INPUT

#define USB_D_MINUS           GPIO_NUM_19   // USB D-
#define USB_D_PLUS            GPIO_NUM_20   // USB D+

#define PERIPHERAL_RESET      GPIO_NUM_21   // OUTPUT, RESET PULLUP, for OLED / Ethernet IC, blocked with 100nf capacitor
#define ETH_LED_PWM           GPIO_NUM_35   // OUTPUT PWM, brightness control for ETH status leds

#define IR_RECEIVE_PIN        GPIO_NUM_36   // INPUT IR filtered 38khz

#define MEASURE_GND_1          GPIO_NUM_9    // INPUT  measures GND level (divide 2) of EXT 1,  always switch high side first and measure, before turning on GND path!
#define MEASURE_GND_1_ADC_UNIT ADC_UNIT_1
#define MEASURE_GND_1_ADC_CH   ADC_CHANNEL_9
#define MEASURE_GND_2          GPIO_NUM_10   // INPUT  measures GND level (divide 2) of EXT 2,  always switch high side first and measure, before turning on GND path!
#define MEASURE_GND_2_ADC_UNIT ADC_UNIT_1
#define MEASURE_GND_2_ADC_CH   ADC_CHANNEL_8

#define SPI_MOSI              GPIO_NUM_11   // OUTPUT, *PULLUP depends on spi mode
#define SPI_MISO              GPIO_NUM_13   // INPUT, *PULLUP  depends on spi mode
#define SPI_CLK               GPIO_NUM_12   // OUTPUT

#define SPI_INT               GPIO_NUM_46   // INPUT,  ETH_INT
#define ROCKCHIP_PIN          GPIO_NUM_47   // to be determined, protected pin with BAT54S and 10k

#define CHARGING_CURRENT      GPIO_NUM_14   // INPUT, measures low side via 0.1 Ohm current to remote
#define CHARGING_CURRENT_ADC_UNIT ADC_UNIT_2
#define CHARGING_CURRENT_ADC_CH   ADC_CHANNEL_3
#define RGB_LED               GPIO_NUM_15   // OUTPUT OPEN DRAIN, pulled physically to 5V 10k

#define CHARGING_ENABLE       GPIO_NUM_45   // OUTPUT, physically pulled low

#define IR_SEND_PIN_INT_SIDE  GPIO_NUM_38   // OUTPUT OPEN DRAIN, physicall pulled up 2.4v
// IR_SEND_PIN_INT_SIDE is an inverted output: required for IRsend GPIO mask
#define IR_SEND_PIN_INT_SIDE_INVERTED 1
#define IR_SEND_PIN_INT_TOP   GPIO_NUM_7    // OUTPUT,OPEN DRAIN. physically pulled up to 2.4V
// IR_SEND_PIN_INT_TOP is an inverted output: required for IRsend GPIO mask
#define IR_SEND_PIN_INT_TOP_INVERTED  1

#define SWITCH_EXT_1          GPIO_NUM_42   // OUTPUT, physically pulled up 5v, switches HIGH side for Extender 1
#define SWITCH_EXT_2          GPIO_NUM_2    // OUTPUT, physically pulled up 5v. switches HIGH side for Extender 2

#define SWITCH_GND_1          GPIO_NUM_48   // OUTPUT, PULLDOWN,  BACK-TO-BACK N-Channel, switch only after high side and measuring
#define SWITCH_GND_2          GPIO_NUM_37   // OUTPUT, PULLDOWN,  BACK-TO-BACK N-Channel, switch only after high side and measuring

#define TX0                   GPIO_NUM_43   // OUTPUT OPEN DRAIN, PULL UP
#define RX0                   GPIO_NUM_44   // INPUT

#define TX1                   GPIO_NUM_41   // OUTPUT OPEN DRAIN, PULL UP
#define RX1                   GPIO_NUM_39   // INPUT

// TX outputs are inverted: required for IRsend GPIO mask
#define TX_INVERTED           1

#define LCD_I2C_BUS_PORT      0
#define LCD_PIXEL_CLOCK_HZ    (100 * 1000)
#define LCD_PIN_NUM_RESET     -1
#define LCD_I2C_HW_ADDR       0x3C

#define LCD_H_RES             128
#define LCD_V_RES             32

// Max supported UART bitrate by the API.
// Note: max converter chip speed is 230400. UART_BITRATE_MAX is the max SOC bitrate.
#define MAX_UART_BITRATE      115200

// include board specific configuration

#if defined(CONFIG_UCD_HW_REVISION_3)
#include "board_r3.h"
#elif defined(CONFIG_UCD_HW_REVISION_4)
#include "board_r4.h"
#else
#error You need to specify a board revision in the SDK configuration: \
CONFIG_UCD_HW_REVISION_3, CONFIG_UCD_HW_REVISION_4
#endif
