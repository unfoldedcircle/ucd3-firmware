// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "soc/gpio_struct.h"  // required for GPIO

#include "IRremoteESP8266.h"
#include "IRsend.h"
#include "WebServer.h"
#include "adc_unit.h"
#include "board.h"
#include "button.h"
#include "cJSON.h"
#include "charger.h"
#include "config.h"
#include "display.h"
#include "efuse.h"
#include "external_port.h"
#include "frogfs/frogfs.h"
#include "frogfs/vfs.h"
#include "globalcache_server.h"
#include "gpio_util.h"
#include "ir_codes.h"
#include "led_pattern.h"
#include "mdns.h"
#include "network.h"
#include "nvs_flash.h"
#include "ota.h"
#include "service_ir.h"
#include "system_stats.h"
#include "uc_events.h"
#include "ucd_api.h"

static const char *const TAG = "MAIN";

extern const uint8_t frogfs_bin[];
extern const size_t  frogfs_bin_len;

void init_nvs(void) {
    // TODO For better error checks:
    // https://github.com/robdobsn/RaftCore/blob/main/components/core/RaftJson/RaftJsonNVS.cpp#L358
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

/// @brief Initialize mDNS advertisement
/// @param cfg
/// @return - ESP_OK on success
///  - ESP_ERR_INVALID_STATE when failed to register event handler
///  - ESP_ERR_NO_MEM on memory error
///  - ESP_FAIL when failed to start mdns task or add a service
static esp_err_t init_mdns(Config *cfg) {
    assert(cfg);

    ESP_RETURN_ON_ERROR(mdns_init(), TAG, "Failed to initialize mDNS");
    auto hostname = cfg->getHostName();
    auto friendly_name = cfg->getFriendlyName();
    auto version = cfg->getSoftwareVersion();

    ESP_ERROR_CHECK(mdns_hostname_set(hostname));
    ESP_ERROR_CHECK(mdns_instance_name_set(hostname));

    mdns_txt_item_t serviceTxtData[] = {{"ver", version.c_str()},
                                        {"model", cfg->getModel()},
                                        {"rev", cfg->getRevision()},
                                        {"name", friendly_name.c_str()},
                                        {"ws_path", "/ws"}};

    return mdns_service_add(NULL, "_uc-dock", "_tcp", CONFIG_UCD_WEB_SERVER_PORT, serviceTxtData,
                            sizeof(serviceTxtData) / sizeof(serviceTxtData[0]));
}

/// @brief Initialize FrogFS (embedded) and SPIFFS (partition) filesystems
/// @return ESP_OK or ESP_FAIL
esp_err_t init_fs(void) {
    frogfs_config_t frogfs_config = {
        .addr = frogfs_bin,
        .part_label = nullptr,
    };

    frogfs_fs_t *fs = frogfs_init(&frogfs_config);
    if (fs == NULL) {
        ESP_LOGE(TAG, "Failed to initialize frogfs");
        return ESP_FAIL;
    }

    frogfs_vfs_conf_t frogfs_vfs_conf = {
        .base_path = CONFIG_UCD_EMBEDDED_MOUNT_POINT,
        .fs = fs,
        .max_files = 5,
    };
    frogfs_vfs_register(&frogfs_vfs_conf);

    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/data",
        .partition_label = "data",
        .partition = nullptr,
        .format_if_mount_failed = true,
        .read_only = false,
        .dont_mount = false,
        .grow_on_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    return ESP_OK;
}

/// @brief Plain and simple REST callback for the single /api/pub/info endpoint.
/// @param req HTTP Request Data Structure
/// @return ESP_OK if response was successfully sent, otherwise ESP_ERR_## in case of an error. An error condition will
/// close the server socket.
esp_err_t on_rest_sysinfo(httpd_req_t *req) {
    const char *sys_info = get_sysinfo_json();
    auto        ret = httpd_resp_sendstr(req, sys_info);
    free((void *)sys_info);
    return ret;
}

/// @brief Manually configure all GPIOs which are not initialized in a dedicated component (e.g. button) or driver (e.g.
/// ethernet).
void init_gpios(void) {
    // turn off PWM LED
    gpio_init(CHARGE_LED_PWM, GPIO_MODE_OUTPUT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_ENABLE);
    gpio_set_level(CHARGE_LED_PWM, 0);

    gpio_init(ETH_LED_PWM, GPIO_MODE_OUTPUT);
    esp_err_t ret = eth_pwm_led_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ETH PWM LED: %d", ret);
    }

    gpio_init(board_get_charging_current_pin(), GPIO_MODE_INPUT);
    // also input to be able to read current state in charger monitor
    gpio_init(CHARGING_ENABLE, GPIO_MODE_INPUT_OUTPUT, GPIO_PULLUP_DISABLE);
    gpio_set_level(CHARGING_ENABLE, 0);

    // Reset Ethernet & LCD (shared reset line). Enable is at end of this function.
    gpio_init(PERIPHERAL_RESET, GPIO_MODE_OUTPUT);
    gpio_set_level(PERIPHERAL_RESET, 0);

    // internal IR
    gpio_init(IR_RECEIVE_PIN, GPIO_MODE_INPUT);

    gpio_init(IR_SEND_PIN_INT_SIDE, GPIO_MODE_OUTPUT_OD, GPIO_PULLUP_DISABLE);
    gpio_set_level(IR_SEND_PIN_INT_SIDE, IR_SEND_PIN_INT_SIDE_INVERTED);  // output low (inverted logic)
    gpio_num_t ir_send_pin_int_top = board_get_ir_send_pin_int_top();
    if (ir_send_pin_int_top != GPIO_NUM_NC) {
        gpio_init(ir_send_pin_int_top, GPIO_MODE_OUTPUT_OD, GPIO_PULLUP_DISABLE);
        gpio_set_level(ir_send_pin_int_top, IR_SEND_PIN_INT_TOP_INVERTED);  // output low (inverted logic)
    }

    // configure UART out for IR blaster
    gpio_init(TX0, GPIO_MODE_OUTPUT_OD);
    gpio_init(TX1, GPIO_MODE_OUTPUT_OD);
    gpio_init(RX0, GPIO_MODE_INPUT);
    gpio_init(RX1, GPIO_MODE_INPUT);
    gpio_set_level(TX0, TX_INVERTED);  // output low
    gpio_set_level(TX1, TX_INVERTED);  // output low

    // RS232 / IR out 1
    gpio_init(SWITCH_EXT_1, board_switch_ext_gpio_mode(), GPIO_PULLUP_DISABLE);
    gpio_init(MEASURE_GND_1, GPIO_MODE_INPUT, GPIO_PULLUP_DISABLE);
    // also use input to retrieve current state in trigger output mode
    gpio_init(SWITCH_GND_1, GPIO_MODE_INPUT_OUTPUT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_ENABLE);
    // disable 5V & GND
    gpio_set_level(SWITCH_EXT_1, board_is_switch_ext_inverted() ? 1 : 0);  // output low
    gpio_set_level(SWITCH_GND_1, 0);                                       // output low

    // RS232 / IR out 2
    gpio_init(SWITCH_EXT_2, board_switch_ext_gpio_mode(), GPIO_PULLUP_DISABLE);
    gpio_init(MEASURE_GND_2, GPIO_MODE_INPUT, GPIO_PULLUP_DISABLE);
    // also use input to retrieve current state in trigger output mode
    gpio_init(SWITCH_GND_2, GPIO_MODE_INPUT_OUTPUT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_ENABLE);
    // disable 5V & GND
    gpio_set_level(SWITCH_EXT_2, board_is_switch_ext_inverted() ? 1 : 0);
    gpio_set_level(SWITCH_GND_2, 0);

    // Rev 6 PoE voltage
    gpio_num_t poe_switch_pin = board_get_poe_switch_pin();
    if (poe_switch_pin != GPIO_NUM_NC) {
        gpio_init(poe_switch_pin, GPIO_MODE_OUTPUT);
        gpio_set_level(poe_switch_pin, 0);
    }

    // Set ethernet / lcd reset to defined state after reset hold, otherwise KSZ8851 ain't happy (mismatched chip ID)
    // Use minimal delay, another ~ 100ms is added with all preceding gpio initializations since PERIPHERAL_RESET
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PERIPHERAL_RESET, 1);
    // Usually a stabilization wait after reset should be done, allowing the KSZ8851 internal PLL to fully stabilize.
    // We can skip this here, since it takes another ~ 200-300 ms until the ETH driver gets initialized during startup.
}

