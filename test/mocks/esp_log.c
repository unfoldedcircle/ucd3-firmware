#include "esp_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static char buffer[1024];

static inline char level_to_char(int level) {
    switch (level) {
        case ESP_LOG_ERROR:
            return 'E';
        case ESP_LOG_WARN:
            return 'W';
        case ESP_LOG_INFO:
            return 'I';
        case ESP_LOG_DEBUG:
            return 'D';
        case ESP_LOG_VERBOSE:
            return 'V';
        default:
            return '?';
    }
}

void test_log(int level, const char* format, ...) {
    va_list argptr;
    va_start(argptr, format);
    vsnprintf(buffer, sizeof(buffer), format, argptr);

    fprintf(stdout, "%c %s\n", level_to_char(level), buffer);
    va_end(argptr);
    fflush(stdout);
}
