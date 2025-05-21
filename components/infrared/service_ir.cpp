// SPDX-FileCopyrightText: Copyright (c) 2022 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "service_ir.h"

#include <IRac.h>
#include <IRtimer.h>
#include <IRutils.h>

#include "esp_event.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"

#include "IRrecv.h"
#include "IRremoteESP8266.h"
#include "IRsend.h"
#include "cJSON.h"
#include "globalcache.h"
#include "globalcache_server.h"
#include "ir_codes.h"
#include "sdkconfig.h"
#include "uc_events.h"
#include "util_types.h"

const char *irLog = "IR";
const char *irLogSend = "IRSEND";
const char *irLogLearn = "IRLEARN";

const int IR_LEARNING_BIT = BIT0;
const int IR_REPEAT_BIT = BIT1;
const int IR_REPEAT_STOP_BIT = BIT2;

// good explanation of IRrecv parameters:
// https://github.com/crankyoldgit/IRremoteESP8266/blob/master/examples/IRrecvDumpV3/IRrecvDumpV3.ino
const uint16_t kCaptureBufferSize = 1024;  // 1024 == ~511 bits
// Suits most messages, while not swallowing many repeats. Not suited for AC IR remotes!
const uint8_t  kTimeout = 15;       // Milli-Seconds.
const uint16_t kFrequency = 38000;  // in Hz. e.g. 38kHz.
// Set the smallest sized "UNKNOWN" message packets we actually care about.
const uint16_t kMinUnknownSize = 12;

void InfraredService::init(port_map_t ports, uint16_t sendCore, uint16_t sendPriority, uint16_t learnCore,
                           uint16_t learnPriority, IrResponseCallback responseCallback) {
    if (m_eventgroup) {
        ESP_LOGE(irLog, "Already initialized");
        return;
    }

    ports_ = ports;
    m_responseCallback = responseCallback;

    m_queue = xQueueCreate(1, sizeof(struct IRSendMessage *));
    if (m_queue == nullptr) {
        ESP_LOGE(irLog, "xQueueCreate failed");
        return;
    }
    m_eventgroup = xEventGroupCreate();
    if (m_eventgroup == nullptr) {
        ESP_LOGE(irLog, "xEventGroupCreate failed");
        return;
    }
    if (sendCore > 1) {
        sendCore = 1;
    }
    if (learnCore > 1) {
        learnCore = 1;
    }

    xTaskCreatePinnedToCore(send_ir_f,  // task function
                            "IR send",  // task name
                            3072,  // stack size: random crashes with 2000! TODO(#30) test if 3072 or 3584 is enough!
                            this,  // task parameter
                            sendPriority,  // task priority
                            &m_ir_task,    // Task handle to keep track of created task
                            sendCore);     // core

    xTaskCreatePinnedToCore(learn_ir_f,     // task function
                            "IR learn",     // task name
                            3072,           // stack size
                            this,           // task parameter
                            learnPriority,  // task priority
                            &m_learn_task,  // Task handle to keep track of created task
                            learnCore);     // core

    ESP_LOGI(irLog, "Initialized: core=%d, priority=%d", xPortGetCoreID(), uxTaskPriorityGet(NULL));
}

void InfraredService::setIrSendPriority(uint16_t priority) {
    // there's an assert in vTaskPrioritySet!
    if (priority >= configMAX_PRIORITIES) {
        priority = configMAX_PRIORITIES - 1;
    }
    if (m_ir_task) {
        vTaskPrioritySet(m_ir_task, priority);
    }
}

void InfraredService::setIrLearnPriority(uint16_t priority) {
    if (priority >= configMAX_PRIORITIES) {
        priority = configMAX_PRIORITIES - 1;
    }
    if (m_learn_task) {
        vTaskPrioritySet(m_learn_task, priority);
    }
}

void InfraredService::startIrLearn() {
    // Note: UC_EVENT_IR_LEARNING_START event is sent when the learning loop starts
    if (m_eventgroup) {
        xEventGroupSetBits(m_eventgroup, IR_LEARNING_BIT);
    }
}

void InfraredService::stopIrLearn() {
    // Note: UC_EVENT_IR_LEARNING_STOP event is sent after the learning loop stops
    if (m_eventgroup) {
        xEventGroupClearBits(m_eventgroup, IR_LEARNING_BIT);
    }
}

