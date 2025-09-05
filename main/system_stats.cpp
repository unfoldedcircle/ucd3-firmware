// SPDX-FileCopyrightText: Copyright (c) 2025 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Adapted from:
// https://github.com/espressif/esp-idf/blob/v5.4.1/examples/system/freertos/real_time_stats/main/real_time_stats_example_main.c

#include "system_stats.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <cassert>
#include <vector>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "sdkconfig.h"

#define STATS_TASK_PRIO 6
#define STATS_INTERVAL_MS 2000
#define ARRAY_SIZE_OFFSET 3

static const char *const TAG = "STAT";

// runtime statistics require multiple configuration flags to enable tracing features
#if defined(CONFIG_FREERTOS_USE_TRACE_FACILITY) && defined(CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS) && \
    defined(CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)
struct TaskStatistic {
    char     name[17];
    uint8_t  priority;
    uint32_t runtimeCounter;
    // Fixed-point number with one decimal place and 100% scaling per core
    uint16_t cpu;
    uint16_t stackHighWaterMark;
#ifdef CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
    int8_t coreId;
#endif
    uint8_t state;
};

// Task statistics from the last interval
static std::vector<TaskStatistic> currentTaskList;
static uint32_t                   currentElapsedTime;
// Semaphore for accessing `currentTaskList`, `currentElapsedTime` from the logging task & WS
static SemaphoreHandle_t currentTaskSem = NULL;
// Enable realtime stats logging in console
static bool printConsole;

// Print real time stats periodically in console
static void stats_task(void *arg);

void start_stats_task(bool console) {
    currentTaskSem = xSemaphoreCreateMutex();
    printConsole = console;
    xTaskCreatePinnedToCore(stats_task, "stats", 3072, NULL, STATS_TASK_PRIO, NULL, tskNO_AFFINITY);
}

void get_current_stats(cJSON *responseDoc) {
    cJSON *runtime = cJSON_AddObjectToObject(responseDoc, "runtime");

    if (xSemaphoreTake(currentTaskSem, pdMS_TO_TICKS(200)) == pdTRUE) {
        // cJSON_AddNumberToObject(runtime, "total_runtime", currentElapsedTime);
        cJSON_AddNumberToObject(runtime, "task_count", currentTaskList.size());

        cJSON *tasks = cJSON_AddArrayToObject(runtime, "tasks");

        for (auto t : currentTaskList) {
            cJSON *item = cJSON_CreateObject();

            cJSON_AddStringToObject(item, "name", t.name);
            cJSON_AddNumberToObject(item, "prio", t.priority);
            cJSON_AddNumberToObject(item, "free_stack", t.stackHighWaterMark);
#ifdef CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
            cJSON_AddNumberToObject(item, "core", t.coreId);
#endif
            // cJSON_AddNumberToObject(item, "runtime", t.runtimeCounter);
            if (currentElapsedTime > 0) {
                // Fixed-point number with one decimal place
                cJSON_AddNumberToObject(item, "cpu", t.cpu);
            }
            cJSON_AddNumberToObject(item, "state", t.state);

            cJSON_AddItemToArray(tasks, item);
        }

        xSemaphoreGive(currentTaskSem);
    }
}

static TaskStatus_t *get_task_system_state(UBaseType_t *array_size, configRUN_TIME_COUNTER_TYPE *runtime) {
    TaskStatus_t *task_status = NULL;
    UBaseType_t   size;

    // Allocate array to store current task states
    size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    task_status = static_cast<TaskStatus_t *>(malloc(sizeof(TaskStatus_t) * size));
    if (task_status == NULL) {
        *array_size = 0;
        return NULL;
    }
    // Get current task states
    *array_size = uxTaskGetSystemState(task_status, size, runtime);
    if (*array_size == 0) {
        free(task_status);
        return NULL;
    }

    return task_status;
}

