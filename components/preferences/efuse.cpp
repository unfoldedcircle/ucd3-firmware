// SPDX-FileCopyrightText: Copyright (c) 2022 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "efuse.h"

#include <ctype.h>
#include <soc/efuse_reg.h>
#include <string.h>

#include "esp_efuse.h"
#include "esp_log.h"

#include "efuse_user.h"

/*
Generate efuse_user.c & .h:
cd $IDF_PATH/components/efuse/
./efuse_table_gen.py --idf_target esp32s3 esp32s3/esp_efuse_table.csv $OLDPWD/efuse_user.csv
*/

typedef struct {
    uint8_t poe : 1;
    uint8_t charging : 1;
    uint8_t not_defined : 6;
} dock_features_t;

typedef struct {
    uint8_t         version;
    char            serial[9];  // always 1 byte more for zero terminator
    char            model[8];
    char            revision[4];
    dock_features_t features;
} device_desc_t;

static device_desc_t device_desc;

Efuse::Efuse() {
    esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA_VERSION, &device_desc.version,
                              ESP_EFUSE_USER_DATA_VERSION[0]->bit_count);
    esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA_DOCK_SERIAL, &device_desc.serial,
                              ESP_EFUSE_USER_DATA_DOCK_SERIAL[0]->bit_count);
    esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA_DOCK_MODEL, &device_desc.model,
                              ESP_EFUSE_USER_DATA_DOCK_MODEL[0]->bit_count);
    esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA_DOCK_HW_REV, &device_desc.revision,
                              ESP_EFUSE_USER_DATA_DOCK_HW_REV[0]->bit_count);
    esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA_DOCK_HW_FEAT, &device_desc.features,
                              ESP_EFUSE_USER_DATA_DOCK_HW_FEAT[0]->bit_count);
    if (device_desc.serial[0] == 0) {
        strncpy(device_desc.serial, "00000000", sizeof(device_desc.serial));
    }
    // workaround for the first docks
    char* model = device_desc.model;
    while ((*model = toupper(*model))) {
        ++model;
    }
    if (device_desc.version == 0) {
        device_desc.features.charging = 1;
    }
    ESP_LOGI(m_ctx, "v=%d, serial: %s, model: %s, revision: %s, charging: %d", device_desc.version, device_desc.serial,
             device_desc.model, device_desc.revision, device_desc.features.charging);
}

const char* Efuse::getSerial() const {
    return device_desc.serial;
}

const char* Efuse::getModel() const {
    return device_desc.model;
}

const char* Efuse::getHwRevision() const {
    return device_desc.revision;
}

bool Efuse::hasChargingFeature() const {
    return device_desc.features.charging;
}

Efuse& Efuse::instance() {
    // https://laristra.github.io/flecsi/src/developer-guide/patterns/meyers_singleton.html
    static Efuse instance;
    return instance;
}