bool InfraredService::isIrLearning() {
    if (!m_eventgroup) {
        return false;
    }
    return xEventGroupGetBits(m_eventgroup) & IR_LEARNING_BIT;
}

uint16_t InfraredService::sendGlobalCache(int16_t clientId, uint32_t msgId, const char *sendir, int socket) {
    // module is always 1 (emulating an iTach device)
    if (strncmp(sendir, "sendir,1:", 9) != 0) {
        return 2;  // invalid module address
    }

    // ID
    char *next = strchr(sendir + 9, ',');
    if (next == NULL) {
        return 4;  // invalid ID
    }
    auto port = atoi(sendir + 9);
    if (port < 1 || port > 15) {
        return 3;  // invalid port address
    }

    // frequency
    next = strchr(next + 1, ',');
    if (next == NULL) {
        return 5;  // invalid frequency
    }
    // repeat
    next = strchr(next + 1, ',');
    if (next == NULL) {
        return 6;  // invalid repeat
    }

    int16_t repeat = atoi(next + 1);
    if (repeat < 1 || repeat > 50) {
        return 6;  // invalid repeat
    }

    std::string code = sendir;
    std::string format = "gc";

    return send(clientId, msgId, code, format, repeat, port & 1, port & 8, port & 2, port & 4, socket);
}

uint16_t InfraredService::send(int16_t clientId, uint32_t msgId, const std::string &code, const std::string &format,
                               uint16_t repeat, bool internal_side, bool internal_top, bool external1, bool external2,
                               int gcSocket) {
    if (!m_queue || !m_eventgroup) {
        return 500;
    }

    if (isIrLearning()) {
        return 503;  // service unavailable
    }

    GpioPinMask pin_mask = createIrPinMask(internal_side, internal_top, external1, external2);
    if (pin_mask.w1ts == 0 && pin_mask.w1tc == 0) {
        ESP_LOGW(irLog, "No output specified");
        return 400;
    }

    IRFormat irFormat;
    if (format == "hex") {
        irFormat = IRFormat::UNFOLDED_CIRCLE;
    } else if (format == "pronto") {
        irFormat = IRFormat::PRONTO;
    } else if (format == "gc") {
        irFormat = IRFormat::GLOBAL_CACHE;
    } else {
        ESP_LOGW(irLog, "Invalid format: '%s'", format.c_str());
        return 400;
    }

    bool sending = uxQueueMessagesWaiting(m_queue) > 0;

    // #30 handle IR repeat if it's the same command. This is a very simple, initial implementation (ignore repeat val)
    if (sending && repeat > 0 && m_currentSendCode == code) {
        ESP_LOGI(irLog, "detected IR repeat for last IR send command (%d)", repeat);
        xEventGroupSetBits(m_eventgroup, IR_REPEAT_BIT);

        return 202;  // accepted IR repeat
    }

    // try to save an allocation if still sending an IR code
    if (sending) {
        return 429;  // too many requests
    }

    // new code, clear repeat flags
    xEventGroupClearBits(m_eventgroup, IR_REPEAT_BIT | IR_REPEAT_STOP_BIT);

    struct IRSendMessage *pxMessage = new IRSendMessage();
    pxMessage->clientId = clientId;
    pxMessage->msgId = msgId;
    pxMessage->format = irFormat;
    pxMessage->message = code;
    pxMessage->repeat = repeat;
    pxMessage->pin_mask = pin_mask;
    pxMessage->gcSocket = gcSocket;

    if (xQueueSendToBack(m_queue, reinterpret_cast<void *>(&pxMessage), 0) == errQUEUE_FULL) {
        // This should never happen with the pre-check!
        delete pxMessage;
        return 429;
    }

    ESP_LOGD(irLog, "queued IRSendMessage");

    m_currentSendCode = code;

    // 0 = asynchronous reply from the the IR send task
    return 0;
}

void InfraredService::stopSend() {
    if (!m_eventgroup) {
        return;
    }
    ESP_LOGI(irLog, "stopping IR repeat");
    xEventGroupSetBits(m_eventgroup, IR_REPEAT_STOP_BIT);
    xEventGroupClearBits(m_eventgroup, IR_REPEAT_BIT);  // shouldn't be required, better be save though
    // TODO(zehnm) what about turning off IR output? That would stop IR sending immediately!
}

