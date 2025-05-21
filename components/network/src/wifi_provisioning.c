// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wifi_provisioning.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "ble_log_util.h"
#include "improv.h"
#include "led_pattern.h"
#include "misc.h"
#include "network_priv.h"
#include "sdkconfig.h"
#include "wifi_prov_cfg.h"

uint8_t ble_addr_type;

// The BLE connection handle is different on various ESP32 models!
// ESP32C6 starts at 1, ESP32-S at 0! Let's hope the max uint16 value isn't used...
#define CONN_HANDLE_UNDEF 0xFFFF
uint16_t conn_hdl = CONN_HANDLE_UNDEF;

uint16_t status_char_att_hdl;
uint16_t error_char_att_hdl;
uint16_t rpc_cmd_char_att_hdl;
uint16_t rpc_result_char_att_hdl;
uint16_t capabilities_char_att_hdl;

static improv_state_t status = STATE_STOPPED;  // 0xFF;  // undefined
static uint8_t        capabilities = 0;
static uint8_t        error = ERROR_NONE;
static uint8_t        rpc_result = 0;

static uint8_t service_data[8] = {
    0x77,  // Service Data UUID: 4677
    0x46,  // "
    0x00,  // current state
    0x00,  // capabilities
    0x00,  // reserved
    0x00,  // reserved
    0x00,  // reserved
    0x00   // reserved
};

// https://www.bluetooth.com/specifications/assigned-numbers-html/
#define DEVICE_INFO_SERVICE 0x180A

#define GATT_MANUFACTURER_NAME 0x2A29
#define GATT_MODEL_NUMBER 0x2A24
#define GATT_SERIAL_NUMBER 0x2A25
#define GATT_HARDWARE_REVISION 0x2A27
#define GATT_FIRMWARE_REVISION 0x2A26

#define GATT_CLIENT_CHARACTERISTIC_CONFIGURATION 0x2902

static const char *const TAG = "IMPROV";
static const char *const BT_TAG = "GAP";

void improv_set_state(improv_state_t new_state);
void improv_set_error(improv_error_t new_error);
void improv_send_response(const uint8_t *data, uint16_t length);
/**
 * Start advertising.
 *
 * FIXME Quick and dirty, requires refactoring if other BLE services are introduced.
 */
void ble_app_advertise(void);

void provision_wifi(struct ImprovCommand cmd);

#ifdef CONFIG_IMPROV_WIFI_AUTHENTICATION_BUTTON
void          start_authorized_timer();
void          on_authorized_timeout();
TimerHandle_t authorized_timer = NULL;
#endif

/// Connection timer to verify that connection is active. Workaround for non-reported disconnect
static TimerHandle_t conn_establish_timer = NULL;
static bool          conn_active = false;

static void ble_app_on_reset(int reason);
static void ble_app_on_sync(void);

static const char *create_device_name() {
    uint8_t mac[6];
    char   *device_name;
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    if (-1 == asprintf(&device_name, "%s %02X%02X", CONFIG_BLE_DEVICE_NAME, mac[4], mac[5])) {
        abort();
    }

    return device_name;
}

static int device_info(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    const char *text = NULL;
    uint16_t    uuid = ble_uuid_u16(ctxt->chr->uuid);

    if (uuid == GATT_MANUFACTURER_NAME) {
        text = CONFIG_BLE_DEVICE_INFO_MANUFACTURER_NAME;
    } else if (uuid == GATT_MODEL_NUMBER) {
        text = cfg_get_model();
    } else if (uuid == GATT_SERIAL_NUMBER) {
        text = cfg_get_serial();
    } else if (uuid == GATT_HARDWARE_REVISION) {
        text = cfg_get_revision();
    } else if (uuid == GATT_FIRMWARE_REVISION) {
        text = DOCK_VERSION;
    }

    if (text) {
        int rc = os_mbuf_append(ctxt->om, text, strlen(text));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static int improv_status_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    ESP_LOGI(TAG, "status callback: %d", status);
    ESP_ERROR_CHECK(os_mbuf_append(ctxt->om, &status, sizeof(status)));
    conn_active = true;
    return 0;
}

static int improv_capabilities_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt,
                                  void *arg) {
    ESP_LOGI(TAG, "capabilities callback: %d", capabilities);
    ESP_ERROR_CHECK(os_mbuf_append(ctxt->om, &capabilities, sizeof(capabilities)));
    conn_active = true;
    return 0;
}

