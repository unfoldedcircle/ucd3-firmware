// SPDX-FileCopyrightText: Copyright (c) 2022 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdlib.h>

class Efuse {
 public:
    const char *getSerial() const;
    const char *getModel() const;
    const char *getHwRevision() const;

    bool hasChargingFeature() const;

    static Efuse &instance();

 private:
    Efuse();
    ~Efuse() {}

    Efuse(const Efuse &) = delete;  // no copying
    Efuse &operator=(const Efuse &) = delete;

    const char *m_ctx = "EFUSE";
};