GpioPinMask InfraredService::createIrPinMask(bool internal_side, bool internal_top, bool external1, bool external2) {
    GpioPinMask mask = {.w1ts_enable = 0, .w1ts = 0, .w1tc = 0};
    gpio_num_t  ext1_gpio_enable = GPIO_NUM_NC;
    gpio_num_t  ext2_gpio_enable = GPIO_NUM_NC;
    gpio_num_t  ext1_gpio_signal = GPIO_NUM_NC;
    gpio_num_t  ext2_gpio_signal = GPIO_NUM_NC;
    bool        ext1_gpio_inverted = false;
    bool        ext2_gpio_inverted = false;

    // make sure external ports are configured for IR sending
    if (external1) {
        auto ext_port = ports_.at(1);
        if (ext_port) {
            ext1_gpio_enable = ext_port->getIrEnableGpio();
            ext1_gpio_signal = ext_port->getIrGpio();
            ext1_gpio_inverted = ext_port->isIrGpioInverted();
        }
    }
#ifdef SWITCH_EXT_2
    if (external2) {
        auto ext_port = ports_.at(2);
        if (ext_port) {
            ext2_gpio_enable = ext_port->getIrEnableGpio();
            ext2_gpio_signal = ext_port->getIrGpio();
            ext2_gpio_inverted = ext_port->isIrGpioInverted();
        }
    }
#endif

    // default outputs if not specified
    if (!(internal_side || internal_top || ext1_gpio_signal != GPIO_NUM_NC || ext2_gpio_signal != GPIO_NUM_NC)) {
        ESP_LOGW(irLogSend, "No output active, using side output");
        internal_side = true;
    }

    if (internal_side) {
        if (IR_SEND_PIN_INT_SIDE_INVERTED) {
            mask.w1tc |= (1ULL << IR_SEND_PIN_INT_SIDE);
        } else {
            mask.w1ts |= (1ULL << IR_SEND_PIN_INT_SIDE);
        }
    }

#ifdef IR_SEND_PIN_INT_TOP
    if (internal_top) {
        if (IR_SEND_PIN_INT_TOP_INVERTED) {
            mask.w1tc |= (1ULL << IR_SEND_PIN_INT_TOP);
        } else {
            mask.w1ts |= (1ULL << IR_SEND_PIN_INT_TOP);
        }
    }
#endif

    if (ext1_gpio_signal != GPIO_NUM_NC) {
        if (ext1_gpio_enable != GPIO_NUM_NC) {
            mask.w1ts_enable |= (1ULL << ext1_gpio_enable);
        }
        if (ext1_gpio_inverted) {
            mask.w1tc |= (1ULL << ext1_gpio_signal);
        } else {
            mask.w1ts |= (1ULL << ext1_gpio_signal);
        }
    }

#ifdef SWITCH_EXT_2
    if (ext2_gpio_signal != GPIO_NUM_NC) {
        if (ext2_gpio_enable != GPIO_NUM_NC) {
            mask.w1ts_enable |= (1ULL << ext2_gpio_enable);
        }
        if (ext2_gpio_inverted) {
            mask.w1tc |= (1ULL << ext2_gpio_signal);
        } else {
            mask.w1ts |= (1ULL << ext2_gpio_signal);
        }
    }
#endif

    return mask;
}

void InfraredService::rebootIfMemError(int memError) {
    // Check we malloc'ed successfully.
    if (memError == 1) {  // malloc failed, so give up.
        ESP_LOGE(irLog, "FATAL: Can't allocate memory for an array for a new message! Forcing a reboot!");
        vTaskDelay(2000 / portTICK_PERIOD_MS);  // Enough time for messages to be sent.
        esp_restart();
        vTaskDelay(5000 / portTICK_PERIOD_MS);  // Enough time to ensure we don't return.
    }
}

