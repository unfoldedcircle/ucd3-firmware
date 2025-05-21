// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"

void log_gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
        case BLE_GATT_REGISTER_OP_SVC:
            MODLOG_DFLT(DEBUG, "registered service %s with handle=%d\n", ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                        ctxt->svc.handle);
            break;

        case BLE_GATT_REGISTER_OP_CHR:
            MODLOG_DFLT(DEBUG,
                        "registering characteristic %s with "
                        "def_handle=%d val_handle=%d\n",
                        ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf), ctxt->chr.def_handle, ctxt->chr.val_handle);
            break;

        case BLE_GATT_REGISTER_OP_DSC:
            MODLOG_DFLT(DEBUG, "registering descriptor %s with handle=%d\n",
                        ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf), ctxt->dsc.handle);
            break;

        default:
            break;
    }
}

void log_ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            // A new connection was established or a connection attempt failed.
            ESP_LOGI("GAP", "BLE_GAP_EVENT_CONNECT %s", event->connect.status == 0 ? "OK" : "Failed");
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            // Connection terminated; resume advertising.
            ESP_LOGI("GAP", "BLE_GAP_EVENT_DISCONNECT");
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            // Advertising terminated; resume advertising.
            ESP_LOGI("GAP", "BLE_GAP_EVENT_ADV_COMPLETE");
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_SUBSCRIBE: handle=%d", event->subscribe.attr_handle);
            break;
        case BLE_GAP_EVENT_CONN_UPDATE:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_CONN_UPDATE");
            break;
        case BLE_GAP_EVENT_CONN_UPDATE_REQ:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_CONN_UPDATE_REQ");
            break;
        case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_L2CAP_UPDATE_REQ");
            break;
        case BLE_GAP_EVENT_TERM_FAILURE:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_TERM_FAILURE");
            break;
        case BLE_GAP_EVENT_DISC:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_DISC");
            break;
        case BLE_GAP_EVENT_DISC_COMPLETE:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_DISC_COMPLETE");
            break;
        case BLE_GAP_EVENT_ENC_CHANGE:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_ENC_CHANGE");
            break;
        case BLE_GAP_EVENT_PASSKEY_ACTION:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_PASSKEY_ACTION");
            break;
        case BLE_GAP_EVENT_NOTIFY_RX:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_NOTIFY_RX");
            break;
        case BLE_GAP_EVENT_NOTIFY_TX:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_NOTIFY_TX");
            break;
        case BLE_GAP_EVENT_MTU:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_MTU");
            break;
        case BLE_GAP_EVENT_IDENTITY_RESOLVED:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_IDENTITY_RESOLVED");
            break;
        case BLE_GAP_EVENT_REPEAT_PAIRING:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_REPEAT_PAIRING");
            break;
        case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_PHY_UPDATE_COMPLETE");
            break;
        case BLE_GAP_EVENT_EXT_DISC:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_EXT_DISC");
            break;
        case BLE_GAP_EVENT_PERIODIC_SYNC:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_PERIODIC_SYNC");
            break;
        case BLE_GAP_EVENT_PERIODIC_REPORT:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_PERIODIC_REPORT");
            break;
        case BLE_GAP_EVENT_PERIODIC_SYNC_LOST:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_PERIODIC_SYNC_LOST");
            break;
        case BLE_GAP_EVENT_SCAN_REQ_RCVD:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_SCAN_REQ_RCVD");
            break;
        case BLE_GAP_EVENT_PERIODIC_TRANSFER:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_PERIODIC_TRANSFER");
            break;
        case BLE_GAP_EVENT_PATHLOSS_THRESHOLD:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_PATHLOSS_THRESHOLD");
            break;
        case BLE_GAP_EVENT_TRANSMIT_POWER:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_TRANSMIT_POWER");
            break;
        case BLE_GAP_EVENT_SUBRATE_CHANGE:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_SUBRATE_CHANGE");
            break;
        case BLE_GAP_EVENT_VS_HCI:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_VS_HCI");
            break;
        case BLE_GAP_EVENT_REATTEMPT_COUNT:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_REATTEMPT_COUNT");
            break;
        case BLE_GAP_EVENT_AUTHORIZE:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_AUTHORIZE");
            break;
        case BLE_GAP_EVENT_TEST_UPDATE:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_TEST_UPDATE");
            break;
        case BLE_GAP_EVENT_DATA_LEN_CHG:
            ESP_LOGI("GAP", "BLE_GAP_EVENT_DATA_LEN_CHG");
            break;
        default:
            ESP_LOGI("GAP", "event: %d", event->type);
            break;
    }
}
