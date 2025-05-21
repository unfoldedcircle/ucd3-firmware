// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "external_port.h"

#include <cstring>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "board.h"
#include "mem_util.h"
#include "sdkconfig.h"
#include "uc_events.h"

static const char *const TAG = "PORT";

typedef struct {
    int expected_rx;
    int voltage_min;
    int voltage_max;
} port_signature_t;

typedef struct {
    uint8_t gnd;
    uint8_t ext;
    uint8_t tx;
} vector_t;

#define SIGNATURE_LEN(x) (sizeof(x) / sizeof(port_signature_t))

/* clang-format off */
static const port_signature_t SIGNATURE_MONO[] = {
    {0, 2000, 2900},
    {0, 2000, 2900}, // second value is higher then first, for future improvement
    {1,    0,  200},
    {1,    0,  200},
    {0, 2000, 2900},
    {1,    0,  200},
    {0, 2000, 2900},
    {0, 2000, 2900},
};

static const port_signature_t SIGNATURE_STEREO[] = {
    {0, 2000, 3300},
    {1, 2000, 3300},
    {1,    0,  200},
    {1,    0,  200},
    {0,    0,  200},
    {1,    0,  200},
    {0,    0,  200},
    {0,    0,  200},
};

static const port_signature_t SIGNATURE_BLASTER[] = {
    {0, 1700, 3300},
    {0, 1700, 2500}, // second value is lower then first, for future improvement
    {1,    0,  200},
    {1,    0,  200},
    {0,  900, 2500},
    {1,    0,  200},
    {0,    0,  200},
    {0,    0,  200},
};

static const port_signature_t SIGNATURE_EMPTY[] = {
    {0, 0, 50},
    {1, 0, 50},
    {1, 0, 50},
    {1, 0, 50},
    {0, 0, 50},
    {1, 0, 50},
    {0, 0, 50},
    {0, 0, 50},
};

static const vector_t VECTOR[] = {
    {0, 1, 0},
    {0, 0, 0},
    {1, 0, 1},
    {0, 0, 1},
    {0, 1, 2},
    {1, 0, 0},
    {2, 1, 0},
    {2, 1, 1}
};
/* clang-format on */

static bool match_signature(const port_signature_t *sig, size_t count, const int *voltages, const int *rx_vals) {
    for (size_t i = 0; i < count; ++i) {
        if (sig[i].expected_rx != -1 && rx_vals[i] != sig[i].expected_rx) {
            return false;
        }
        if (voltages[i] < sig[i].voltage_min || voltages[i] > sig[i].voltage_max) {
            return false;
        }
    }
    return true;
}

// TODO static or dynamic config option?
#define CONFIG_PORT_UART_RING_BUFFER_SIZE 512

ExternalPort::ExternalPort(uint8_t port, ext_port_config_t config, std::unique_ptr<AdcReader> reader,
                           std::shared_ptr<AdcReader> vcc_reader)
    : port_(port),
      mode_(NOT_CONFIGURED),
      config_(config),
      uart_cfg_(nullptr),
      adc_reader_(std::move(reader)),
      trigger_timer_(nullptr),
      port_lock_(xSemaphoreCreateMutex()),
      vcc_reader_(vcc_reader),
      uart_event_queue_(nullptr) {
    assert(port_lock_);
    if (asprintf(&tag_, "EXT%d", port) < 0) {
        tag_ = nullptr;
    }
}

ExternalPort::~ExternalPort() {
    ESP_LOGI(tag_ ? tag_ : TAG, "ExternalPort destructor");
    if (trigger_timer_) {
        esp_timer_stop(trigger_timer_);
        esp_timer_delete(trigger_timer_);
    }
    deinitUart();
    FREE_AND_NULL(tag_);
}