void InfraredService::send_ir_f(void *param) {
    if (param == nullptr) {
        ESP_LOGE(irLogSend, "BUG: missing send_ir_f param");
        return;
    }

    InfraredService *ir = reinterpret_cast<InfraredService *>(param);
    if (ir->m_queue == nullptr) {
        ESP_LOGE(irLogSend, "terminated: input queue missing");
        return;
    }

    bool modulation = true;
    // used default output to initialize, active outputs are set with `setPinMask` before calling send
    uint64_t w1ts_mask = 1ULL << IR_SEND_PIN_INT_SIDE;
    IRsend   irsend = IRsend(modulation, w1ts_mask, 0);

    int8_t value = irsend.calibrate(38000);
    ESP_LOGI(irLogSend, "IR Calibration, calculated period offset: %dus", value);

    irsend.begin();

    ESP_LOGI(irLogSend, "initialized: core=%d, priority=%d", xPortGetCoreID(), uxTaskPriorityGet(NULL));

    struct IRSendMessage *pIrMsg;
    uint16_t              repeatLimit;
    int                   repeat;
    int                   repeatCount;
    EventGroupHandle_t    eventgroup = ir->m_eventgroup;

    // reference required to persist values during callbacks (also initialization is further down!)
    auto repeatCallback = [&repeatLimit, &repeat, &repeatCount, eventgroup]() -> bool {
        // commented out log statements: depending on IR format this is very time critical!
        // ESP_LOGI(irLogSend, "in callback!");

        // check if there's a command from the API
        auto bits = xEventGroupGetBits(eventgroup);
        if (bits & IR_REPEAT_STOP_BIT) {
            // abort immediately
            repeat = 0;
            ESP_LOGI(irLogSend, "stopping repeat");
        } else if (bits & IR_REPEAT_BIT) {
            // reset repeat count and start counting down again
            ESP_LOGI(irLogSend, "continue repeat: %d -> %d", repeat, repeatLimit);
            repeat = repeatLimit;
            xEventGroupClearBits(eventgroup, IR_REPEAT_BIT);
        }
        if (repeat > 0) {
            // repeat still active: count down
            // ESP_LOGI(irLogSend, "repeat callback #%d, remaining repeats: %d",
            //             ++repeatCount, repeat);
            repeat--;
            return true;
        }
        return false;
    };

    // start the IR sending task
    while (true) {
        // Peek a message on the created queue.
        if (xQueuePeek(ir->m_queue, &(pIrMsg), portMAX_DELAY) == pdFALSE) {
            // timeout
            continue;
        }
        // pIrMsg now points to the struct IRSendMessage variable, but the item still remains on the queue.
        // This blocks the sender from queuing more messages and notify the client with a "busy error".

        ESP_LOGI(irLogSend, "new command: id=%lu, format=%u, repeat=%u, mask_e=%llu, mask_s=%llu, mask_c=%llu",
                 pIrMsg->msgId, (uint8_t)pIrMsg->format, pIrMsg->repeat, pIrMsg->pin_mask.w1ts_enable,
                 pIrMsg->pin_mask.w1ts, pIrMsg->pin_mask.w1tc);

        // Activate continuous IR repeat
        if (pIrMsg->repeat > 0) {
            // set lambda reference variables
            repeatLimit = pIrMsg->repeat;
            repeat = pIrMsg->repeat;
            repeatCount = 0;
            irsend.setRepeatCallback(repeatCallback);
        } else {
            irsend.setRepeatCallback(nullptr);
        }

        // enable GPIOs for external IR peripherals if required
        if (pIrMsg->pin_mask.w1ts_enable) {
            GPIO.out_w1ts = static_cast<int32_t>(pIrMsg->pin_mask.w1ts_enable);
            GPIO.out1_w1ts.val = static_cast<int32_t>(pIrMsg->pin_mask.w1ts_enable >> 32);
            usleep(20);
        }

        // set active outputs for parallel IR sending
        if (!irsend.setPinMask(pIrMsg->pin_mask.w1ts, pIrMsg->pin_mask.w1tc)) {
            ESP_LOGE(irLogSend, "failed to set PinMask");
        }

        bool success = false;
        switch (pIrMsg->format) {
            case IRFormat::UNFOLDED_CIRCLE: {
                IRHexData data;
                if (buildIRHexData(pIrMsg->message, &data)) {
                    // Override repeat in code
                    // Note: if only `data.repeat > 1`: some codes have to be sent twice for a single command,
                    // i.e. it's not a repeat indicator yet!
                    if (pIrMsg->repeat > 0) {
                        data.repeat = pIrMsg->repeat;
                    }
                    success = irsend.send(data.protocol, data.command, data.bits, data.repeat);
                } else {
                    ESP_LOGW(irLogSend, "failed to parse UC code");
                }
                break;
            }
            case IRFormat::PRONTO: {
                // use space as default separator
                char separator = ' ';
                if (pIrMsg->message.find_first_of(separator) == std::string::npos) {
                    // fallback to old comma (dock version <= 0.6.0)
                    separator = ',';
                }

                // operate directly on the underlaying message buffer: avoid std::string allocations from using
                // "message.substring"!
                auto      msg = pIrMsg->message.c_str();
                uint16_t  count;
                int       memError;
                uint16_t *code_array = prontoBufferToArray(msg, separator, &count, &memError);
                if (!(code_array == NULL || count == 0)) {
                    // Attention: PRONTO codes don't have an embedded repeat count field, some codes might required
                    // to be sent twice to be recognized correctly! One could argue it's an invalid code...
                    // We ignore that here and treat every code the same in regards to the repeat field!
                    success = irsend.sendPronto(code_array, count, pIrMsg->repeat);
                    free(code_array);
                } else {
                    ESP_LOGW(irLogSend, "failed to parse PRONTO code");
                    rebootIfMemError(memError);
                }
                break;
            }
            case IRFormat::GLOBAL_CACHE: {
                uint16_t  count;
                int       memError;
                uint16_t *code_array = globalCacheBufferToArray(pIrMsg->message.c_str(), &count, &memError);
                if (!(code_array == NULL || count == 0)) {
                    // Override repeat in code
                    if (pIrMsg->repeat > 0) {
                        code_array[1] = pIrMsg->repeat;
                    }
                    irsend.sendGC(code_array, count);
                    success = true;
                    free(code_array);
                } else {
                    ESP_LOGW(irLogSend, "failed to parse GC code");
                    rebootIfMemError(memError);
                }
                break;
            }
            default:
                ESP_LOGE(irLogSend, "Invalid IR format");
        }

        irsend.setRepeatCallback(nullptr);

        // #70 quick & dirty hack from UCD2 (rewrite with callback function or a dedicated queue)
        if (pIrMsg->clientId == IR_CLIENT_GC && pIrMsg->gcSocket > 0) {
            char    response[24];
            uint8_t module = 1;
            uint8_t port = 1;
            GCMsg   req;
            if (parseGcRequest(pIrMsg->message.c_str(), &req) == 0) {
                module = req.module;
                port = req.port;
            }
            snprintf(response, sizeof(response), "completeir,%u:%u,%lu\r", module, port, pIrMsg->msgId);
            send_string_to_socket(pIrMsg->gcSocket, response);
        } else {
            cJSON *responseDoc = cJSON_CreateObject();
            cJSON_AddStringToObject(responseDoc, "type", "dock");
            cJSON_AddStringToObject(responseDoc, "msg", "ir_send");
            cJSON_AddNumberToObject(responseDoc, "req_id", pIrMsg->msgId);
            cJSON_AddNumberToObject(responseDoc, "code", success ? 200 : 400);

            struct IrResponse *response = new IrResponse();
            response->clientId = pIrMsg->clientId;
            char *resp = cJSON_PrintUnformatted(responseDoc);
            response->message = resp;
            delete (resp);
            cJSON_Delete(responseDoc);

            if (ir->m_responseCallback) {
                ir->m_responseCallback(response);
            } else {
                delete response;
            }
        }

        // disable GPIOs for external IR-emitters if required
        if (pIrMsg->pin_mask.w1ts_enable) {
            GPIO.out_w1tc = static_cast<int32_t>(pIrMsg->pin_mask.w1ts_enable);
            GPIO.out1_w1tc.val = static_cast<int32_t>(pIrMsg->pin_mask.w1ts_enable >> 32);
        }

        // all done, release queue (reset works because of queue length 1)
        delete pIrMsg;
        xQueueReset(ir->m_queue);
    }
}

