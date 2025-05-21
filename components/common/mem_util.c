/*
 * Memory utils from
 * https://github.com/sle118/squeezelite-esp32/blob/SqueezeAmp.32.1681.master-v4.3/components/tools/tools.h
 * Original header and licence:
 *
 *  (c) Philippe G. 20201, philippe_44@outlook.com
 *	see other copyrights below
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */

#include "mem_util.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *const TAG = "MEM";

void *malloc_init_external(size_t sz) {
    void *ptr = NULL;
    ptr = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ESP_LOGE(TAG, "malloc_init_external:  unable to allocate %d bytes of PSRAM!", sz);
    } else {
        memset(ptr, 0x00, sz);
    }
    return ptr;
}

void *clone_to_psram(void *source, size_t size) {
    void *dest = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (dest == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes of PSRAM!", size);
    } else {
        memcpy(dest, source, size);
    }
    return dest;
}

char *strdup_to_psram(const char *source) {
    size_t size = strlen(source) + 1;
    char  *dest = (char *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (dest == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes of PSRAM! Cannot clone string %s", size, source);
    } else {
        memset(dest, 0, size);
        strcpy(dest, source);
    }
    return dest;
}