esp_err_t ExternalPort::init(ExtPortMode mode) {
    esp_err_t ret = ESP_OK;

    if (!tag_) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(tag_, "Initializing output port %d", port_);

    if (port_ < 1 || port_ > EXTERNAL_PORT_COUNT) {
        ESP_LOGW(tag_, "Invalid port number %d. Supported ports: %d", port_, EXTERNAL_PORT_COUNT);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // disable output switches
    enableGround(false);
    enable5V(false);

    if (mode != NOT_CONFIGURED) {
        ESP_RETURN_ON_ERROR(changeMode(mode), tag_, "Failed to set output port mode");
    }

    return ret;
}

bool ExternalPort::supportsIR() {
    return mode_ == IR_BLASTER || mode_ == IR_EMITTER_MONO_PLUG || mode_ == IR_EMITTER_STEREO_PLUG;
}

gpio_num_t ExternalPort::getIrEnableGpio() {
    // Not used anymore with production boards.
    // Older revisions required to enable a GPIO before sending IR
    return GPIO_NUM_NC;
}

gpio_num_t ExternalPort::getIrGpio() {
    switch (mode_) {
        case IR_BLASTER:
            return config_.gpio_tx;
        case IR_EMITTER_MONO_PLUG:
        case IR_EMITTER_STEREO_PLUG:
            return config_.gpio_gnd_switch;
        default:
            return GPIO_NUM_NC;
    }
}

bool ExternalPort::isIrGpioInverted() {
    // all is switched with TX
    switch (mode_) {
        case IR_BLASTER:
        case IR_EMITTER_MONO_PLUG:
        case IR_EMITTER_STEREO_PLUG:
            return TX_INVERTED;
        default:
            return false;
    }
}

esp_err_t ExternalPort::changeMode(ExtPortMode mode) {
    esp_err_t ret = ESP_OK;

    ESP_LOGI(tag_, "Setting output port %d to mode: %s", port_, ExtPortMode_to_str(mode));

    if (!isModeSupported(mode)) {
        ESP_LOGW(tag_, "Output %d does not support mode %s", port_, ExtPortMode_to_str(mode));
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (mode == mode_) {
        return ESP_OK;
    }

    // Prevent parallel access while port is set up.
    // Detection also takes some time while changing various output gpios!
    if (xSemaphoreTake(port_lock_, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(tag_, "Port is locked, cannot change mode to: %s", ExtPortMode_to_str(mode));
        return ESP_ERR_NOT_ALLOWED;
    }

    // Set to idle state, turn off output switches
    enableGround(false);
    enable5V(false);
    setTx(false);

    if (mode_ == RS232) {
        deinitUart();
    } else if (mode_ == TRIGGER_5V) {
        if (trigger_timer_) {
            esp_timer_stop(trigger_timer_);
            esp_timer_delete(trigger_timer_);
            trigger_timer_ = nullptr;
        }
    }

    mode_ = NOT_CONFIGURED;
    uc_event_ext_port_mode_t event_port;
    event_port.port = port_;
    event_port.mode = mode;
    event_port.active_mode = mode_;
    event_port.state = ESP_OK;
    event_port.uart_cfg[0] = 0;

    if (mode == AUTO) {
        ExtPortMode detected = detectPortType();
        if (detected == NOT_CONFIGURED) {
            ESP_LOGI(tag_, "No known port configuration detected.");
            goto err;
        }
        ESP_LOGI(tag_, "Port configuration detected: %s", ExtPortMode_to_str(detected));
        mode = detected;
    }

    // configure port
    switch (mode) {
        case NOT_CONFIGURED:
            // nothing to do
            break;
        case IR_BLASTER: {
            // Stereo plug. Signal with TX
            setTx(false);
            enableGround(true);
            enable5V(true);
            break;
        }
        case IR_EMITTER_MONO_PLUG: {
            // Mono plug: Center & Base are shorted
            // Disable 5V, otherwise PTC is tripped.
            // Signal with GND, TX always on. See getIrGpio()
            enable5V(false);
            enableGround(false);
            setTx(true);
            ESP_LOGI(tag_, "IR-emitter with mono-plug configured");
            break;
        }
        case IR_EMITTER_STEREO_PLUG:
            // Stereo plug
            // 5V doesn't matter.
            // Signal with GND, TX always on. See getIrGpio()
            enable5V(false);
            enableGround(false);
            setTx(true);
            ESP_LOGI(tag_, "IR-emitter with stereo-plug configured");
            break;
        case TRIGGER_5V: {
            // Stereo plug: trigger between base and middle.
            // Switching with EXT & GND for safety. TX is not used.
            enable5V(false);
            enableGround(false);
            ESP_LOGI(tag_, "5V trigger configured");
            break;
        }
        case RS232:
            enableGround(true);
            ESP_GOTO_ON_ERROR(initUart(), err, tag_, "UART initialization failed");
            ESP_LOGI(tag_, "RS232 configured");

            if (uart_cfg_) {
                strncpy(event_port.uart_cfg, uart_cfg_->toString().c_str(), sizeof(event_port.uart_cfg));
            }

            break;
        default:
            ret = ESP_ERR_NOT_SUPPORTED;
            goto err;
    }

    mode_ = mode;
    xSemaphoreGive(port_lock_);

    event_port.active_mode = mode_;
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_event_post(UC_DOCK_EVENTS, UC_EVENT_EXT_PORT_MODE, &event_port, sizeof(event_port), pdMS_TO_TICKS(200)));

    return ESP_OK;

// error handler: reset everything to idle state
err:
    mode_ = NOT_CONFIGURED;
    enableGround(false);
    enable5V(false);
    setTx(false);
    xSemaphoreGive(port_lock_);

    // send port configuration event with error status
    event_port.state = ret;
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_event_post(UC_DOCK_EVENTS, UC_EVENT_EXT_PORT_MODE, &event_port, sizeof(event_port), pdMS_TO_TICKS(200)));

    return ret;
}

esp_err_t ExternalPort::setUartConfig(std::unique_ptr<UartConfig> config) {
    if (config == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!isModeSupported(RS232)) {
        ESP_LOGW(tag_, "Output %d does not support RS232 mode", port_);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (mode_ == RS232) {
        ESP_LOGW(
            tag_,
            "Output is already configured to RS232: new UART configuration will be applied at next initialization!");
    }

    uart_cfg_ = std::move(config);

    return ESP_OK;
}

bool ExternalPort::isTriggerOn() {
    if (mode_ != TRIGGER_5V) {
        return false;
    }
    // Note: only gpio_gnd_switch is configured for input.
    // Since switching is encapsulated in this class, this is good enough.
    return gpio_get_level(config_.gpio_gnd_switch) == 1;
}

esp_err_t ExternalPort::setTrigger(bool enabled) {
    if (mode_ != TRIGGER_5V) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGI(tag_, "set 5V trigger: %d", enabled);

    if (xSemaphoreTake(port_lock_, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(tag_, "Port is locked, cannot set trigger to: %d", enabled);
        return ESP_ERR_NOT_ALLOWED;
    }

    enableGround(enabled);
    enable5V(enabled);

    xSemaphoreGive(port_lock_);
    return ESP_OK;
}

esp_err_t ExternalPort::triggerImpulse(uint32_t duration_ms) {
    if (mode_ != TRIGGER_5V) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!trigger_timer_) {
        const esp_timer_create_args_t timer_args = {
            .callback = &triggerTimerCb,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "trigger",
            .skip_unhandled_events = false,
        };
        ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &trigger_timer_), tag_, "Failed to create trigger timer");
    }

    // TODO what to do if trigger is already running: pro-long or return error (current behaviour)?

    ESP_LOGI(tag_, "trigger impulse for %lums", duration_ms);
    ESP_RETURN_ON_ERROR(esp_timer_start_once(trigger_timer_, duration_ms * 1000), tag_,
                        "Failed to start trigger timer");
    return setTrigger(true);
}

