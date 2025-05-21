// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Based on the Espressif IDF examples (license: Unlicense OR CC0-1.0)

#pragma once

#include "esp_eth.h"
#include "esp_eth_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Ethernet driver based on Espressif IoT Development Framework Configuration
 *
 * @param[out] eth_handle_out initialized Ethernet driver handle
 * @return
 *          - ESP_OK on success
 *          - ESP_ERR_INVALID_ARG when passed invalid pointer
 *          - ESP_ERR_NO_MEM when there is no memory to allocate for Ethernet driver handle
 *          - ESP_FAIL on any other failure
 */
esp_err_t eth_init(esp_eth_handle_t *eth_handle_out);

/**
 * @brief De-initialize of Ethernet driver. WARNING: not yet tested!
 * @note The Ethernet driver must be stopped prior calling this function.
 *
 * @param[in] eth_handles Ethernet driver to be de-initialized
 * @return
 *          - ESP_OK on success
 *          - ESP_ERR_INVALID_ARG when passed invalid pointer
 */
esp_err_t eth_deinit(esp_eth_handle_t *eth_handle);

#ifdef __cplusplus
}
#endif
