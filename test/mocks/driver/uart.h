// extracted definitions from the Espressif SDK for unit tests
// - components/hal/include/hal/uart_types.h

#pragma once

#include "soc/soc_caps.h"

#include "stdlib.h"

#include "uart_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UART_BITRATE_MAX SOC_UART_BITRATE_MAX  ///< Maximum configurable bitrate

/**
 * @brief UART configuration parameters for uart_param_config function
 */
typedef struct {
    int baud_rate;                      /*!< UART baud rate*/
    uart_word_length_t data_bits;       /*!< UART byte size*/
    uart_parity_t parity;               /*!< UART parity mode*/
    uart_stop_bits_t stop_bits;         /*!< UART stop bits*/
    uart_hw_flowcontrol_t flow_ctrl;    /*!< UART HW flow control mode (cts/rts)*/
    uint8_t rx_flow_ctrl_thresh;        /*!< UART HW RTS threshold*/
    union {
        uart_sclk_t source_clk;             /*!< UART source clock selection */
#if (SOC_UART_LP_NUM >= 1)
        lp_uart_sclk_t lp_source_clk;       /*!< LP_UART source clock selection */
#endif
    };
    struct {
        uint32_t allow_pd: 1;               /*!< If set, driver allows the power domain to be powered off when system enters sleep mode.
                                                 This can save power, but at the expense of more RAM being consumed to save register context. */
        uint32_t backup_before_sleep: 1;    /*!< @deprecated, same meaning as allow_pd */
    } flags;                                /*!< Configuration flags */
} uart_config_t;

#ifdef __cplusplus
}
#endif