void InfraredService::learn_ir_f(void *param) {
    if (param == nullptr) {
        ESP_LOGE(irLogLearn, "BUG: missing learn_ir_f param");
        return;
    }

    InfraredService *ir = reinterpret_cast<InfraredService *>(param);
    if (ir->m_eventgroup == nullptr) {
        ESP_LOGE(irLogLearn, "terminated: input queue missing");
        return;
    }

    // Use turn on the save buffer feature for more complete capture coverage.
    IRrecv irrecv = IRrecv(IR_RECEIVE_PIN, kCaptureBufferSize, kTimeout, true);

    // Ignore messages with less than minimum on or off pulses.
    irrecv.setUnknownThreshold(kMinUnknownSize);

    ESP_LOGI(irLogLearn, "initialized: core=%d, priority=%d", xPortGetCoreID(), uxTaskPriorityGet(NULL));

    EventBits_t    bits;
    decode_results results;
    // start the IR learning task
    while (true) {
        // wait until learning is requested
        bits = xEventGroupWaitBits(ir->m_eventgroup, IR_LEARNING_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
        if ((bits & portMAX_DELAY) == 0) {
            // timeout
            continue;
        }

        ESP_LOGI(irLogLearn, "ir_learn task starting");
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            esp_event_post(UC_DOCK_EVENTS, UC_EVENT_IR_LEARNING_START, NULL, 0, pdMS_TO_TICKS(500)));

        // enable IR learning
        irrecv.enableIRIn();
        // Clear buffers to make sure no old data is returned to client
        // Note: I'm not 100% sure if this is really required, but shouldn't hurt either :-)
        //       I couldn't find where the 2nd `params_save` buffer is cleared in enableIRIn().
        irrecv.decode(&results);

        // start learning loop
        while (xEventGroupGetBits(ir->m_eventgroup) & IR_LEARNING_BIT) {
            // Delay value by experimentation with Sony, Denon, LG, RC6 remotes.
            // If too high, there are more "double learned" code failures with Denon, if too low a core gets hogged.
            vTaskDelay(pdMS_TO_TICKS(20));

            if (!irrecv.decode(&results)) {
                continue;
            }

            bool          failed = false;
            uc_event_ir_t event_ir;
            memset(&event_ir, 0, sizeof(event_ir));
            // make sure to only report successfully decoded IR codes
            if (results.overflow) {
                ESP_LOGW(irLogLearn, "IR code is too big for buffer (>= %d)", kCaptureBufferSize);
                failed = true;
                event_ir.error = UC_ERROR_IR_LEARN_OVERFLOW;
            } else if (results.decode_type == decode_type_t::UNKNOWN) {
                ESP_LOGW(irLogLearn, "Learning failed: unknown code");
                failed = true;
                event_ir.error = UC_ERROR_IR_LEARN_UNKNOWN;
            } else if (results.value == 0 || results.value == UINT64_MAX) {
                ESP_LOGW(irLogLearn, "Learning failed: invalid value");
                failed = true;
                event_ir.error = UC_ERROR_IR_LEARN_INVALID;
            }

            if (failed) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, UC_EVENT_IR_LEARNING_FAIL, &event_ir,
                                                             sizeof(event_ir), pdMS_TO_TICKS(500)));
                continue;
            }

            std::string code;
            code += std::to_string(results.decode_type);
            code += ";";
            code += resultToHexidecimal(&results);
            code += ";";
            code += std::to_string(results.bits);
            code += ";";
            // TODO(#30) adjust repeat count for known protocols, e.g. set Sony to 2?
            code += std::to_string(results.repeat);

            // code += ";";
            // code += std::to_string(results.address);
            // code += ";";
            // code += std::to_string(results.command);

            // TODO(#32) here we could add "protocol specific quirk handling":
            //           e.g. filter out double Denon codes (within 200ms) and report repeat count 2 instead

            ESP_LOGI(irLogLearn, "Learned: %s", code.c_str());
            event_ir.decode_type = results.decode_type;
            event_ir.value = results.value;
            event_ir.address = results.address;
            event_ir.command = results.command;
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, UC_EVENT_IR_LEARNING_OK, &event_ir,
                                                         sizeof(event_ir), pdMS_TO_TICKS(500)));

            cJSON *responseDoc = cJSON_CreateObject();
            cJSON_AddStringToObject(responseDoc, "type", "event");
            cJSON_AddStringToObject(responseDoc, "msg", "ir_receive");
            cJSON_AddStringToObject(responseDoc, "ir_code", code.c_str());

            struct IrResponse *response = new IrResponse();
            response->clientId = -1;  // broadcast
            char *resp = cJSON_PrintUnformatted(responseDoc);
            response->message = resp;
            delete (resp);
            cJSON_Delete(responseDoc);

            if (ir->m_responseCallback) {
                ir->m_responseCallback(response);
            } else {
                delete response;
            }
        }

        ESP_LOGI(irLogLearn, "ir_learn task stopping");
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            esp_event_post(UC_DOCK_EVENTS, UC_EVENT_IR_LEARNING_STOP, NULL, 0, pdMS_TO_TICKS(500)));

        // learning turned off: disable processing
        irrecv.disableIRIn();
    }
}

InfraredService &InfraredService::getInstance() {
    static InfraredService instance;
    return instance;
}

InfraredService &irService = irService.getInstance();
