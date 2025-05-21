// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include "DisplayDriver.h"
#include "uc_events.h"

class EventParameter {
 public:
    EventParameter() {};
    EventParameter(ui_event_queue_message* event);

    ui_icon_t   icon() { return icon_; };
    std::string title() { return title_; };
    std::string message() { return message_; };
    int32_t     value() { return value_; };
    bool        isFatalError() { return fatal_error_; };

 private:
    ui_icon_t   icon_;
    std::string title_;
    std::string message_;
    int32_t     value_;
    bool        fatal_error_;
};
