// SPDX-FileCopyrightText: Copyright (c) 2022 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "service_ir.h"

#include <IRac.h>
#include <IRtimer.h>
#include <IRutils.h>
#include <sys/param.h>  // For MIN/MAX(a, b)

#include <algorithm>

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
#include "raw_timings.h"
#include "sdkconfig.h"
#include "uc_events.h"
#include "util_types.h"

const char *irLog = "IR";
const char *irLogSend = "IRSEND";
const char *irLogLearn = "IRLEARN";

/// Learning is active
const int IR_LEARNING_BIT = BIT0;
/// Return raw timings for IR learning (requires IR_LEARNING_BIT)
const int IR_LEARNING_RAW_BIT = BIT1;
/// Repeat active IR command
const int IR_REPEAT_BIT = BIT2;
const int IR_REPEAT_STOP_BIT = BIT3;

/// Limit maximum repeat count in continuous IR repeat mode to 20.
const uint32_t MAX_REPEAT = 20;

// Only enable log statements in IR repeat callback function for development!
// Depending on IR format the callback is very time critical and log statements can interfere with timing!
const bool DEVELOPMENT_LOG = false;

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
    m_callbackOverride = nullptr;

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

void InfraredService::startIrLearn(IRFormat irFormat) {
    // Note: UC_EVENT_IR_LEARNING_START event is sent when the learning loop starts
    m_callbackOverride = nullptr;
    if (m_eventgroup) {
        EventBits_t uxBitsToSet = IR_LEARNING_BIT;
        if (irFormat == IRFormat::RAW) {
            uxBitsToSet |= IR_LEARNING_RAW_BIT;
        }

        xEventGroupSetBits(m_eventgroup, uxBitsToSet);
    }
}

void InfraredService::startIrLearn(IrRawResponseCallback callbackOverride) {
    if (m_eventgroup) {
        m_callbackOverride = callbackOverride;
        xEventGroupSetBits(m_eventgroup, IR_LEARNING_BIT | IR_LEARNING_RAW_BIT);
    }
}

void InfraredService::stopIrLearn() {
    // Note: UC_EVENT_IR_LEARNING_STOP event is sent after the learning loop stops
    if (m_eventgroup) {
        xEventGroupClearBits(m_eventgroup, IR_LEARNING_BIT | IR_LEARNING_RAW_BIT);
    }
    m_callbackOverride = nullptr;
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

    return send(clientId, msgId, code, format, repeat, 0, port & 1, port & 8, port & 2, port & 4, socket);
}

