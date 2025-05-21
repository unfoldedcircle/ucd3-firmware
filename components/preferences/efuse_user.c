/*
 * SPDX-FileCopyrightText: 2017-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "efuse_user.h"

#include <assert.h>

#include "esp_efuse.h"

#include "sdkconfig.h"

// md5_digest_table 46883448d3e09728b55a29cfde957972
// This file was generated from the file efuse_user.csv. DO NOT CHANGE THIS FILE MANUALLY.
// If you want to change some fields, you need to change efuse_user.csv file
// then run `efuse_common_table` or `efuse_custom_table` command it will generate this file.
// To show efuse_table run the command 'show_efuse_table'.

static const esp_efuse_desc_t USER_DATA_VERSION[] = {
    {EFUSE_BLK3, 0, 8},  // data version field,
};

static const esp_efuse_desc_t USER_DATA_DOCK_SERIAL[] = {
    {EFUSE_BLK3, 8, 64},  // serial number string,
};

static const esp_efuse_desc_t USER_DATA_DOCK_MODEL[] = {
    {EFUSE_BLK3, 72, 56},  // model number string,
};

static const esp_efuse_desc_t USER_DATA_DOCK_HW_REV[] = {
    {EFUSE_BLK3, 128, 24},  // hardware revision string,
};

static const esp_efuse_desc_t USER_DATA_DOCK_HW_FEAT[] = {
    {EFUSE_BLK3, 152, 8},  // hardware features,
};

static const esp_efuse_desc_t USER_DATA_DOCK_HW_FEAT_POE[] = {
    {EFUSE_BLK3, 152, 1},  // PoE feature,
};

static const esp_efuse_desc_t USER_DATA_DOCK_HW_FEAT_CHARGING[] = {
    {EFUSE_BLK3, 153, 1},  // Charging feature,
};

const esp_efuse_desc_t* ESP_EFUSE_USER_DATA_VERSION[] = {&USER_DATA_VERSION[0],  // data version field
                                                         NULL};

const esp_efuse_desc_t* ESP_EFUSE_USER_DATA_DOCK_SERIAL[] = {&USER_DATA_DOCK_SERIAL[0],  // serial number string
                                                             NULL};

const esp_efuse_desc_t* ESP_EFUSE_USER_DATA_DOCK_MODEL[] = {&USER_DATA_DOCK_MODEL[0],  // model number string
                                                            NULL};

const esp_efuse_desc_t* ESP_EFUSE_USER_DATA_DOCK_HW_REV[] = {&USER_DATA_DOCK_HW_REV[0],  // hardware revision string
                                                             NULL};

const esp_efuse_desc_t* ESP_EFUSE_USER_DATA_DOCK_HW_FEAT[] = {&USER_DATA_DOCK_HW_FEAT[0],  // hardware features
                                                              NULL};

const esp_efuse_desc_t* ESP_EFUSE_USER_DATA_DOCK_HW_FEAT_POE[] = {&USER_DATA_DOCK_HW_FEAT_POE[0],  // PoE feature
                                                                  NULL};

const esp_efuse_desc_t* ESP_EFUSE_USER_DATA_DOCK_HW_FEAT_CHARGING[] = {
    &USER_DATA_DOCK_HW_FEAT_CHARGING[0],  // Charging feature
    NULL};
