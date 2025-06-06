/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

// Original source: https://github.com/apache/mynewt-nimble

#include "misc.h"

void rand_bytes(uint8_t *data, int len) {
    int i;

    for (i = 0; i < len; ++i) {
        data[i] = (uint8_t)rand() % UINT8_MAX;
    }
}

/**
 * Utility function to log an array of bytes.
 */
void print_bytes(const uint8_t *bytes, int len) {
    int i;

    for (i = 0; i < len; i++) {
        MODLOG_DFLT(INFO, "%s0x%02x", i != 0 ? ":" : "", bytes[i]);
    }
}

void print_addr(const void *addr) {
    const uint8_t *u8p;

    u8p = addr;
    MODLOG_DFLT(INFO, "%02x:%02x:%02x:%02x:%02x:%02x", u8p[5], u8p[4], u8p[3], u8p[2], u8p[1], u8p[0]);
}

void print_mbuf(const struct os_mbuf *om) {
    while (om != NULL) {
        print_bytes(om->om_data, om->om_len);
        om = SLIST_NEXT(om, om_next);

        if (om == NULL) {
            return;
        }

        /* Separate buf fields with colon to maintain continuity */
        MODLOG_DFLT(INFO, ":");
    }
}

char *addr_str(const void *addr) {
    /* 6 bytes of MAC address * 2 signs for each byte in string + 5 colons to
     * separate bytes + 1 byte for null-character appended by sprintf
     */
    static char    buf[6 * 2 + 5 + 1];
    const uint8_t *u8p;

    u8p = addr;
    sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x", u8p[5], u8p[4], u8p[3], u8p[2], u8p[1], u8p[0]);

    return buf;
}

void print_uuid(const ble_uuid_t *uuid) {
    char buf[BLE_UUID_STR_LEN];

    MODLOG_DFLT(INFO, "%s", ble_uuid_to_str(uuid, buf));
}

/**
 * Logs information about a connection to the console.
 */
void print_conn_desc(const struct ble_gap_conn_desc *desc) {
    MODLOG_DFLT(INFO, "handle=%d our_ota_addr_type=%d our_ota_addr=%s ", desc->conn_handle, desc->our_ota_addr.type,
                addr_str(desc->our_ota_addr.val));
    MODLOG_DFLT(INFO, "our_id_addr_type=%d our_id_addr=%s ", desc->our_id_addr.type, addr_str(desc->our_id_addr.val));
    MODLOG_DFLT(INFO, "peer_ota_addr_type=%d peer_ota_addr=%s ", desc->peer_ota_addr.type,
                addr_str(desc->peer_ota_addr.val));
    MODLOG_DFLT(INFO, "peer_id_addr_type=%d peer_id_addr=%s ", desc->peer_id_addr.type,
                addr_str(desc->peer_id_addr.val));
    MODLOG_DFLT(INFO,
                "conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                "encrypted=%d authenticated=%d bonded=%d",
                desc->conn_itvl, desc->conn_latency, desc->supervision_timeout, desc->sec_state.encrypted,
                desc->sec_state.authenticated, desc->sec_state.bonded);
}

void print_adv_fields(const struct ble_hs_adv_fields *fields) {
    char           s[BLE_HS_ADV_MAX_SZ];
    const uint8_t *u8p;
    int            i;

    if (fields->flags != 0) {
        MODLOG_DFLT(INFO, "    flags=0x%02x\n", fields->flags);
    }

    if (fields->uuids16 != NULL) {
        MODLOG_DFLT(INFO, "    uuids16(%scomplete)=", fields->uuids16_is_complete ? "" : "in");
        for (i = 0; i < fields->num_uuids16; i++) {
            print_uuid(&fields->uuids16[i].u);
            MODLOG_DFLT(INFO, " ");
        }
        MODLOG_DFLT(INFO, "\n");
    }

    if (fields->uuids32 != NULL) {
        MODLOG_DFLT(INFO, "    uuids32(%scomplete)=", fields->uuids32_is_complete ? "" : "in");
        for (i = 0; i < fields->num_uuids32; i++) {
            print_uuid(&fields->uuids32[i].u);
            MODLOG_DFLT(INFO, " ");
        }
        MODLOG_DFLT(INFO, "\n");
    }

    if (fields->uuids128 != NULL) {
        MODLOG_DFLT(INFO, "    uuids128(%scomplete)=", fields->uuids128_is_complete ? "" : "in");
        for (i = 0; i < fields->num_uuids128; i++) {
            print_uuid(&fields->uuids128[i].u);
            MODLOG_DFLT(INFO, " ");
        }
        MODLOG_DFLT(INFO, "\n");
    }

    if (fields->name != NULL) {
        assert(fields->name_len < sizeof s - 1);
        memcpy(s, fields->name, fields->name_len);
        s[fields->name_len] = '\0';
        MODLOG_DFLT(INFO, "    name(%scomplete)=%s\n", fields->name_is_complete ? "" : "in", s);
    }

    if (fields->tx_pwr_lvl_is_present) {
        MODLOG_DFLT(INFO, "    tx_pwr_lvl=%d\n", fields->tx_pwr_lvl);
    }

    if (fields->slave_itvl_range != NULL) {
        MODLOG_DFLT(INFO, "    slave_itvl_range=");
        print_bytes(fields->slave_itvl_range, BLE_HS_ADV_SLAVE_ITVL_RANGE_LEN);
        MODLOG_DFLT(INFO, "\n");
    }

    if (fields->svc_data_uuid16 != NULL) {
        MODLOG_DFLT(INFO, "    svc_data_uuid16=");
        print_bytes(fields->svc_data_uuid16, fields->svc_data_uuid16_len);
        MODLOG_DFLT(INFO, "\n");
    }

    if (fields->public_tgt_addr != NULL) {
        MODLOG_DFLT(INFO, "    public_tgt_addr=");
        u8p = fields->public_tgt_addr;
        for (i = 0; i < fields->num_public_tgt_addrs; i++) {
            MODLOG_DFLT(INFO, "public_tgt_addr=%s ", addr_str(u8p));
            u8p += BLE_HS_ADV_PUBLIC_TGT_ADDR_ENTRY_LEN;
        }
        MODLOG_DFLT(INFO, "\n");
    }

    if (fields->appearance_is_present) {
        MODLOG_DFLT(INFO, "    appearance=0x%04x\n", fields->appearance);
    }

    if (fields->adv_itvl_is_present) {
        MODLOG_DFLT(INFO, "    adv_itvl=0x%04x\n", fields->adv_itvl);
    }

    if (fields->svc_data_uuid32 != NULL) {
        MODLOG_DFLT(INFO, "    svc_data_uuid32=");
        print_bytes(fields->svc_data_uuid32, fields->svc_data_uuid32_len);
        MODLOG_DFLT(INFO, "\n");
    }

    if (fields->svc_data_uuid128 != NULL) {
        MODLOG_DFLT(INFO, "    svc_data_uuid128=");
        print_bytes(fields->svc_data_uuid128, fields->svc_data_uuid128_len);
        MODLOG_DFLT(INFO, "\n");
    }

    if (fields->uri != NULL) {
        MODLOG_DFLT(INFO, "    uri=");
        print_bytes(fields->uri, fields->uri_len);
        MODLOG_DFLT(INFO, "\n");
    }

    if (fields->mfg_data != NULL) {
        MODLOG_DFLT(INFO, "    mfg_data=");
        print_bytes(fields->mfg_data, fields->mfg_data_len);
        MODLOG_DFLT(INFO, "\n");
    }
}
