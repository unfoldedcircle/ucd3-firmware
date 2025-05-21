// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <map>
#include <memory>

#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "hal/adc_types.h"
#include "soc/gpio_num.h"

#include "adc_unit.h"
#include "config.h"
#include "ext_port_mode.h"
#include "stdlib.h"

class ExternalPort;
typedef std::map<uint8_t, std::shared_ptr<ExternalPort>> port_map_t;

/// @brief GPIO configuration & UART settings for an external port
// TODO this should be refactored and separated from ExternalPort: create an ExternalPortDriver which takes care of all
//      the hardware communication. -> simplifies writing unit tests and reduces mocking IDF files!
typedef struct {
    gpio_num_t  gpio_gnd_switch;
    gpio_num_t  gpio_5v_switch;
    gpio_num_t  gpio_rx;
    gpio_num_t  gpio_tx;
    uart_port_t uart_port;
} ext_port_config_t;

class ExternalPort {
 public:
    /// @brief Create a new external port.
    /// @param port Output port number, 1-based.
    /// @param reader ADC reader instance to measure GND of the port.
    /// @param vcc_reader ADC reader instance to measure VCC.
    ExternalPort(uint8_t port, ext_port_config_t config, std::unique_ptr<AdcReader> reader,
                 std::shared_ptr<AdcReader> vcc_reader);
    virtual ~ExternalPort();

    /// @brief Initialize the external port.
    ///
    /// This method should only be called once! Use `changeMode` to change the output port to another operation mode.
    /// @param mode Operation mode to initialize.
    /// @return ESP_OK if initialized,
    esp_err_t init(ExtPortMode mode);

    /// @brief Get the 1-based external port number.
    uint8_t getPortNumber() { return port_; };

    /// @brief Get the current operation mode of the external port.
    ExtPortMode getMode() { return mode_; };

    /// @brief Check if the current mode supports IR sending.
    /// @return true if IR can be sent, false otherwise.
    bool supportsIR();

    /// @brief Get the GPIO number to enable IR sending.
    /// @return GPIO number or GPIO_NUM_NC if not required.
    gpio_num_t getIrEnableGpio();

    /// @brief Get the GPIO number for IR sending.
    /// @return GPIO number or GPIO_NUM_NC if port is not configured for IR.
    gpio_num_t getIrGpio();

    /// @brief Returns true if the GPIO output for IR sending is inverted.
    bool isIrGpioInverted();

    /// @brief Change the operation mode of the external port.
    /// @param mode New operation mode.
    /// @return ESP_OK if mode could be changed.
    esp_err_t changeMode(ExtPortMode mode);

    /// @brief Provide UART settings for RS232 operation mode.
    ///
    /// This must be called before changing the operation mode with `changeMode(RS232)`.
    /// @param config UART configuration.
    /// @return ESP_OK if settings are valid.
    esp_err_t setUartConfig(std::unique_ptr<UartConfig> uart_cfg);

    /// @brief Return the current trigger mode if operation mode is `5V_TRIGGER`.
    /// @return true if trigger is active, false otherwise or if a different operation mode is set.
    bool isTriggerOn();

    /// @brief Set trigger state if operation mode is `5V_TRIGGER`.
    /// @param enabled true to set trigger (output high), false to disable it (output low).
    /// @return ESP_OK if state was set.
    esp_err_t setTrigger(bool enabled);

    /// @brief Set trigger output for the given time.
    /// @param duration_ms Active time of the trigger in milliseconds.
    /// @return ESP_OK if the trigger is active.
    esp_err_t triggerImpulse(uint32_t duration_ms);

    /// @brief Check if a specific port mode is supported.
    /// @param mode the port mode the test.
    /// @return true if mode is supported, false otherwise.
    bool isModeSupported(ExtPortMode mode);

 private:
    void        enableGround(bool enable);
    void        enable5V(bool enable);
    void        setTx(bool high);
    esp_err_t   measureGnd(int *voltage);
    esp_err_t   initUart();
    void        deinitUart();
    esp_err_t   measureVcc(int *voltage);
    void        applyVector(int g, int e, int t, bool is_mono);
    ExtPortMode detectPortType();

    /// @brief Detect if port has an IR-emitter connected.
    bool is_ir_emitter();
    /// @brief Detect if port has an IR-blaster connected.
    bool is_ir_blaster();

    static void triggerTimerCb(void *timer_id);

 private:
    // logging tag, based on port number
    char *tag_;

    uint8_t                     port_;
    ExtPortMode                 mode_;
    ext_port_config_t           config_;
    std::unique_ptr<UartConfig> uart_cfg_;
    std::unique_ptr<AdcReader>  adc_reader_;
    esp_timer_handle_t          trigger_timer_;
    SemaphoreHandle_t           port_lock_;
    std::shared_ptr<AdcReader>  vcc_reader_;
    // TODO do we need an event queue?
    QueueHandle_t uart_event_queue_;
};