/// @brief Set the PoE voltage mode. Only applicable for hardware revisions supporting PoE voltage switching (e.g.
/// rev6).
/// @param mode mode 0 is normal operation, mode 1 enables the higher PoE voltage on compatible hardware revisions
static void init_poe_voltage_mode(uint8_t mode) {
    gpio_num_t poe_switch_pin = board_get_poe_switch_pin();
    if (poe_switch_pin != GPIO_NUM_NC) {
        gpio_set_level(poe_switch_pin, mode == 0 ? 0 : 1);
        ESP_LOGI(TAG, "Set PoE voltage mode to %d", mode);
    } else {
        ESP_LOGD(TAG, "PoE voltage mode cannot be set: not supported");
    }
}

/// @brief Create and configure output ports.
/// @param cfg Configuration to retrieve port settings.
/// @param adc_unit ADC unit for measuring GND.
/// @param vcc_channel ADC channel for measuring GND.
/// @return A map of initialized ExternalPort instances. Key is 1-based port number.
port_map_t init_external_ports(Config *cfg, std::shared_ptr<AdcUnit> adc_unit,
                               std::shared_ptr<AdcChannel> vcc_channel) {
    assert(cfg);
    assert(adc_unit);
    assert(vcc_channel);

    port_map_t ports;

    // Create ADC channels to read GND value from port
    // quick an dirty: just 2 output ports with the same ADC unit
    adc_channel_t     channels[EXTERNAL_PORT_COUNT] = {MEASURE_GND_1_ADC_CH, MEASURE_GND_2_ADC_CH};
    ext_port_config_t configs[EXTERNAL_PORT_COUNT] =
        // ports
        {// port 1
         {
             .gpio_gnd_switch = SWITCH_GND_1,
             .gpio_5v_switch = SWITCH_EXT_1,
             .gpio_rx = RX0,
             .gpio_tx = TX0,
             .uart_port = UART_NUM_1,
         },
         // port 2
         {
             .gpio_gnd_switch = SWITCH_GND_2,
             .gpio_5v_switch = SWITCH_EXT_2,
             .gpio_rx = RX1,
             .gpio_tx = TX1,
             .uart_port = UART_NUM_2,
         }};

    for (uint8_t i = 1; i <= EXTERNAL_PORT_COUNT; i++) {
        std::unique_ptr<AdcChannel> channel = adc_unit->createChannel(channels[i - 1]);
        if (channel == nullptr) {
            ESP_LOGE(TAG, "Cannot create output port %d: ADC channel %d creation failed", i, channels[i - 1]);
            uc_error_check(ESP_FAIL, i == 1 ? uc_errors::UC_ERROR_INIT_PORT1_ADC : uc_errors::UC_ERROR_INIT_PORT2_ADC);
            continue;
        }

        ports[i] = std::make_shared<ExternalPort>(i, configs[i - 1], std::move(channel), vcc_channel);
    }

    // Initialize output ports based on stored configuration
    for (uint8_t i = 1; i <= EXTERNAL_PORT_COUNT; i++) {
        ExtPortMode port_mode = cfg->getExternalPortMode(i);
        std::string uart_cfg = cfg->getExternalPortUart(i);

        auto cfg = UartConfig::fromString(uart_cfg.c_str());
        if (cfg == nullptr) {
            cfg = UartConfig::defaultCfg();
            ESP_LOGW(TAG, "Invalid UART configuration for port %d: using default", i);
        }

        esp_err_t ret = ports[i]->setUartConfig(std::move(cfg));
        if (ret == ESP_OK) {
            ret = ports[i]->init(port_mode);
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "External port %d could not be initialized. Error %d", i, ret);
            uc_error_check(ESP_FAIL, i == 1 ? uc_errors::UC_ERROR_INIT_PORT1 : uc_errors::UC_ERROR_INIT_PORT2);
        }
    }

    return ports;
}