static void print_console_stats(const std::vector<TaskStatistic> &taskList, uint32_t total_elapsed_time) {
    assert(total_elapsed_time);

    printf("\n| Task           Prio | Runtime |  CPU     | Stack\n");
    for (auto t : taskList) {
#ifdef CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
        char core = t.coreId == -1 ? '-' : t.coreId == PRO_CPU_NUM ? 'P' : t.coreId == APP_CPU_NUM ? 'A' : '?';
#else
        char core = ' ';
#endif
        printf("| %-16s %2d | %7lu | %3d.%1d%% %c | %5u\n", t.name, t.priority, t.runtimeCounter, t.cpu / 10,
               t.cpu % 10, core, t.stackHighWaterMark);
    }
}

static void stats_task(void *arg) {
    ESP_LOGI(TAG, "Starting real time statistics, interval: %dms", STATS_INTERVAL_MS);

    TaskStatus_t               *start_array = NULL, *end_array = NULL;
    UBaseType_t                 start_array_size, end_array_size;
    configRUN_TIME_COUNTER_TYPE start_run_time, end_run_time;

    // continuously query system statistics
    while (1) {
        if (start_array == NULL) {
            start_run_time = end_run_time = 0;
            start_array = get_task_system_state(&start_array_size, &start_run_time);
            if (start_array == NULL) {
                ESP_LOGE(TAG, "Failed to get system statistics");
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(STATS_INTERVAL_MS));

        end_array = get_task_system_state(&end_array_size, &end_run_time);
        if (end_array == NULL) {
            ESP_LOGE(TAG, "Failed to get system statistics: terminating runtime statistic task!");
            break;
        }

        // process statistics, lock access to currentTaskList for WS requests
        xSemaphoreTake(currentTaskSem, portMAX_DELAY);

        // Calculate elapsed time of current interval in units of run time stats clock period.
        currentElapsedTime = (end_run_time - start_run_time);
        if (currentElapsedTime == 0) {
            ESP_LOGE(TAG, "Invalid runtime state. Check if all STATS config options are enabled");
            xSemaphoreGive(currentTaskSem);
            break;
        }

        currentTaskList.clear();
        // Match each task in start_array to those in the end_array
        for (int i = 0; i < start_array_size; i++) {
            int k = -1;
            for (int j = 0; j < end_array_size; j++) {
                if (start_array[i].xHandle == end_array[j].xHandle) {
                    k = j;
                    break;
                }
            }
            // Check if matching task found
            if (k >= 0) {
                uint32_t      runtime = end_array[k].ulRunTimeCounter - start_array[i].ulRunTimeCounter;
                TaskStatistic stat = {
                    .name = 0,
                    .priority = static_cast<uint8_t>(start_array[i].uxCurrentPriority),
                    .runtimeCounter = runtime,
                    .cpu = static_cast<uint16_t>(runtime * 1000 / currentElapsedTime),
                    .stackHighWaterMark = static_cast<uint16_t>(end_array[k].usStackHighWaterMark),
#ifdef CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
                    .coreId = start_array[i].xCoreID == tskNO_AFFINITY ? static_cast<int8_t>(-1)
                                                                       : static_cast<int8_t>(start_array[i].xCoreID),
#endif
                    .state = static_cast<uint8_t>(end_array[k].eCurrentState),
                };
                strncpy(stat.name, start_array[i].pcTaskName, sizeof(stat.name));
                currentTaskList.push_back(stat);
            }
        }

        if (printConsole) {
            // sort by runtime counter is only required for console pretty print
            std::sort(currentTaskList.begin(), currentTaskList.end(),
                      [](const TaskStatistic &left, const TaskStatistic &right) {
                          return left.runtimeCounter > right.runtimeCounter;
                      });

            print_console_stats(currentTaskList, currentElapsedTime);
        }

        xSemaphoreGive(currentTaskSem);

        // swap statistics: end counters are now the start counters
        free(start_array);

        start_array = end_array;
        start_array_size = end_array_size;
        start_run_time = end_run_time;
        end_run_time = 0;
    }

    free(start_array);
    free(end_array);

    vTaskDelete(NULL);
}

#else
void start_stats_task(bool) {
    ESP_LOGI(TAG, "Runtime statistics are not enabled");
}

void get_current_stats(cJSON *) {}
#endif
