/*
 * SPDX-FileCopyrightText: 2017-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_efuse.h"

// md5_digest_table 46883448d3e09728b55a29cfde957972
// This file was generated from the file efuse_user.csv. DO NOT CHANGE THIS FILE MANUALLY.
// If you want to change some fields, you need to change efuse_user.csv file
// then run `efuse_common_table` or `efuse_custom_table` command it will generate this file.
// To show efuse_table run the command 'show_efuse_table'.

extern const esp_efuse_desc_t* ESP_EFUSE_USER_DATA_VERSION[];
extern const esp_efuse_desc_t* ESP_EFUSE_USER_DATA_DOCK_SERIAL[];
extern const esp_efuse_desc_t* ESP_EFUSE_USER_DATA_DOCK_MODEL[];
extern const esp_efuse_desc_t* ESP_EFUSE_USER_DATA_DOCK_HW_REV[];
extern const esp_efuse_desc_t* ESP_EFUSE_USER_DATA_DOCK_HW_FEAT[];
extern const esp_efuse_desc_t* ESP_EFUSE_USER_DATA_DOCK_HW_FEAT_POE[];
extern const esp_efuse_desc_t* ESP_EFUSE_USER_DATA_DOCK_HW_FEAT_CHARGING[];

#ifdef __cplusplus
}
#endif
