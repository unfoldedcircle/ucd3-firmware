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
#include "external_port.h"
#include "frogfs/frogfs.h"
#include "frogfs/vfs.h"
#include "globalcache_server.h"
#include "ir_codes.h"
#include "led_pattern.h"
#include "mdns.h"
#include "network.h"
#include "nvs_flash.h"
#include "ota.h"
#include "service_ir.h"
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

void gpio_init(gpio_num_t gpio_num, gpio_mode_t mode, gpio_pullup_t pullup = GPIO_PULLUP_ENABLE,
               gpio_pulldown_t pulldown = GPIO_PULLDOWN_DISABLE) {
    assert(GPIO_IS_VALID_GPIO(gpio_num));
    gpio_config_t cfg = {
        .pin_bit_mask = BIT64(gpio_num),
        .mode = mode,
        // for powersave reasons, the GPIO should not be floating, select pullup
        .pull_up_en = pullup,
        .pull_down_en = pulldown,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
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

    gpio_init(CHARGING_CURRENT, GPIO_MODE_INPUT);
    // also input to be able to read current state in charger monitor
    gpio_init(CHARGING_ENABLE, GPIO_MODE_INPUT_OUTPUT, GPIO_PULLUP_DISABLE);
    gpio_set_level(CHARGING_ENABLE, 0);

    // set ethernet / lcd reset to defined state, otherwise W5500 ain't happy (w5500.mac: W5500 version mismatched,
    // expected 0x04, got 0x00)
    gpio_init(PERIPHERAL_RESET, GPIO_MODE_OUTPUT);
    gpio_set_level(PERIPHERAL_RESET, 1);

    // internal IR
    gpio_init(IR_RECEIVE_PIN, GPIO_MODE_INPUT);

    gpio_init(IR_SEND_PIN_INT_TOP, GPIO_MODE_OUTPUT_OD, GPIO_PULLUP_DISABLE);
    gpio_init(IR_SEND_PIN_INT_SIDE, GPIO_MODE_OUTPUT_OD, GPIO_PULLUP_DISABLE);
    gpio_set_level(IR_SEND_PIN_INT_TOP, IR_SEND_PIN_INT_TOP_INVERTED);    // output low (inverted logic)
    gpio_set_level(IR_SEND_PIN_INT_SIDE, IR_SEND_PIN_INT_SIDE_INVERTED);  // output low (inverted logic)

    // configure UART out for IR blaster
    gpio_init(TX0, GPIO_MODE_OUTPUT_OD);
    gpio_init(TX1, GPIO_MODE_OUTPUT_OD);
    gpio_init(RX0, GPIO_MODE_INPUT);
    gpio_init(RX1, GPIO_MODE_INPUT);
    gpio_set_level(TX0, TX_INVERTED);  // output low
    gpio_set_level(TX1, TX_INVERTED);  // output low

    // RS232 / IR out 1
    gpio_init(SWITCH_EXT_1, SWITCH_EXT_GPIO_MODE, GPIO_PULLUP_DISABLE);
    gpio_init(MEASURE_GND_1, GPIO_MODE_INPUT, GPIO_PULLUP_DISABLE);
    // also use input to retrieve current state in trigger output mode
    gpio_init(SWITCH_GND_1, GPIO_MODE_INPUT_OUTPUT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_ENABLE);
    // disable 5V & GND
    gpio_set_level(SWITCH_EXT_1, SWITCH_EXT_INVERTED);  // output low
    gpio_set_level(SWITCH_GND_1, 0);                    // output low

    // RS232 / IR out 2
    gpio_init(SWITCH_EXT_2, SWITCH_EXT_GPIO_MODE, GPIO_PULLUP_DISABLE);
    gpio_init(MEASURE_GND_2, GPIO_MODE_INPUT, GPIO_PULLUP_DISABLE);
    // also use input to retrieve current state in trigger output mode
    gpio_init(SWITCH_GND_2, GPIO_MODE_INPUT_OUTPUT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_ENABLE);
    // disable 5V & GND
    gpio_set_level(SWITCH_EXT_2, SWITCH_EXT_INVERTED);
    gpio_set_level(SWITCH_GND_2, 0);
}

/// @brief Create and configure output ports.
/// @param cfg Configuration to retrieve port settings.
/// @return A map of initialized ExternalPort instances. Key is 1-based port number.
port_map_t init_external_ports(Config *cfg) {
    port_map_t ports;
    adc_unit_t unit = MEASURE_GND_1_ADC_UNIT;

    std::shared_ptr<AdcUnit> adcUnit = AdcUnit::create(unit);
    if (!adcUnit) {
        ESP_LOGE(TAG, "Cannot create output ports: ADC unit %d creation failed", unit);
        uc_error_check(ESP_FAIL, uc_errors::UC_ERROR_INIT_PORT_ADC);
        return ports;
    }

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

    std::shared_ptr<AdcChannel> channel2 = adcUnit->createChannel(adc_channel_t::ADC_CHANNEL_2);
    if (!channel2) {
        ESP_LOGE(TAG, "Error: vcc reference channel 2 couldnt be created!");
    }

    for (uint8_t i = 1; i <= EXTERNAL_PORT_COUNT; i++) {
        std::unique_ptr<AdcChannel> channel = adcUnit->createChannel(channels[i - 1]);
        if (channel == nullptr) {
            ESP_LOGE(TAG, "Cannot create output port %d: ADC channel %d creation failed", i, channels[i - 1]);
            uc_error_check(ESP_FAIL, i == 1 ? uc_errors::UC_ERROR_INIT_PORT1_ADC : uc_errors::UC_ERROR_INIT_PORT2_ADC);
            continue;
        }

        ports[i] = std::make_shared<ExternalPort>(i, configs[i - 1], std::move(channel), channel2);
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

esp_err_t init_charger() {
    adc_unit_t unit = CHARGING_CURRENT_ADC_UNIT;
    auto       adc_ch = CHARGING_CURRENT_ADC_CH;

    std::shared_ptr<AdcUnit> adcUnit = AdcUnit::create(unit);
    ESP_RETURN_ON_FALSE(adcUnit, ESP_FAIL, TAG, "Cannot initialize charger: ADC unit %d creation failed", unit);

    std::unique_ptr<AdcChannel> channel = adcUnit->createChannel(adc_ch, ADC_ATTEN_DB_0);
    ESP_RETURN_ON_FALSE(channel, ESP_FAIL, TAG, "Cannot initialize charger: ADC channel %d creation failed", adc_ch);

    static RemoteCharger charger(std::move(channel));

    return charger.start();
}

static void factoryResetHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    Config::instance().reset();
}

extern "C" void app_main(void) {
    esp_err_t ret = ESP_OK;
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

    static Display *display = Display::instance(&cfg);
    if (display->init() == ESP_OK) {
        display->start();
    }

    init_led();
    uc_error_check(init_fs(), uc_errors::UC_ERROR_INIT_FS);

    // setup external ports
    auto ports = init_external_ports(&cfg);

    uc_fatal_error_check(network_start(), uc_errors::UC_ERROR_INIT_NET);
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
        uc_error_check(init_charger(), uc_errors::UC_ERROR_INIT_CHARGER);
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

    // heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
    // heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
}