static int improv_error_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    ESP_LOGI(TAG, "error callback: %d", error);
    ESP_ERROR_CHECK(os_mbuf_append(ctxt->om, &error, sizeof(error)));
    conn_active = true;
    return 0;
}

static int improv_command(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    improv_error_t new_error = ERROR_NONE;
    improv_state_t new_status = status;

    conn_active = true;
    switch (ctxt->om->om_data[0]) {
        case IDENTIFY: {
            if (ctxt->om->om_len != 3) {
                ESP_LOGE(TAG, "Invalid RPC Command: Identify");
                new_error = ERROR_INVALID_RPC;
            } else {
                ESP_LOGI(TAG, "RPC Command: Identify");
                // This command has no RPC result.
                led_pattern(LED_IMPROV_IDENTIFY);
            }
            break;
        }
        case WIFI_SETTINGS: {
            ESP_LOGI(TAG, "RPC Command: Send Wi-Fi settings");
            if (status != STATE_AUTHORIZED) {
                ESP_LOGE(TAG, "Wi-Fi settings received, but not authorized");
                new_error = ERROR_NOT_AUTHORIZED;
                break;
            }

            // This command will generate an RPC result. The first entry in the list is an URL to redirect the user to.
            // If there is no URL, omit the entry or add an empty string.
            uint16_t om_len;
            int      rc;

            om_len = OS_MBUF_PKTLEN(ctxt->om);
            if (om_len < 15 || om_len > 100) {
                ESP_LOGE(TAG, "RPC Command: Invalid Wi-Fi settings message length %d", om_len);
                new_error = ERROR_INVALID_RPC;
                break;
            }

            uint8_t  buffer[100];
            uint16_t len;
            rc = ble_hs_mbuf_to_flat(ctxt->om, buffer, sizeof(buffer), &len);
            if (rc != 0) {
                return BLE_ATT_ERR_UNLIKELY;
            }

            struct ImprovCommand cmd = parse_improv_data(buffer, len, true);
            // in case of error, the error code is returned in the command field!
            if (cmd.command != WIFI_SETTINGS) {
                ESP_LOGE(TAG, "RPC Command: failed to parse Wi-Fi settings. Error: %d", cmd.command);
                new_error = ERROR_UNKNOWN_RPC;
                break;
            }

            new_status = STATE_PROVISIONING;

            provision_wifi(cmd);
            break;
        }
        case UC_SET_DEVICE_PARAM: {
            ESP_LOGI(TAG, "RPC Command: set device parameter");
            if (status != STATE_AUTHORIZED) {
                ESP_LOGE(TAG, "Wi-Fi settings received, but not authorized");
                new_error = ERROR_NOT_AUTHORIZED;
                break;
            }

            uint16_t om_len;
            int      rc;

            om_len = OS_MBUF_PKTLEN(ctxt->om);
            if (om_len < 3 || om_len > 255) {
                ESP_LOGE(TAG, "RPC Command: Invalid device parameter message length %d", om_len);
                new_error = ERROR_INVALID_RPC;
                break;
            }

            uint8_t  buffer[255];
            uint16_t len;
            rc = ble_hs_mbuf_to_flat(ctxt->om, buffer, sizeof(buffer), &len);
            if (rc != 0) {
                return BLE_ATT_ERR_UNLIKELY;
            }

            struct ImprovCommand cmd = parse_improv_data(buffer, len, true);
            // in case of error, the error code is returned in the command field!
            if (cmd.command != UC_SET_DEVICE_PARAM) {
                ESP_LOGE(TAG, "RPC Command: failed to parse device parameter settings. Error: %d", cmd.command);
                new_error = ERROR_UNKNOWN_RPC;
                break;
            }

            set_device_parameters(cmd);
            break;
        }
        default:
            ESP_LOGE(TAG, "Unknown RPC command: %d (length=%d)", ctxt->om->om_data[0],
                     ctxt->om->om_len > 1 ? ctxt->om->om_data[1] : 0);
            print_mbuf(ctxt->om);
            new_error = ERROR_UNKNOWN_RPC;
    }

    improv_set_error(new_error);
    improv_set_state(new_status);

    return 0;
}

