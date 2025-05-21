// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Based on the Espressif IDF examples (license: Unlicense OR CC0-1.0)

#include "network_ethernet.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "board.h"
#include "sdkconfig.h"

typedef struct {
    uint8_t  spi_cs_gpio;
    int8_t   int_gpio;
    uint32_t polling_ms;
    int8_t   phy_reset_gpio;
    uint8_t  phy_addr;
    uint8_t *mac_addr;
} spi_eth_module_config_t;

static const char *TAG = "ETH";

static bool gpio_isr_svc_init_by_eth = false;  // indicates that we initialized the GPIO ISR service

/**
 * @brief SPI bus initialization (to be used by Ethernet SPI modules)
 *
 * @return
 *          - ESP_OK on success
 */
static esp_err_t spi_bus_init(void) {
    esp_err_t ret = ESP_OK;

#if (CONFIG_UCD_ETH_SPI_INT0_GPIO >= 0)
    // Install GPIO ISR handler to be able to service SPI Eth modules interrupts
    ret = gpio_install_isr_service(0);
    if (ret == ESP_OK) {
        gpio_isr_svc_init_by_eth = true;
    } else if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "GPIO ISR handler has been already installed");
        ret = ESP_OK;  // ISR handler has been already installed so no issues
    } else {
        ESP_LOGE(TAG, "GPIO ISR handler install failed");
        goto err;
    }
#endif

    // Init SPI bus
    spi_bus_config_t buscfg = {
        .miso_io_num = CONFIG_UCD_ETH_SPI_MISO_GPIO,
        .mosi_io_num = CONFIG_UCD_ETH_SPI_MOSI_GPIO,
        .sclk_io_num = CONFIG_UCD_ETH_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_GOTO_ON_ERROR(spi_bus_initialize(CONFIG_UCD_ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO), err, TAG,
                      "SPI host #%d init failed", CONFIG_UCD_ETH_SPI_HOST);

err:
    return ret;
}

/**
 * @brief Ethernet SPI modules initialization
 *
 * @param[in] spi_eth_module_config specific SPI Ethernet module configuration
 * @param[out] mac_out optionally returns Ethernet MAC object
 * @param[out] phy_out optionally returns Ethernet PHY object
 * @return
 *          - esp_eth_handle_t if init succeeded
 *          - NULL if init failed
 */