esp_err_t init_charger(std::shared_ptr<AdcChannel> vcc_channel, std::shared_ptr<AdcUnit> shared_adc_unit) {
    assert(vcc_channel);
    assert(shared_adc_unit);

    adc_unit_t    unit = board_get_charging_current_adc_unit();
    adc_channel_t adc_ch = board_get_charging_current_adc_ch();

    std::shared_ptr<AdcUnit> adcUnit;
    if (board_get_revision() == 6) {
        // Rev6 shares ADC_UNIT_1 between charger current and port sensing.
        adcUnit = shared_adc_unit;
    } else {
        adcUnit = AdcUnit::create(unit);
    }
    ESP_RETURN_ON_FALSE(adcUnit, ESP_FAIL, TAG, "Cannot initialize charger: ADC unit %d creation failed", unit);

    std::unique_ptr<AdcChannel> channel = adcUnit->createChannel(adc_ch, ADC_ATTEN_DB_0);
    ESP_RETURN_ON_FALSE(channel, ESP_FAIL, TAG, "Cannot initialize charger: ADC channel %d creation failed", adc_ch);

    static RemoteCharger charger(std::move(channel), vcc_channel);

    return charger.start();
}

static void factoryResetHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    Config::instance().reset();
}

extern "C" void app_main(void) {
    esp_err_t ret = ESP_OK;

    board_init_revision();

    init_gpios();

    // to set another timezone
    // setenv("TZ", "UTC", 1);
    // tzset();
    init_nvs();

    // Create default event loop that is running in background
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_event_handler_register(UC_DOCK_EVENTS, UC_ACTION_RESET, factoryResetHandler, NULL));

    Config &cfg = Config::instance();

    // set PoE voltage mode based on stored configuration
    if (cfg.hasPoeFeature()) {
        uint8_t poe_mode = cfg.getPoeVoltageMode();
        init_poe_voltage_mode(poe_mode);
    }

    static Display *display = Display::instance(&cfg);
    if (display->init() == ESP_OK) {
        display->start();
    }

    init_led(cfg.getLedBrightness());
    uc_error_check(init_fs(), uc_errors::UC_ERROR_INIT_FS);

    std::shared_ptr<AdcUnit> vcc_adc_unit = AdcUnit::create(MEASURE_GND_1_ADC_UNIT);

    if (!vcc_adc_unit) {
        ESP_LOGE(TAG, "Cannot create VCC ADC unit");
        uc_fatal_error_check(ESP_FAIL, uc_errors::UC_ERROR_INIT_PORT_ADC);
        return;
    }

    std::shared_ptr<AdcChannel> vcc_channel = vcc_adc_unit->createChannel(adc_channel_t::ADC_CHANNEL_2);
    if (!vcc_channel) {
        ESP_LOGE(TAG, "Cannot create VCC channel");
        uc_fatal_error_check(ESP_FAIL, uc_errors::UC_ERROR_INIT_CHARGER);
        return;
    }

    // setup external ports
    auto ports = init_external_ports(&cfg, vcc_adc_unit, vcc_channel);
    uc_fatal_error_check(network_start(cfg.isNtpEnabled()), uc_errors::UC_ERROR_INIT_NET);
    uc_error_check(init_mdns(&cfg), uc_errors::UC_ERROR_INIT_MDNS);

    static WebServer web;
    uc_fatal_error_check(web.init(CONFIG_UCD_WEB_SERVER_PORT, CONFIG_UCD_WEB_MOUNT_POINT),
                         uc_errors::UC_ERROR_INIT_WEBSRV);

    web.setRestHandler(on_rest_sysinfo);
    web.setOtaHandler(on_ota_upload);

    static DockApi api(&cfg, &web, ports);
    api.init();

    uc_error_check(init_button(), uc_errors::UC_ERROR_INIT_BUTTON);
    if (cfg.hasChargingFeature()) {
        uc_error_check(init_charger(vcc_channel, vcc_adc_unit), uc_errors::UC_ERROR_INIT_CHARGER);
    }

    // Initialize IR
    InfraredService &irService = InfraredService::getInstance();
    irService.init(ports, cfg.getIrSendCore(), cfg.getIrSendPriority(), cfg.getIrLearnCore(), cfg.getIrLearnPriority(),
                   [=](IrResponse *response) -> esp_err_t {
                       esp_err_t ret;
                       // check if response is for a specific client (send IR response), or a learning broadcast
                       if (response->clientId >= 0) {
                           ret = web.sendWsTxt(response->clientId, response->message);
                       } else {
                           web.broadcastWsTxt(response->message);
                           ret = ESP_OK;
                       }
                       delete response;
                       return ret;
                   });

    if (cfg.isGcServerEnabled()) {
        GlobalCacheServer *gcServer = new GlobalCacheServer(&irService, &cfg, cfg.isGcServerBeaconEnabled());
    }

    start_stats_task();

    // heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
    // heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
}
