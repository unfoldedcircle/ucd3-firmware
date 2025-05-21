// SPDX-FileCopyrightText: Copyright (c) 2022 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "board.h"
#include "external_port.h"
#include "ir_codes.h"

#define IR_CLIENT_GC -2

struct IrResponse {
    int16_t     clientId;
    std::string message;
};

typedef std::function<esp_err_t(IrResponse *response)> IrResponseCallback;

class InfraredService {
 public:
    static InfraredService &getInstance();

    /**
     * Initialize and start infrared processing.
     *
     * This must be called **once** at startup before using `send` or `startIrLearn`.
     */
    void init(port_map_t ports, uint16_t sendCore, uint16_t sendPriority, uint16_t learnCore, uint16_t learnPriority,
              IrResponseCallback responseCallback);

    void setIrSendPriority(uint16_t priority);
    void setIrLearnPriority(uint16_t priority);

    uint16_t sendGlobalCache(int16_t clientId, uint32_t msgId, const char *sendir, int socket = 0);

    /**
     * Asynchronously send an IR code on the 2nd core.
     *
     * If there's still an IR code being sent, error 429 (too many requests) is returned.
     *
     * @param clientId the WebSocket client identifier to associate the response message.
     * @param msgId the client send request message identifier to associate the response message with.
     * @param code  IR code to send, either PRONTO or HEX (UnfoldedCircle) format.
     * @param format IR code format: "pronto" or "hex".
     * @param repeat IR repeat count.
     * @param internal_side Send IR signal on internal LEDs.
     * @param internal_top Send IR signal on internal top LED.
     * @param external1 Send IR signal on external 1 emitter port.
     * @param external2 Send IR signal on external 2 emitter port.
     * @param gcSocket Optional TCP socket if message was received from the GlobalCache TCP server.
     */
    uint16_t send(int16_t clientId, uint32_t msgId, const std::string &code, const std::string &format, uint16_t repeat,
                  bool internal_side, bool internal_top, bool external1, bool external2, int gcSocket = 0);

    void stopSend();

    void startIrLearn();
    void stopIrLearn();
    bool isIrLearning();

 private:
    InfraredService() = default;

    InfraredService(const InfraredService &) = delete;  // no copying
    InfraredService &operator=(const InfraredService &) = delete;

    GpioPinMask createIrPinMask(bool internal_side, bool internal_top, bool external1, bool external2);

    static void rebootIfMemError(int memError);

    // IR sending task
    static void send_ir_f(void *param);

    // IR learning task
    static void learn_ir_f(void *param);

    // Used to start and stop IR learning
    EventGroupHandle_t m_eventgroup = nullptr;
    // IR sending task handle for `send_ir_f`
    TaskHandle_t m_ir_task = nullptr;
    // IR learning task handle for `learn_ir_f`
    TaskHandle_t m_learn_task = nullptr;
    // IR send input queue
    QueueHandle_t m_queue = nullptr;

    // Current IR code which is being sent. Used to check for IR repeat commands.
    std::string m_currentSendCode;

    port_map_t ports_;

    IrResponseCallback m_responseCallback;
};

extern InfraredService &irService;