static esp_eth_handle_t eth_init_spi(spi_eth_module_config_t *spi_eth_module_config, esp_eth_mac_t **mac_out,
                                     esp_eth_phy_t **phy_out) {
    esp_eth_handle_t ret = NULL;

    // Init common MAC and PHY configs to default
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    // Update PHY config based on board specific configuration
    phy_config.phy_addr = spi_eth_module_config->phy_addr;
    phy_config.reset_gpio_num = spi_eth_module_config->phy_reset_gpio;

    // Configure SPI interface for specific SPI module
    spi_device_interface_config_t spi_devcfg = {.mode = 0,
                                                .clock_speed_hz = CONFIG_UCD_ETH_SPI_CLOCK_MHZ * 1000 * 1000,
                                                .queue_size = 20,
                                                .spics_io_num = spi_eth_module_config->spi_cs_gpio};

// Init vendor specific MAC config to default, and create new SPI Ethernet MAC instance
// and new PHY instance based on board configuration
#if CONFIG_UCD_USE_KSZ8851SNL
    eth_ksz8851snl_config_t ksz8851snl_config = ETH_KSZ8851SNL_DEFAULT_CONFIG(CONFIG_UCD_ETH_SPI_HOST, &spi_devcfg);
    ksz8851snl_config.int_gpio_num = spi_eth_module_config->int_gpio;
    ksz8851snl_config.poll_period_ms = spi_eth_module_config->polling_ms;
    esp_eth_mac_t *mac = esp_eth_mac_new_ksz8851snl(&ksz8851snl_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_ksz8851snl(&phy_config);
#elif CONFIG_UCD_USE_DM9051
    eth_dm9051_config_t dm9051_config = ETH_DM9051_DEFAULT_CONFIG(CONFIG_UCD_ETH_SPI_HOST, &spi_devcfg);
    dm9051_config.int_gpio_num = spi_eth_module_config->int_gpio;
    dm9051_config.poll_period_ms = spi_eth_module_config->polling_ms;
    esp_eth_mac_t *mac = esp_eth_mac_new_dm9051(&dm9051_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_dm9051(&phy_config);
#elif CONFIG_UCD_USE_W5500
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(CONFIG_UCD_ETH_SPI_HOST, &spi_devcfg);
    w5500_config.int_gpio_num = spi_eth_module_config->int_gpio;
    w5500_config.poll_period_ms = spi_eth_module_config->polling_ms;
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
#endif  // CONFIG_UCD_USE_W5500

    // Init Ethernet driver to default and install it
    esp_eth_handle_t eth_handle = NULL;
    esp_eth_config_t eth_config_spi = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_GOTO_ON_FALSE(esp_eth_driver_install(&eth_config_spi, &eth_handle) == ESP_OK, NULL, err, TAG,
                      "SPI Ethernet driver install failed");

    // The SPI Ethernet module might not have a burned factory MAC address, we can set it manually.
    if (spi_eth_module_config->mac_addr != NULL) {
        ESP_GOTO_ON_FALSE(esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, spi_eth_module_config->mac_addr) == ESP_OK,
                          NULL, err, TAG, "SPI Ethernet MAC address config failed");
    }

    if (mac_out != NULL) {
        *mac_out = mac;
    }
    if (phy_out != NULL) {
        *phy_out = phy;
    }
    return eth_handle;
err:
    if (eth_handle != NULL) {
        esp_eth_driver_uninstall(eth_handle);
    }
    if (mac != NULL) {
        mac->del(mac);
    }
    if (phy != NULL) {
        phy->del(phy);
    }
    return ret;
}

esp_err_t eth_init(esp_eth_handle_t *eth_handle_out) {
    esp_err_t         ret = ESP_OK;
    esp_eth_handle_t *eth_handle = NULL;

    ESP_GOTO_ON_FALSE(eth_handle_out != NULL, ESP_ERR_INVALID_ARG, err, TAG, "invalid arguments: initialized handle");
    eth_handle = calloc(1, sizeof(esp_eth_handle_t));
    ESP_GOTO_ON_FALSE(eth_handle != NULL, ESP_ERR_NO_MEM, err, TAG, "no memory");

    ESP_GOTO_ON_ERROR(spi_bus_init(), err, TAG, "SPI bus init failed");
    // Init specific SPI Ethernet module configuration from Kconfig (CS GPIO, Interrupt GPIO, etc.)
    spi_eth_module_config_t spi_eth_module_config = {0};

    spi_eth_module_config.spi_cs_gpio = CONFIG_UCD_ETH_SPI_CS0_GPIO;
    spi_eth_module_config.int_gpio = CONFIG_UCD_ETH_SPI_INT0_GPIO;
    spi_eth_module_config.polling_ms = CONFIG_UCD_ETH_SPI_POLLING0_MS;
    spi_eth_module_config.phy_reset_gpio = CONFIG_UCD_ETH_SPI_PHY_RST0_GPIO;
    spi_eth_module_config.phy_addr = CONFIG_UCD_ETH_SPI_PHY_ADDR0;

    // The w5500 doesn't come with a burnt-in MAC address! Use the derived ETH MAC from espressif
    uint8_t mac_addr[ETH_ADDR_LEN];
    ESP_GOTO_ON_ERROR(esp_read_mac(mac_addr, ESP_MAC_ETH), err, TAG, "reading ETH MAC failed");
    ESP_LOGD(TAG, "ethernet MAC: %02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3],
             mac_addr[4], mac_addr[5]);

    spi_eth_module_config.mac_addr = mac_addr;

    eth_handle = eth_init_spi(&spi_eth_module_config, NULL, NULL);
    ESP_GOTO_ON_FALSE(eth_handle, ESP_FAIL, err, TAG, "SPI Ethernet init failed");

    *eth_handle_out = eth_handle;

    return ret;
err:
    free(eth_handle);
    return ret;
}

esp_err_t eth_deinit(esp_eth_handle_t *eth_handle) {
    ESP_RETURN_ON_FALSE(eth_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Ethernet handle cannot be NULL");
    esp_eth_mac_t *mac = NULL;
    esp_eth_phy_t *phy = NULL;
    if (eth_handle != NULL) {
        esp_eth_get_mac_instance(eth_handle, &mac);
        esp_eth_get_phy_instance(eth_handle, &phy);
        ESP_RETURN_ON_ERROR(esp_eth_driver_uninstall(eth_handle), TAG, "Ethernet %p uninstall failed", eth_handle);
    }
    if (mac != NULL) {
        mac->del(mac);
    }
    if (phy != NULL) {
        phy->del(phy);
    }
    spi_bus_free(CONFIG_UCD_ETH_SPI_HOST);
#if (CONFIG_UCD_ETH_SPI_INT0_GPIO >= 0)
    // We installed the GPIO ISR service so let's uninstall it too.
    // BE CAREFUL HERE though since the service might be used by other functionality!
    if (gpio_isr_svc_init_by_eth) {
        ESP_LOGW(TAG, "uninstalling GPIO ISR service!");
        gpio_uninstall_isr_service();
    }
#endif
    free(eth_handle);
    return ESP_OK;
}

static const ledc_channel_t ETH_CHANNEL = LEDC_CHANNEL_0;  // LEDC_CHANNEL_4;
const int                   LED_PWM_FREQ = 12000;
const ledc_timer_bit_t      LED_RESOLUTION = LEDC_TIMER_8_BIT;
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE

esp_err_t eth_pwm_led_init(void) {
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {.speed_mode = LEDC_MODE,
                                      .duty_resolution = LED_RESOLUTION,
                                      .timer_num = LEDC_TIMER,
                                      .freq_hz = LED_PWM_FREQ,
                                      .clk_cfg = LEDC_AUTO_CLK};
    ESP_RETURN_ON_ERROR(ledc_timer_config(&ledc_timer), TAG, "ETH PWM LED timer failed");

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {.speed_mode = LEDC_MODE,
                                          .channel = ETH_CHANNEL,
                                          .timer_sel = LEDC_TIMER,
                                          .intr_type = LEDC_INTR_DISABLE,
                                          .gpio_num = ETH_LED_PWM,
                                          .duty = 0,  // Set duty to 0%
                                          .hpoint = 0};
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ledc_channel), TAG, "ETH PWM LED channel failed");

    return ESP_OK;
}

esp_err_t set_eth_led_brightness(int value) {
#if defined(ETH_LED_PWM)
    // if (!m_testMode) {
    if (value >= 0 && value <= 255) {
        ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_MODE, ETH_CHANNEL, value), TAG, "failed to set ETH PWM LED duty");
        // Update duty to apply the new value
        ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_MODE, ETH_CHANNEL), TAG, "failed to update ETH PWM LED duty");
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    // }
#endif
    return ESP_OK;
}
