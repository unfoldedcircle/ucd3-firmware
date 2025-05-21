/*
 * Memory utils from
 * https://github.com/sle118/squeezelite-esp32/blob/SqueezeAmp.32.1681.master-v4.3/components/tools/tools.h
 * Original header and licence:
 *
 *      Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */

#pragma once

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FREE_AND_NULL
#define FREE_AND_NULL(x) \
    if (x) {             \
        free(x);         \
        x = NULL;        \
    }
#endif

void* malloc_init_external(size_t sz);

void* clone_to_psram(void* source, size_t size);

char* strdup_to_psram(const char* source);

#ifdef __cplusplus
}
#endif