void provision_wifi(struct ImprovCommand cmd) {
    ESP_LOGI(TAG, "Got ssid=%s, password=%s", cmd.ssid, cmd.password[0] ? "****" : "<null>");

    // FIXME wifi command parameters should not be strings!
    trigger_connect_to_ap_event((const char *)cmd.ssid, (const char *)cmd.password);
}

void improv_set_provisioned() {
    uint16_t    length;
    const char *urls[] = {CONFIG_IMPROV_WIFI_PROVISIONED_URL};
    uint8_t    *data = build_rpc_response(WIFI_SETTINGS, urls, sizeof(urls) / sizeof(char *), true, &length);
    if (!data) {
        ESP_LOGE(TAG, "build_rpc_response failed");
        return;
    }
    improv_set_state(STATE_PROVISIONED);

    improv_send_response(data, length);
    free(data);
}

void on_wifi_connect_timeout() {
    if (status != STATE_PROVISIONING) {
        return;
    }
    ESP_LOGW(TAG, "Timed out trying to connect to given WiFi network");

    // TODO is esp_wifi_disconnect() sufficient or do we need something else?
    esp_wifi_disconnect();

    // If the gadget is unable to connect an error is returned.
    improv_set_error(ERROR_UNABLE_TO_CONNECT);
    improv_set_state(STATE_AUTHORIZED);
}

static int improv_rpc_result_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt,
                                void *arg) {
    ESP_LOGI(TAG, "RPC result callback: %d", rpc_result);
    ESP_ERROR_CHECK(os_mbuf_append(ctxt->om, &rpc_result, sizeof(rpc_result)));
    return 0;
}

static const struct ble_gatt_svc_def gat_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = BLE_UUID16_DECLARE(DEVICE_INFO_SERVICE),
     .characteristics =
         (struct ble_gatt_chr_def[]){
             {.uuid = BLE_UUID16_DECLARE(GATT_MANUFACTURER_NAME),
              .flags = BLE_GATT_CHR_F_READ,
              .access_cb = device_info},
             {.uuid = BLE_UUID16_DECLARE(GATT_MODEL_NUMBER), .flags = BLE_GATT_CHR_F_READ, .access_cb = device_info},
             {.uuid = BLE_UUID16_DECLARE(GATT_SERIAL_NUMBER), .flags = BLE_GATT_CHR_F_READ, .access_cb = device_info},
             {.uuid = BLE_UUID16_DECLARE(GATT_HARDWARE_REVISION),
              .flags = BLE_GATT_CHR_F_READ,
              .access_cb = device_info},
             {.uuid = BLE_UUID16_DECLARE(GATT_FIRMWARE_REVISION),
              .flags = BLE_GATT_CHR_F_READ,
              .access_cb = device_info},
             {0}}},
    // improv-wifi
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = BLE_UUID128_DECLARE(0x00, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46, 0x72, 0x22, 0x28, 0x62, 0x68, 0x77,
                                 0x46, 0x00),
     .characteristics = (struct ble_gatt_chr_def[])
         // improv characteristics
         {
            // STATUS_UUID
            {
                .uuid = BLE_UUID128_DECLARE(0x01, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
                                            0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00),
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &status_char_att_hdl,
                .access_cb = improv_status_cb,
            },
            // ERROR_UUID
            {
                .uuid = BLE_UUID128_DECLARE(0x02, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
                                            0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00),
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &error_char_att_hdl,
                .access_cb = improv_error_cb,
            },
            // RPC_COMMAND_UUID
            {
                .uuid = BLE_UUID128_DECLARE(0x03, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
                                            0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00),
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &rpc_cmd_char_att_hdl,
                .access_cb = improv_command,
            },
            // RPC_RESULT_UUID
            {
                .uuid = BLE_UUID128_DECLARE(0x04, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
                                            0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00),
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &rpc_result_char_att_hdl,
                .access_cb = improv_rpc_result_cb,
            },
            // CAPABILITIES_UUID
            {
                .uuid = BLE_UUID128_DECLARE(0x05, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
                                            0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00),
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &capabilities_char_att_hdl,  // not required because of no NOTIFY
                .access_cb = improv_capabilities_cb,
            },
            {0}}},
    {0}};