void ExternalPort::triggerTimerCb(void *arg) {
    ExternalPort *that = static_cast<ExternalPort *>(arg);
    that->setTrigger(false);
}

bool ExternalPort::isModeSupported(ExtPortMode mode) {
    switch (mode) {
        case AUTO:
        case NOT_CONFIGURED:
            return true;
        case IR_BLASTER:
            return EXTERNAL_IR_BLASTER_SUPPORT & (1 << (port_ - 1));
        case IR_EMITTER_MONO_PLUG:
            return EXTERNAL_IR_EMITTER_MONO_SUPPORT & (1 << (port_ - 1));
        case IR_EMITTER_STEREO_PLUG:
            return EXTERNAL_IR_EMITTER_STEREO_SUPPORT & (1 << (port_ - 1));
        case TRIGGER_5V:
            return EXTERNAL_5V_TRIGGER_SUPPORT & (1 << (port_ - 1));
        case RS232:
            return EXTERNAL_RS232_SUPPORT & (1 << (port_ - 1));
        default:
            return false;
    }
}

void ExternalPort::enableGround(bool enable) {
    gpio_set_level(config_.gpio_gnd_switch, enable ? 1 : 0);
}

void ExternalPort::enable5V(bool enable) {
    // inverted logic depending on board revision!
    if (SWITCH_EXT_INVERTED) {
        enable = !enable;
    }
    gpio_set_level(config_.gpio_5v_switch, enable ? 1 : 0);
}