uint16_t InfraredService::send(int16_t clientId, uint32_t msgId, const std::string &code, const std::string &format,
                               uint16_t repeat, uint16_t hold, bool internal_side, bool internal_top, bool external1,
                               bool external2, int gcSocket) {
    if (!m_queue || !m_eventgroup) {
        return 500;
    }

    if (isIrLearning()) {
        return 503;  // service unavailable
    }

    IRFormat irFormat;
    if (format == "hex") {
        irFormat = IRFormat::UNFOLDED_CIRCLE;
    } else if (format == "pronto") {
        irFormat = IRFormat::PRONTO;
    } else if (format == "gc") {
        irFormat = IRFormat::GLOBAL_CACHE;
    } else if (format == "raw") {
        irFormat = IRFormat::RAW;
    } else {
        ESP_LOGW(irLog, "Invalid format: '%s'", format.c_str());
        return 400;
    }

    // Safety cap on the client-requested repeat count. The `repeat` field is a uint16_t and is not
    // range-checked on the WebSocket API, so without this a client could request a very large repeat
    // value and trigger a runaway transmission (e.g. sending "volume up" dozens of times from a single
    // request). Press-and-hold is unaffected: a client holds a button by repeatedly extending the
    // active repeat with the same command (see the matching-extend branch below), not by requesting a
    // large initial repeat count.
    if (repeat > MAX_REPEAT) {
        ESP_LOGW(irLog, "repeat count %u exceeds limit %lu, capping", repeat, MAX_REPEAT);
    }
    repeat = static_cast<uint16_t>(std::min<uint32_t>(repeat, MAX_REPEAT));

    bool sending = uxQueueMessagesWaiting(m_queue) > 0;

    // #30 A request for the currently-active code extends its repeat/hold ("press-and-hold"): while the
    // button is held the client keeps re-sending the same command. Re-arm the repeat so the send task
    // starts another countdown on its next repeat-callback tick.
    // Notes:
    // - the repeat value itself is ignored on an extend.
    // - a genuine stop (IR_REPEAT_STOP_BIT, set only by ir_stop or by the owning client
    //   disconnecting) is intentionally NOT cleared here. A stop always takes priority in the send-task
    //   callback, which is the safe default: if the owner asked to stop, the repeat must stop.
    if (sending && repeat > 0 && m_currentSendCode == code) {
        ESP_LOGI(irLog, "repeat req %lu", msgId);
        xEventGroupSetBits(m_eventgroup, IR_REPEAT_BIT);
        return 202;  // extended IR repeat sequence
    }

    // A request arrived while a different transmission is still active (different code, or repeat == 0).
    // Only one IR transmission may be active at a time, so this request is rejected with 429. The active
    // send is deliberately left running: a rejected request from one client must never cancel another
    // client's in-progress repeat/hold. The active send ends on its own terms — when its owner stops
    // extending it (the repeat counts down), on an explicit ir_stop, or when the owning client
    // disconnects (handled in the WebSocket layer). The caller may retry shortly after.
    if (sending) {
        return 429;  // too many requests
    }

    GpioPinMask pin_mask = createIrPinMask(internal_side, internal_top, external1, external2);
    if (pin_mask.w1ts == 0 && pin_mask.w1tc == 0) {
        ESP_LOGW(irLog, "No output specified");
        return 400;
    }

    // new code, clear repeat flags
    xEventGroupClearBits(m_eventgroup, IR_REPEAT_BIT | IR_REPEAT_STOP_BIT);

    struct IRSendMessage *pxMessage = new IRSendMessage();
    pxMessage->clientId = clientId;
    pxMessage->msgId = msgId;
    pxMessage->format = irFormat;
    pxMessage->message = code;
    pxMessage->repeat = repeat;
    pxMessage->hold = hold;
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
        ESP_LOGW(irLogSend, "No output active, using internal outputs");
        internal_side = true;
        internal_top = true;
    }

    if (internal_side) {
        if (IR_SEND_PIN_INT_SIDE_INVERTED) {
            mask.w1tc |= (1ULL << IR_SEND_PIN_INT_SIDE);
        } else {
            mask.w1ts |= (1ULL << IR_SEND_PIN_INT_SIDE);
        }
    }

    if (internal_top) {
        gpio_num_t ir_send_pin_int_top = board_get_ir_send_pin_int_top();
        if (ir_send_pin_int_top != GPIO_NUM_NC) {
            if (IR_SEND_PIN_INT_TOP_INVERTED) {
                mask.w1tc |= (1ULL << ir_send_pin_int_top);
            } else {
                mask.w1ts |= (1ULL << ir_send_pin_int_top);
            }
        }
    }

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
    auto   default_mask = ir->createIrPinMask(true, true, false, false);
    IRsend irsend = IRsend(modulation, default_mask.w1ts, default_mask.w1tc);

    int8_t value = irsend.calibrate(38000);
    ESP_LOGI(irLogSend, "IR Calibration, calculated period offset: %dus", value);

    irsend.begin();

    ESP_LOGI(irLogSend, "initialized: core=%d, priority=%d", xPortGetCoreID(), uxTaskPriorityGet(NULL));

    struct IRSendMessage *pIrMsg;
    uint16_t              repeatLimit;
    int                   repeat;
    int                   repeatCount;
    TimerMs               startSendTimer;
    uint32_t              holdTimeLimit;
    EventGroupHandle_t    eventgroup = ir->m_eventgroup;

    // reference required to persist values during callbacks (also initialization is further down!)
    auto repeatCallback = [&repeatLimit, &repeat, &repeatCount, &startSendTimer, &holdTimeLimit, eventgroup]() -> bool {
        if (DEVELOPMENT_LOG) {
            ESP_LOGD(irLogSend, "in callback! hold: %lums, repeat: %d, repeatLimit: %d", holdTimeLimit, repeat,
                     repeatLimit);
        }

        // check if there's a command from the API
        auto bits = xEventGroupGetBits(eventgroup);
        if (bits & IR_REPEAT_STOP_BIT) {
            // abort immediately
            repeat = 0;
            if (holdTimeLimit > 0) {
                holdTimeLimit = 1;
            }
            ESP_LOGI(irLogSend, "stopping repeat");
        } else if (bits & IR_REPEAT_BIT) {
            // reset repeat count and start counting down again
            if (DEVELOPMENT_LOG) {
                ESP_LOGI(irLogSend, "continue repeat: %d -> %d", repeat, repeatLimit);
            }
            repeat = repeatLimit;
            startSendTimer.reset();
            xEventGroupClearBits(eventgroup, IR_REPEAT_BIT);
        }

        if (holdTimeLimit > 0) {
            uint32_t elapsed = startSendTimer.elapsed();
            if (elapsed < holdTimeLimit) {
                // hold time still active
                if (DEVELOPMENT_LOG) {
                    ESP_LOGI(irLogSend, "hold time: %lu/%lums", elapsed, holdTimeLimit);
                }
                return true;
            } else {
                ESP_LOGI(irLogSend, "stopping hold: %lums", elapsed);
                return false;
            }
        }

        if (repeat > 0) {
            // repeat still active: count down
            if (DEVELOPMENT_LOG) {
                ESP_LOGI(irLogSend, "repeat callback #%d, remaining repeats: %d", ++repeatCount, repeat);
            }
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

        ESP_LOGI(irLogSend, "new command: id=%lu, format=%u, repeat=%u, hold=%u, mask_e=%llu, mask_s=%llu, mask_c=%llu",
                 pIrMsg->msgId, (uint8_t)pIrMsg->format, pIrMsg->repeat, pIrMsg->hold, pIrMsg->pin_mask.w1ts_enable,
                 pIrMsg->pin_mask.w1ts, pIrMsg->pin_mask.w1tc);

        if (pIrMsg->hold > 0 && pIrMsg->repeat == 0) {
            // repeat needs to be enabled for hold time to work, set default repeat count to 1 if not set by client
            pIrMsg->repeat = 1;
        }
        // Activate continuous IR repeat
        if (pIrMsg->repeat > 0) {
            // set lambda reference variables
            repeatLimit = pIrMsg->repeat;
            repeat = pIrMsg->repeat;
            repeatCount = 0;
            holdTimeLimit = pIrMsg->hold;
            irsend.setRepeatCallback(repeatCallback);
        } else {
            holdTimeLimit = 0;
            irsend.setRepeatCallback(nullptr);
        }
        startSendTimer.reset();

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
                    // Override repeat value in IR code.
                    // Note: "press-and-hold" repeat indicator is the separate `repeat` parameter in IRSendMessage.
                    // The parsed `data.repeat` value from the IR code is protocol specific and used for individual
                    // commands.
                    if (pIrMsg->repeat > 0) {
                        data.repeat = pIrMsg->repeat;
                        // Adapt repeat count for protocols requiring a minimal repeat count (Epson, Sony, etc.).
                        // E.g. Sony 40k requires 2 repeats for a single command, each msg 45ms apart, total of 3 msgs.
                        // Handling repeat gets tricky now: increase repeat count, multiply the min repeat count,
                        // or multiply the total count?
                        // Answer is likely "it depends" on the IR protocol.
                        // While testing, a Sony TV required 18 IR messages to increase the volume by 2 steps.
                        // Afterwards, it increases much faster.
                        // Repeat count logic (which might need further IR protocol specific adaption):
                        // - only multiply the min repeat count.
                        // - cap maximum repeat count at 20.
                        // Example: repeat value of 3 with min repeat of 2: 3 * 2 = 6 repeat messages (+ 1 initial).
                        // Repeat transmission length: 6 * 45ms = 270ms.
                        // This allows to extend the repeat signal with a new WS request message every ~200ms.
                        uint16_t min_repeat = IRsend::minRepeats(data.protocol);
                        if (min_repeat > 1) {
                            data.repeat = std::min(static_cast<uint32_t>(data.repeat * min_repeat), MAX_REPEAT);
                            repeatLimit = data.repeat;
                            ESP_LOGD(irLogSend, "repeat: req=%u, min=%d, ir=%d", pIrMsg->repeat, min_repeat,
                                     data.repeat);
                        }
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
                uint16_t  count = 0;
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
            case IRFormat::RAW: {
                RawParseResult parsed = parse_raw_timings(pIrMsg->message.c_str(), pIrMsg->message.size());
                if (parsed.error == RawParseError::OK) {
                    irsend.sendRaw(parsed.buf, parsed.len, parsed.hz, pIrMsg->repeat);
                    free(parsed.buf);
                    success = true;
                } else {
                    ESP_LOGE(irLogSend, "Parse error %d at token %u", (int)parsed.error, parsed.error_token_index);
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
            cJSON_free(resp);
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

/// Optimized RAW serialization using string formatting instead of inefficient cJSON array handling.
/// A std::string is returned to avoid another memory allocation passing the message to the web server.
static std::string serialize_raw_optimized(const decode_results *results, const char *code) {
    uint16_t count = results->rawlen - 1;

    // A conservative estimate would be: 8 bytes per entry (sign + 5 digits + comma/bracket).
    // However: most IR timings use much shorter Mark & Space sequences, that's why we use 6 bytes per entry.
    // Plus envelope (~128 chars + code length).
    size_t      est_size = count * 6 + strlen(code) + 128;
    std::string buf;
    buf.reserve(est_size);

    buf.append("{\"type\":\"event\",\"msg\":\"ir_receive\",\"format\":\"hex\",\"ir_code\":\"");
    buf.append(code);
    buf.append("\",\"overflow\":");
    buf.append(results->overflow ? "true" : "false");
    buf.append(",\"raw\":[");

    for (uint16_t i = 0; i < count; i++) {
        uint16_t src_idx = i + 1;  // skip index 0
        int32_t  us = static_cast<int32_t>(results->rawbuf[src_idx]) * kRawTick;

        char timing[16];
        snprintf(timing, sizeof(timing), i < count - 1 ? "%ld," : "%ld", (src_idx % 2 == 1) ? us : -us);
        buf.append(timing);
    }

    buf.append("]}");
    return buf;
}

/// Serialize raw IR timings into an IrRawResponse struct.
/// Parameters:
///   results - decoded IR results from IRrecv
///
/// Returns heap-allocated IrRawResponse with frequency (default 38000 Hz) and timing values in microseconds.
/// Caller must free().
/// Returns NULL on invalid input.
static IrRawResponse *serialize_raw_response(const decode_results *results) {
    if (results->rawlen == 0) {
        return NULL;
    }

    IrRawResponse *response = new IrRawResponse();
    // Default carrier frequency. Not possible to get modulation frequency with IRrecv and a TSOP‑style demodulator.
    // TODO use IR_RECEIVE_ANALOG and custom logic for raw learning.
    response->frequency = frequencyFromProtocol(results->decode_type, results->bits);
    response->timings_len = getCorrectedRawLength(results);
    response->timings_us = resultToRawArray(results);

    return response;
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

        bool raw_mode = bits & IR_LEARNING_RAW_BIT;

        ESP_LOGI(irLogLearn, "ir_learn task starting");
        uc_event_ir_start_t ir_start;
        ir_start.irFormat = static_cast<int8_t>(raw_mode ? IRFormat::RAW : IRFormat::UNFOLDED_CIRCLE);
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, UC_EVENT_IR_LEARNING_START, &ir_start,
                                                     sizeof(ir_start), pdMS_TO_TICKS(500)));

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

            std::string   code;
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
                if (!raw_mode) {
                    continue;
                }
                // empty code indicates a failed learned code in raw mode
                code = "";
            } else {
                code += std::to_string(results.decode_type);
                code += ";";
                code += resultToHexidecimal(&results);
                code += ";";
                code += std::to_string(results.bits);
                code += ";";
                // TODO(#30) adjust repeat count for known protocols, e.g. set Sony to 2?
                code += std::to_string(results.repeat);

                // TODO(#32) here we could add "protocol specific quirk handling":
                //           e.g. filter out double Denon codes (within 200ms) and report repeat count 2 instead

                ESP_LOGI(irLogLearn, "Learned: %s", code.c_str());
                event_ir.decode_type = results.decode_type;
                event_ir.value = results.value;
                event_ir.address = results.address;
                event_ir.command = results.command;
                ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, UC_EVENT_IR_LEARNING_OK, &event_ir,
                                                             sizeof(event_ir), pdMS_TO_TICKS(500)));
            }

            if (ir->m_callbackOverride) {
                struct IrRawResponse *response = serialize_raw_response(&results);
                if (response) {
                    ir->m_callbackOverride(response);
                }

                continue;
            }

            struct IrResponse *response = new IrResponse();
            response->clientId = -1;  // broadcast

            if (raw_mode) {
                response->message = serialize_raw_optimized(&results, code.c_str());
            } else {
                cJSON *responseDoc = cJSON_CreateObject();
                cJSON_AddStringToObject(responseDoc, "type", "event");
                cJSON_AddStringToObject(responseDoc, "msg", "ir_receive");
                cJSON_AddStringToObject(responseDoc, "format", "hex");
                cJSON_AddStringToObject(responseDoc, "ir_code", code.c_str());

                char *resp = cJSON_PrintUnformatted(responseDoc);
                response->message = resp;
                cJSON_free(resp);
                cJSON_Delete(responseDoc);
            }

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

InfraredService &irService = InfraredService::getInstance();