/// @brief Workaround to handle client disconnects with error code 0x3E, which are NOT reported in the GAP callback by
/// NimBLE :-(
static void verify_connection_cb(TimerHandle_t timer_id) {
    // we might also use ble_gap_host_check_status and see if it returns an active connection: BIT(BLE_GAP_STATUS_CONN)
    ESP_LOGI(BT_TAG, "verify connection: %x, active=%d", conn_hdl, conn_active);

    if (conn_active || conn_hdl == CONN_HANDLE_UNDEF) {
        return;
    }

    // Only certain reason codes are allowed:
    // https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host-controller-interface/host-controller-interface-functional-specification.html#UUID-6bb8119e-aa67-d517-db2a-7470c35fbf4a
    ble_gap_terminate(conn_hdl, BLE_ERR_REM_USER_CONN_TERM);

    conn_hdl = CONN_HANDLE_UNDEF;
    trigger_improv_ble_disconnect_event();
    improv_set_state(STATE_STOPPED);
}

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            // A new connection was established or a connection attempt failed.
            ESP_LOGI(BT_TAG, "BLE_GAP_EVENT_CONNECT %s", event->connect.status == 0 ? "OK" : "Failed");
            if (event->connect.status != 0) {
                ESP_LOGI(BT_TAG, "Connection attempt failed, starting advertisement.");
                conn_hdl = CONN_HANDLE_UNDEF;
                ble_app_advertise();
            } else {
                conn_hdl = event->connect.conn_handle;

                if (conn_establish_timer != NULL) {
                    xTimerDelete(conn_establish_timer, portMAX_DELAY);
                }
                conn_establish_timer =
                    xTimerCreate("ble-conn-timeout", pdMS_TO_TICKS(5 * 1000), pdFALSE, NULL, verify_connection_cb);
                if (conn_establish_timer == NULL) {
                    ESP_LOGE(BT_TAG, "Could not create connection timer");
                } else if (xTimerStart(conn_establish_timer, 0) != pdPASS) {
                    ESP_LOGE(TAG, "Could not start connection timer");
                }
                conn_active = false;

                trigger_improv_ble_connect_event();
                init_improv();
                ESP_LOGI(BT_TAG,
                         "CONNECT: conn_handle=%x, status handle=%d, error handle=%d, rpc cmd handle=%d, rpc result "
                         "handle=%d, capabilities handle=%d",
                         event->connect.conn_handle, status_char_att_hdl, error_char_att_hdl, rpc_cmd_char_att_hdl,
                         rpc_result_char_att_hdl, capabilities_char_att_hdl);
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            // Connection terminated; resume advertising.
            ESP_LOGI(BT_TAG, "BLE_GAP_EVENT_DISCONNECT");

            if (conn_establish_timer != NULL) {
                xTimerDelete(conn_establish_timer, portMAX_DELAY);
                conn_establish_timer = NULL;
                conn_active = false;
            }

            conn_hdl = CONN_HANDLE_UNDEF;
            trigger_improv_ble_disconnect_event();
            improv_set_state(STATE_STOPPED);
            ble_app_advertise();
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            // Advertising terminated; resume advertising.
            ESP_LOGI(BT_TAG, "BLE_GAP_EVENT_ADV_COMPLETE");
            ble_app_advertise();
            break;
        default:
            log_ble_gap_event(event, arg);
    }

    return 0;
}