void ExternalPort::setTx(bool high) {
    // inverted logic!
    gpio_set_level(config_.gpio_tx, !high);
}

esp_err_t ExternalPort::measureGnd(int *voltage) {
    return adc_reader_->read(voltage);
}

esp_err_t ExternalPort::measureVcc(int *voltage) {
    if (!vcc_reader_) {
        ESP_LOGW(tag_, "VCC reference reader not available");
        return ESP_ERR_INVALID_STATE;
    }
    return vcc_reader_->read(voltage);
}

esp_err_t ExternalPort::initUart() {
    esp_err_t ret = ESP_OK;

    if (uart_cfg_ == nullptr || config_.uart_port == UART_NUM_MAX) {
        return ESP_ERR_INVALID_STATE;
    }

    uart_config_t uart_config = uart_cfg_->toConfig();

    // TODO finalize init uart with queue etc
    int event_queue_size = 16;
    int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif
    ESP_GOTO_ON_ERROR(
        uart_driver_install(config_.uart_port, CONFIG_PORT_UART_RING_BUFFER_SIZE, CONFIG_PORT_UART_RING_BUFFER_SIZE,
                            event_queue_size, &uart_event_queue_, intr_alloc_flags),
        err_uart_install, tag_, "install uart driver failed");
    ESP_GOTO_ON_ERROR(uart_param_config(config_.uart_port, &uart_config), err_uart_config, tag_,
                      "config uart parameter failed");
    ESP_GOTO_ON_ERROR(
        uart_set_pin(config_.uart_port, config_.gpio_tx, config_.gpio_rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
        err_uart_config, tag_, "config uart gpio failed");

    ESP_GOTO_ON_ERROR(uart_set_line_inverse(config_.uart_port, UART_SIGNAL_TXD_INV | UART_SIGNAL_RXD_INV),
                      err_uart_config, tag_, "uart line inverse failed");

    return ESP_OK;

err_uart_install:
    uart_driver_delete(config_.uart_port);
err_uart_config:
    return ret;
}

void ExternalPort::deinitUart() {
    // TODO(zehnm) stop task & event loop
    // vTaskDelete(uart_tsk_hdl_);
    // esp_event_loop_delete(uear_event_loop_hdl_);

    if (uart_event_queue_) {
        xQueueReset(uart_event_queue_);
        uart_event_queue_ = nullptr;
    }
    if (config_.uart_port != UART_NUM_MAX) {
        uart_driver_delete(config_.uart_port);
        config_.uart_port = UART_NUM_MAX;
    }
}

void ExternalPort::applyVector(int g, int e, int t, bool is_mono) {
    enable5V(e > 0);
    enableGround((is_mono && g == 2) ? false : (g > 0));
    setTx(!((is_mono && t == 2) ? false : (t > 0)));
}

ExtPortMode ExternalPort::detectPortType() {
    esp_err_t ret = ESP_OK;

    const size_t test_cases = sizeof(VECTOR) / sizeof(vector_t);
    int          voltages[test_cases];
    int          rx_vals[test_cases];
    bool         is_mono = false;

    for (size_t i = 0; i < test_cases; ++i) {
        enable5V(false);
        setTx(true);
        enableGround(true);
        vTaskDelay(pdMS_TO_TICKS(50));
        enableGround(false);
        vTaskDelay(pdMS_TO_TICKS(50));

        ESP_LOGI(tag_, "[%zu] applyVector: GND=%d, EXT=%d, TX=%d, is_mono=%d", i, VECTOR[i].gnd, VECTOR[i].ext,
                 VECTOR[i].tx, is_mono);
        applyVector(VECTOR[i].gnd, VECTOR[i].ext, VECTOR[i].tx, is_mono);

        if (i == 0) {
            vTaskDelay(pdMS_TO_TICKS(500));  // allow charge buildup

            int v_meas_sum = 0;
            int v_ref_sum = 0;

            for (int k = 0; k < 5; ++k) {
                int tmp_meas = 0;
                int tmp_ref = 0;

                if (measureGnd(&tmp_meas) != ESP_OK) {
                    ESP_LOGE(tag_, "Failed to read GND voltage (sample %d)", k);
                    return NOT_CONFIGURED;
                }

                if (measureVcc(&tmp_ref) != ESP_OK) {
                    ESP_LOGE(tag_, "Failed to read VCC voltage (sample %d)", k);
                    return NOT_CONFIGURED;
                }

                v_meas_sum += tmp_meas;
                v_ref_sum += tmp_ref;
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            int v_meas = v_meas_sum / 5;
            int v_ref = v_ref_sum / 5;

            is_mono = (v_meas >= v_ref - 36 && v_meas <= v_ref + 5);

            ESP_LOGI(tag_, "Mono-plug check: vcc=%dmV, gnd=%dmV → Δ=%d", v_ref, v_meas, v_meas - v_ref);
            if (is_mono) {
                ESP_LOGW(tag_,
                         "MONO-PLUG detected: enableGround() and enable5V() can cause BROWNOUT when both are high!");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30));

        int v_adc = -1;
        int rx = -1;

        if (measureGnd(&v_adc) != ESP_OK) {
            ESP_LOGE(tag_, "[%zu] Failed to read GND voltage", i);
            v_adc = -1;
        }
        rx = gpio_get_level(config_.gpio_rx);
        ESP_LOGI(tag_, "[%zu] RX = %d, ADC GND = %dmV", i, rx, v_adc);

        voltages[i] = v_adc;
        rx_vals[i] = rx;
    }

    if (match_signature(SIGNATURE_MONO, SIGNATURE_LEN(SIGNATURE_MONO), voltages, rx_vals)) {
        return IR_EMITTER_MONO_PLUG;
    }
    if (match_signature(SIGNATURE_STEREO, SIGNATURE_LEN(SIGNATURE_STEREO), voltages, rx_vals)) {
        return IR_EMITTER_STEREO_PLUG;
    }
    if (match_signature(SIGNATURE_BLASTER, SIGNATURE_LEN(SIGNATURE_BLASTER), voltages, rx_vals)) {
        return IR_BLASTER;
    }
    if (match_signature(SIGNATURE_EMPTY, SIGNATURE_LEN(SIGNATURE_EMPTY), voltages, rx_vals)) {
        return NOT_CONFIGURED;
    }

    return NOT_CONFIGURED;
}
