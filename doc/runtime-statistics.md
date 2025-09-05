# Runtime Statistics

Realtime runtime statistics can be enabled with IDF build flags. This is a development feature to monitor CPU & stack usage.

Once enabled, statistics are continuously gathered in an interval of 2 seconds.
The CPU & free stack usage of all tasks are calculated.
The stats are logged in the console and can also be retrieved with a WebSocket call.

## Build

Required build flags to enable statistics:
- [FREERTOS_USE_TRACE_FACILITY](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-reference/kconfig.html#config-freertos-use-trace-facility)
- [FREERTOS_USE_STATS_FORMATTING_FUNCTIONS](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-reference/kconfig.html#config-freertos-use-stats-formatting-functions)
- [FREERTOS_GENERATE_RUN_TIME_STATS](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-reference/kconfig.html#config-freertos-generate-run-time-stats)

Optional, but recommended:
- [FREERTOS_VTASKLIST_INCLUDE_COREID](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-reference/kconfig.html#config-freertos-vtasklist-include-coreid)

Include the [sdkconfig.stats](../sdkconfig.stats) in the build to enable all options.

## Console logging

The statistics are printed every 2 seconds, ordered by runtime usage.

Example output:
```
| Task           Prio | Runtime |  CPU     | Stack
| IDLE1             0 | 2060414 |  99.5% A |   696
| IDLE0             0 | 1927581 |  93.1% P |   688
| stats             6 |   70680 |   3.4% - |   804
| wifi             23 |   33025 |   1.5% P |  2652
| esp_timer        22 |   18444 |   0.8% P |  2968
| tiT              18 |   11376 |   0.5% - |  1760
| Tmr Svc           1 |    8751 |   0.4% - |  1376
| mdns              1 |    7877 |   0.3% P |  1024
| httpd             5 |    1515 |   0.0% - |  2176
| taskLVGL          4 |     308 |   0.0% - |  3692
| ksz8851snl_tsk   15 |      30 |   0.0% - |  3300
| IR send          18 |       0 |   0.0% A |   868
| ipc1             24 |       0 |   0.0% A |   432
| sys_evt          20 |       0 |   0.0% P |   480
| network           5 |       0 |   0.0% - |  1396
| ipc0             24 |       0 |   0.0% P |   536
| IR learn          5 |       0 |   0.0% A |   828
```

- Runtime: total amount of run time consumed by the task
- CPU usage is scaled to 100% / core.
- Core flags:
  - `A`: pinned to application core 1
  - `P`: pinned to program core 0
  - `-`: not pinned to a core
- Stack: lowest free stack size

## WebSocket

Request statistic of the last interval:

```json
{
    "type": "dock",
    "command": "get_stats",
    "id": 123
}
```

- Response code 503: the firmware doesn't have the trace support compiled in.
- Example response when trace support is enabled:

```json
{
    "req_id": 123,
    "type": "dock",
    "msg": "get_stats",
    "runtime": {
        "task_count": 16,
        "tasks": [
            {
                "name": "IDLE1",
                "prio": 0,
                "free_stack": 688,
                "core": 1,
                "cpu": 995,
                "state": 1
            },
            {
                "name": "IDLE0",
                "prio": 0,
                "free_stack": 696,
                "core": 0,
                "cpu": 957,
                "state": 1
            },
            {
                "name": "stats",
                "prio": 6,
                "free_stack": 832,
                "core": -1,
                "cpu": 32,
                "state": 0
            },
            {
                "name": "esp_timer",
                "prio": 22,
                "free_stack": 2960,
                "core": 0,
                "cpu": 7,
                "state": 3
            },
            {
                "name": "ksz8851snl_tsk",
                "prio": 15,
                "free_stack": 2796,
                "core": -1,
                "cpu": 3,
                "state": 2
            },
            {
                "name": "tiT",
                "prio": 18,
                "free_stack": 1752,
                "core": -1,
                "cpu": 2,
                "state": 2
            },
            {
                "name": "Tmr Svc",
                "prio": 1,
                "free_stack": 1528,
                "core": -1,
                "cpu": 1,
                "state": 2
            },
            {
                "name": "httpd",
                "prio": 5,
                "free_stack": 2072,
                "core": -1,
                "cpu": 0,
                "state": 2
            },
            {
                "name": "taskLVGL",
                "prio": 4,
                "free_stack": 3716,
                "core": -1,
                "cpu": 0,
                "state": 2
            },
            {
                "name": "ipc1",
                "prio": 24,
                "free_stack": 440,
                "core": 1,
                "cpu": 0,
                "state": 3
            },
            {
                "name": "sys_evt",
                "prio": 20,
                "free_stack": 488,
                "core": 0,
                "cpu": 0,
                "state": 2
            },
            {
                "name": "IR send",
                "prio": 18,
                "free_stack": 864,
                "core": 1,
                "cpu": 0,
                "state": 2
            },
            {
                "name": "mdns",
                "prio": 1,
                "free_stack": 1024,
                "core": 0,
                "cpu": 0,
                "state": 2
            },
            {
                "name": "IR learn",
                "prio": 5,
                "free_stack": 824,
                "core": 1,
                "cpu": 0,
                "state": 2
            },
            {
                "name": "network",
                "prio": 5,
                "free_stack": 1420,
                "core": -1,
                "cpu": 0,
                "state": 2
            },
            {
                "name": "ipc0",
                "prio": 24,
                "free_stack": 528,
                "core": 0,
                "cpu": 0,
                "state": 3
            }
        ]
    },
    "code": 200
}
```

- `free_stack`: lowest free stack size
- `core`: 0, 1: pinned to core, -1: not pinned to a core
- `cpu`: CPU usage % as a fixed-point number with one decimal place
- `state`: FreeRTOS eTaskState: 0 = running, 1 = ready, 2 = blocked, 3 = suspended, 4 = deleted, 5 = invalid