// set state and notify if not in state STOPPED
void improv_set_state(improv_state_t new_state) {
    if (new_state != status) {
        ESP_LOGI(TAG, "Setting state: %s -> %s", get_state_str(status), get_state_str(new_state));
        status = new_state;

        if (conn_hdl != CONN_HANDLE_UNDEF && status != STATE_STOPPED) {
            struct os_mbuf *om = ble_hs_mbuf_from_flat(&status, sizeof(status));
            ble_gatts_notify_custom(conn_hdl, status_char_att_hdl, om);
        }

        // TODO put in network state-machine
        switch (new_state) {
            case STATE_STOPPED:
                led_pattern(LED_IMPROV_STOPPED);
                break;
            case STATE_AWAITING_AUTHORIZATION:
                led_pattern(LED_IMPROV_WAIT_AUTHORIZATION);
                break;
            case STATE_AUTHORIZED:
                led_pattern(LED_IMPROV_WAIT_CREDENTIALS);
                // Specification: if the gadget required authorization, the authorization reset timeout should start
                // over.
#ifdef CONFIG_IMPROV_WIFI_AUTHENTICATION_BUTTON
                start_authorized_timer();
#endif
                break;
            case STATE_PROVISIONING:
                led_pattern(LED_IMPROV_PROVISIONING);
                break;
            case STATE_PROVISIONED:
                led_pattern(LED_IMPROV_PROVISIONED);
                break;
        }
    }
}

// set error and notify if not in state STOPPED
void improv_set_error(improv_error_t new_error) {
    if (new_error != error) {
        ESP_LOGI(TAG, "Setting error: %s", get_error_str(new_error));
        error = new_error;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(&error, sizeof(error));

        if (conn_hdl != CONN_HANDLE_UNDEF && status != STATE_STOPPED) {
            ble_gattc_notify_custom(conn_hdl, error_char_att_hdl, om);
        }

        if (new_error == ERROR_UNABLE_TO_CONNECT) {
            led_pattern(LED_IMPROV_FAILED);
        }
    }
}

void improv_send_response(const uint8_t *data, uint16_t length) {
    if (conn_hdl != CONN_HANDLE_UNDEF && status != STATE_STOPPED) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(data, length);
        ble_gattc_notify_custom(conn_hdl, rpc_result_char_att_hdl, om);
    }
}

void init_service_data() {
    service_data[2] = status;
    service_data[3] = capabilities;
}

