// SPDX-FileCopyrightText: Copyright (c) 2025 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdlib.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Start system statistic gathering task
/// @param console Print real time stats periodically in console
void start_stats_task(bool console = true);

/// @brief Get current realtime task statistics
/// @param responseDoc JSON node to add the task statistics
void get_current_stats(cJSON *responseDoc);

#ifdef __cplusplus
}
#endif