#ifdef CONFIG_IMPROV_WIFI_AUTHENTICATION_BUTTON
void start_authorized_timer() {
    ESP_LOGI(TAG, "Starting authorization timeout: %ds", CONFIG_IMPROV_WIFI_AUTHENTICATION_TIMEOUT);

    if (!authorized_timer) {
        authorized_timer =
            xTimerCreate("authorized-timeout", pdMS_TO_TICKS(CONFIG_IMPROV_WIFI_AUTHENTICATION_TIMEOUT * 1000), pdFALSE,
                         NULL, on_authorized_timeout);
        if (authorized_timer == NULL) {
            ESP_LOGE(TAG, "Could not create authorized timer");
            return;
        }
    }
    if (xTimerStart(authorized_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Could not start authorized timer");
    }
}

void improv_set_authorized() {
    if (status != STATE_AWAITING_AUTHORIZATION) {
        ESP_LOGI(TAG, "Ignoring authorization callback: not waiting for authorization");
        return;
    }

    // switch to authorized state and start authorization timeout timer
    ESP_LOGI(TAG, "Authorization through button press");

    improv_set_state(STATE_AUTHORIZED);
}

void on_authorized_timeout() {
    if (status != STATE_AUTHORIZED) {
        ESP_LOGI(TAG, "Ignoring authorization timeout in state %s", get_state_str(status));
        return;
    }

    ESP_LOGI(TAG, "Authorization timeout: require new authorization");
    improv_set_state(STATE_AWAITING_AUTHORIZATION);
    trigger_improv_authorized_timeout_event();
}
#endif

void init_improv(void) {
#ifdef CONFIG_IMPROV_WIFI_CAPABILITY_IDENTIFY
    capabilities = CAPABILITY_IDENTIFY;
#endif
    error = ERROR_NONE;

    // Set initial state
    if (conn_hdl == CONN_HANDLE_UNDEF) {
        improv_set_state(STATE_STOPPED);
        return;
    }
    // Specification: the Improv service can optionally require physical authorization to allow pairing, like pressing a
    // button.
#ifdef CONFIG_IMPROV_WIFI_AUTHENTICATION_BUTTON
    improv_set_state(STATE_AWAITING_AUTHORIZATION);
#else
    // Specification: a gadget that does not require authorization should start in the "authorized" state.
    improv_set_state(STATE_AUTHORIZED);
#endif
}

esp_err_t start_improv(void) {
    // TODO does this affect advertisement fields?
    // Default name is "nimble", which suddenly appeared in macOS LightBlue scan results if device_name was not set!
    ESP_RETURN_ON_ERROR(ble_svc_gap_device_name_set(create_device_name()), TAG, "Failed to set GAP device name");

    ble_svc_gap_init();
    ble_svc_gatt_init();

    ESP_RETURN_ON_ERROR(ble_gatts_count_cfg(gat_svcs), TAG, "Failed to count GATT services");
    ESP_RETURN_ON_ERROR(ble_gatts_add_svcs(gat_svcs), TAG, "Failed to register GATT services");

    ble_hs_cfg.reset_cb = ble_app_on_reset;
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    ble_hs_cfg.gatts_register_cb = log_gatt_svr_register_cb;  // just for logging purposes

    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;

    return ESP_OK;
}

void ble_app_advertise(void) {
    ESP_LOGI(TAG, "Setting up advertisement data");
    // Setting up minimal information in the advertisement data because we have
    // to announce a 128 bit UUID, plus a 16 bit service data with 6 byte payload.
    // Local name, tx power level are set in the advertisement response data. See below.
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    fields.flags =
        BLE_HS_ADV_F_DISC_GEN |    // Discoverability in forthcoming advertisement (general)
                                   // BLE_HS_ADV_F_DISC_LTD |  // ? -> not set in esphome-web reference implementation
        BLE_HS_ADV_F_BREDR_UNSUP;  // classic BT not available

    // advertise improv-wifi service UUID
    fields.uuids128 = (ble_uuid128_t[]){BLE_UUID128_INIT(0x00, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46, 0x72, 0x22,
                                                         0x28, 0x62, 0x68, 0x77, 0x46, 0x00)};
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    // advertise improv-wifi service data. Per spec this may not be in the scan response data
    init_service_data();
    fields.svc_data_uuid16 = service_data;
    fields.svc_data_uuid16_len = sizeof(service_data);

    ESP_ERROR_CHECK(ble_gap_adv_set_fields(&fields));

    ESP_LOGI(TAG, "Setting up advertisement response data");
    struct ble_hs_adv_fields rsp_fields;
    memset(&rsp_fields, 0, sizeof(rsp_fields));

    /* Indicate that the TX power level field should be included; have the
     * stack fill this value automatically.  This is done by assigning the
     * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
     */
    rsp_fields.tx_pwr_lvl_is_present = 1;
    rsp_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    // advertise "Local Name"
    const char *device_name = ble_svc_gap_device_name();
    rsp_fields.name = (uint8_t *)device_name;
    rsp_fields.name_len = strlen(device_name);
    rsp_fields.name_is_complete = 1;

    ESP_ERROR_CHECK(ble_gap_adv_rsp_set_fields(&rsp_fields));

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;  // connectable or non-connectable
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;  // discoverable or non-discoverable

    ESP_LOGI(TAG, "Starting GAP advertisement");
    ESP_ERROR_CHECK(ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL));
}

static void ble_app_on_reset(int reason) {
    ESP_LOGI(BT_TAG, "Resetting state; reason=%d\n", reason);
}

static void ble_app_on_sync(void) {
    ESP_ERROR_CHECK(ble_hs_id_infer_auto(0, &ble_addr_type));
    ble_app_advertise();
}
