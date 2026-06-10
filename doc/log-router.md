# Log Router

## Overview

The Log Router is a lightweight logging infrastructure component that intercepts ESP-IDF log messages and forwards them
to multiple output destinations while preserving the default console/UART output. It was designed specifically for the
ESP32-S3 Dock 3 firmware to enable real-time log streaming to WebSocket clients.

**Primary Use Case:** Real-time debugging and monitoring via WebSocket API or the upcoming web management UI without
compromising serial console output.

**Developer Feature:** Log streaming is designed for developer use and is inactive during normal operation. The
FreeRTOS sender task and the log queue only exist while at least one WebSocket client is subscribed.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP_LOG* Macros                          │
│              (ESP_LOGI, ESP_LOGE, ESP_LOGD, etc.)           │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                  esp_log_set_vprintf()                      │
│              (Intercepts all log output)                    │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                  log_router_vprintf()                       │
│  ┌───────────────────────────────────────────────────────┐  │
│  │ FAST PATH (normal runtime, no subscribers):           │  │
│  │ - Check g_active atomic flag                          │  │
│  │ - If false: call original vprintf and return          │  │
│  │ - Overhead: single atomic load                        │  │
│  └───────────────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────────────┐  │
│  │ SLOW PATH (when queue is active):                     │  │
│  │ - Always call original vprintf (UART/console)         │  │
│  │ - Format message to fixed-size buffer                 │  │
│  │ - Parse tag and level                                 │  │
│  │ - Allocate log_entry_t and post to FreeRTOS queue     │  │
│  │ - Drop silently if queue is full (non-blocking)       │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
          │                              │
          ▼                              ▼
┌──────────────────┐          ┌──────────────────────────────┐
│ Original vprintf │          │  FreeRTOS Queue              │
│ (Console/UART)   │          │  (LOG_QUEUE_DEPTH = 64)      │
│ ALWAYS ACTIVE    │          └──────────────�───────────────┘
└──────────────────┘                        │
                                            │ xQueueReceive(portMAX_DELAY)
                                            ▼
                               ┌─────────────────────────────┐
                               │  logSenderTask (DockApi)    │
                               │  - Lazy: exists only while  │
                               │    subscribers > 0          │
                               │  - Blocks on portMAX_DELAY  │
                               │  - Exits on sentinel entry  │
                               └─────────────────────────────┘
                                            │
                                            ▼
                               ┌─────────────────────────────┐
                               │  sendLogToSubscribers()     │
                               │  - Snapshot FD list         │
                               │  - Stack-buffer JSON build  │
                               │  - Send to ≤4 WS clients    │
                               └─────────────────────────────┘
```

## Component Structure

```
components/
└── log_router/
    ├── CMakeLists.txt
    ├── include/
    │   └── log_router.h
    └── log_router.c
```

### Files

| File             | Purpose                                              | Language |
|------------------|------------------------------------------------------|----------|
| `log_router.h`   | Public API header with `log_entry_t` type definition | C        |
| `log_router.c`   | Implementation with vprintf interception and queue   | C        |
| `CMakeLists.txt` | ESP-IDF component registration                       | CMake    |

## Public API

### Type Definitions

```c
// Reserved level value used as a queue sentinel to signal logSenderTask to stop.
// Must not overlap any esp_log_level_t value (0–5).
#define LOG_ENTRY_SENTINEL 0xFF

typedef struct {
    char     tag[32];       // Extracted log tag, e.g. "API", "wifi"
    uint8_t  level;         // esp_log_level_t or LOG_ENTRY_SENTINEL
    uint16_t len;           // Length of message string
    char     message[100];  // Formatted log text, truncated to LOG_MSG_MAX_LEN
} log_entry_t;
```

### Constants

| Constant          | Value | Rationale                                                                 |
|-------------------|-------|---------------------------------------------------------------------------|
| `LOG_MESSAGE_LEN` | 100   | Caps per-entry heap allocation and queue memory footprint                 |
| `LOG_QUEUE_DEPTH` | 64    | Absorbs short bursts; entries are freed immediately after use             |
| `MAX_WS_LOG_SUBS` | 2     | Hard cap on concurrent log streaming WebSocket clients                    |
| `WS_LOG_DELAY_MS` | 100   | Delay in ms between sending WebSocket log messages to avoid dropping msgs |

### Functions

#### `log_router_init()`

```c
esp_err_t log_router_init(void);
```

Initializes the log router by intercepting ESP-IDF's vprintf function. Must be called once at startup before
`log_router_start()`. Does **not** activate log forwarding — the queue is not created until `log_router_start()`.

**Returns:**

- `ESP_OK`: Success
- Error: Initialization failed

**Side Effects:**

- Calls `esp_log_set_vprintf()` to intercept all log output.
- Original vprintf is preserved and always called (console output never lost).
- Sets the `g_active` atomic flag to `false`; the fast path is taken for every log call until `log_router_start()`.

#### `log_router_start()`

```c
esp_err_t log_router_start(QueueHandle_t *queue_out);
```

Creates the FreeRTOS log queue and activates log forwarding. Called by `DockApi` when the first WebSocket client
subscribes to log events. Idempotent: if called while already started, returns the existing queue handle.

**Parameters:**

- `queue_out`: Output parameter; receives the queue handle to be passed to `logSenderTask`.

**Returns:**

- `ESP_OK`: Success
- `ESP_ERR_INVALID_STATE`: Not initialized
- `ESP_ERR_NO_MEM`: Queue creation failed

**Side Effects:**

- Creates a FreeRTOS queue of depth `LOG_QUEUE_DEPTH` holding `log_entry_t *` pointers.
- Sets `g_active` to `true`; the slow path is taken for subsequent log calls.

#### `log_router_stop()`

```c
esp_err_t log_router_stop(void);
```

Deactivates log forwarding and signals `logSenderTask` to exit cleanly via a sentinel queue entry. Called by `DockApi`
when the last WebSocket subscriber disconnects or unsubscribes.

**Returns:**

- `ESP_OK`: Success
- `ESP_ERR_INVALID_STATE`: Not initialized or not started

**Side Effects:**

1. Sets `g_active` to `false` atomically; no new real entries are enqueued after this point.
2. Allocates a sentinel `log_entry_t` with `level = LOG_ENTRY_SENTINEL` and posts it to the queue.
3. `logSenderTask` dequeues the sentinel, performs a final drain, and calls `vTaskDelete(NULL)`.

If the queue is full when the sentinel is posted, `log_router_stop()` logs a warning and returns. The sender task
will remain blocked until a real entry is eventually dequeued to make room, at which point the caller's destructor
fallback (`vTaskDelete`) provides the safety net.

## Design Decisions

### vprintf Interception via `esp_log_set_vprintf()`

**Rationale:**

- IDF `ESP_LOG*` functions are used throughout the project.
- `esp_log_set_vprintf()` is the correct IDF 5.x interception point.
- Returns original vprintf function pointer, which is stored and always called (UART output never lost).
- Minimal overhead when `g_active` is false (single atomic load).

### Queue-Based Fan-Out Instead of Synchronous Callbacks

**Decision:** Replace synchronous callbacks with a FreeRTOS queue consumed by a dedicated sender task.

**Rationale:**

The previous design executed callbacks synchronously inside the vprintf hook. This meant any slow operation
(WebSocket send, mutex contention) directly delayed every subsequent log call system-wide. The queue design decouples
the logging context from I/O:

| Aspect              | Callback design                        | Queue design                              |
|---------------------|----------------------------------------|-------------------------------------------|
| vprintf hook work   | Format + parse + send (slow path)      | Format + parse + `xQueueSend` (fast)      |
| Blocking risk       | Yes — if WS send queue full            | No — `xQueueSend` with 0 timeout, drops   |
| Sender task exists  | Always (registered at startup)         | Only while subscribers > 0                |
| Shutdown            | `log_router_unregister_callback()`     | Sentinel entry triggers self-deletion     |

### Fast Path Optimization

**Decision:** Atomic `g_active` flag checked first in `log_router_vprintf()`.

**Rationale:**

- Log forwarding is inactive during the entire normal operation lifetime of the device.
- An atomic load on a false flag costs one instruction and does not touch the queue or any heap memory.
- No FreeRTOS primitives are touched in the fast path.

### Console Output Always Active

**Decision:** Original vprintf is always called regardless of `g_active`.

**Rationale:**

- Serial console is critical for debugging and recovery.
- Log router is additive, not a replacement.
- Prevents accidental loss of console access during development.

### Lazy Task Lifecycle

**Decision:** `logSenderTask` is created when the first subscriber enables log events and self-deletes when the last
subscriber is gone.

**Rationale:**

- Log streaming is a developer-only feature; normal firmware operation must not pay any scheduler cost for it.
- A task blocked on `portMAX_DELAY` with no queue to read from is still a live TCB consuming heap and a scheduler
  slot. Creating it only on demand eliminates this cost entirely.
- `portMAX_DELAY` in `xQueueReceive` is correct: between real log messages (typically 10+ seconds apart in normal
  operation) the task must be completely invisible to the scheduler.

### Sentinel-Based Shutdown

**Decision:** `log_router_stop()` posts a `LOG_ENTRY_SENTINEL` entry to the queue to signal `logSenderTask` to exit.

**Rationale:**

Task notification (`xTaskNotifyGive`) was considered but has a fundamental flaw: the notification check occurs at
the top of the loop, so if the task is blocked in `xQueueReceive(portMAX_DELAY)` with no pending messages, it will
not wake up to check the notification until the next real log message arrives — which may be never. The sentinel
approach guarantees immediate wakeup because it is delivered through the same queue the task is already waiting on.

External `vTaskDelete` was also considered. While `vTaskDelete` from outside is supported by FreeRTOS, it introduces
two narrow but real correctness hazards:

1. If deletion fires while `sendLogToSubscribers` holds `log_subscribers_mutex_`, that mutex is permanently locked.
2. The current `log_entry_t *` on the task stack leaks without `free()`.

The sentinel avoids both by letting the task exit cleanly at a safe point after releasing all resources.

### Message Parsing in the vprintf Hook

**Decision:** Tag and level are extracted inside `log_router_vprintf()`, not in the sender task.

**Rationale:**

- The formatted message string is already available in the hook.
- Parsing once here avoids re-parsing in every consumer.
- The `log_entry_t` carries structured metadata (tag, level, timestamp) so `sendLogToSubscribers` can build JSON
  directly without string scanning.

**Parsed Format:** `<LEVEL> (<TIMESTAMP>) <TAG>: <TEXT>`

**Example:** `I (273131) CHARGE: 0 mV`

- Level: `ESP_LOG_INFO` → `'I'`
- Timestamp: `273131` ms
- Tag: `CHARGE`
- Text: `0 mV`

## WebSocket Log Streaming Integration

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    DockApi (ucd_api.cpp)                    │
│  ┌───────────────────────────────────────────────────────┐  │
│  │ log_subscribers_       (std::set<int>, max 4 FDs)     │  │
│  │ log_subscribers_mutex_ (SemaphoreHandle_t)            │  │
│  │ log_sender_task_handle_(TaskHandle_t, nullptr at rest)│  │
│  │ log_queue_             (QueueHandle_t, nullptr at rest│  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
         │
         │ handleEnableLogEvents(): first subscriber
         │   → log_router_start(&log_queue_)
         │   → xTaskCreate(logSenderTask)
         ▼
┌─────────────────────────────────────────────────────────────┐
│                   Log Router Queue                          │
│  log_router_vprintf() enqueues log_entry_t* (non-blocking)  │
└─────────────────────────────────────────────────────────────┘
         │
         │ xQueueReceive(portMAX_DELAY)
         ▼
┌─────────────────────────────────────────────────────────────┐
│                 logSenderTask                               │
│  - Blocks on portMAX_DELAY — zero scheduler cost at rest    │
│  - Detects LOG_ENTRY_SENTINEL → drain → vTaskDelete(NULL)   │
└─────────────────────────────────────────────────────────────┘
         │
         │ sendLogToSubscribers()
         ▼
┌─────────────────────────────────────────────────────────────┐
│              WebSocket Clients (≤ MAX_WS_LOG_SUBS = 4)      │
│  Receive: {"type":"event","msg":"log","level":"I",...}      │
└─────────────────────────────────────────────────────────────┘
```

### Subscription Management

**Data Structures in `DockApi`:**

```cpp
std::set<int>     log_subscribers_;           // WebSocket socket FDs (max 4)
SemaphoreHandle_t log_subscribers_mutex_;     // Protects subscriber set
TaskHandle_t      log_sender_task_handle_;    // nullptr when inactive
QueueHandle_t     log_queue_;                 // nullptr when inactive
```

**Lifecycle:**

1. **Constructor:** `log_router_init()` is called. No task or queue is created.
2. **Client connects:** Not yet subscribed; `log_queue_` remains `nullptr`; fast path active.
3. **First client sends `enable_log_events: true`:**
   - Add FD to `log_subscribers_`.
   - Call `log_router_start(&log_queue_)` — queue created, `g_active` set to `true`.
   - Call `xTaskCreate(logSenderTask)` — task created and immediately blocks on queue.
4. **Additional clients subscribe:** Add FD to `log_subscribers_`. Queue and task already exist.
5. **Client sends `enable_log_events: false`:** Remove FD. If last subscriber, trigger stop sequence.
6. **Client disconnects (`WS_DISCONNECTED`):** Remove FD. If last subscriber, trigger stop sequence.
7. **Stop sequence (last subscriber gone):**
   - Call `log_router_stop()` — sets `g_active` false, posts sentinel to queue.
   - `logSenderTask` dequeues sentinel, drains queue, calls `vTaskDelete(NULL)`.
   - `log_sender_task_handle_` and `log_queue_` are nulled by the task and caller respectively.
8. **Destructor:** Calls `log_router_stop()` if queue still active, then force-kills task if still alive after 50 ms.

### WebSocket API

#### Subscribe to Log Streaming

**Request:**

```json
{
  "type": "dock",
  "id": 100,
  "command": "enable_log_events",
  "enable": true
}
```

**Response:**

```json
{
  "type": "dock",
  "req_id": 100,
  "msg": "enable_log_events",
  "code": 200
}
```

#### Unsubscribe from Log Streaming

**Request:**

```json
{
  "type": "dock",
  "id": 101,
  "command": "enable_log_events",
  "enable": false
}
```

**Response:**

```json
{
  "type": "dock",
  "req_id": 101,
  "msg": "enable_log_events",
  "code": 200
}
```

---

### Log Event Message Format

**JSON Structure:**

```json
{
  "type": "event",
  "msg": "log",
  "level": "I",
  "tag": "CHARGE",
  "ts": 273131,
  "log": "0 mV"
}
```

**Fields:**

| Field   | Type   | Description                           | Example                           |
|---------|--------|---------------------------------------|-----------------------------------|
| `type`  | string | Always `"event"`                      | `"event"`                         |
| `msg`   | string | Event type, always `"log"`            | `"log"`                           |
| `level` | string | Log level character                   | `"I"`, `"E"`, `"W"`, `"D"`, `"V"` |
| `tag`   | string | Log tag extracted from message        | `"CHARGE"`, `"wifi"`, `"API"`     |
| `ts`    | number | Timestamp in milliseconds from boot   | `273131`                          |
| `log`   | string | Log message text (trimmed, no prefix) | `"0 mV"`                          |

**Level Characters:**

- `E`: ESP_LOG_ERROR
- `W`: ESP_LOG_WARN
- `I`: ESP_LOG_INFO
- `D`: ESP_LOG_DEBUG
- `V`: ESP_LOG_VERBOSE

### Message Processing Details

#### Parsing

**Input Format:** `<LEVEL> (<TIMESTAMP>) <TAG>: <TEXT>`

**Example:** `I (273131) CHARGE: 0 mV`

**Parsing Steps:**

1. Find timestamp between `(` and `)`
2. Convert to integer with `strtoul()`
3. Find text after first `:` following timestamp
4. Skip leading spaces after `:`
5. Trim leading whitespace (space, tab, CR, LF)
6. Trim trailing whitespace (space, tab, CR, LF)
7. Truncate to `LOG_MSG_MAX_LEN` (100) characters

## Performance Considerations

### Fast Path (No Subscribers, Normal Runtime)

**Overhead per log call:**

- 1 atomic load: `atomic_load(&g_active)`
- If false: call original vprintf and return
- No memory operations, no queue interaction, no task wakeup

**Impact:** Negligible. This is the path taken for 100% of log calls during normal device operation.

### Slow Path (Subscribers Active)

**Overhead per log call inside `log_router_vprintf()`:**

1. Call original vprintf (UART output — always done regardless)
2. `va_copy` to re-use the `va_list` for queue formatting
3. `vsnprintf` into 100-byte buffer
4. String parsing (tag, level extraction)
5. `malloc(sizeof(log_entry_t))` + fill
6. `xQueueSend` with 0 timeout (non-blocking)

**Overhead in `logSenderTask`, `sendLogToSubscribers` (off the logging critical path):**

1. `xQueueReceive(portMAX_DELAY)` — zero CPU when idle
2. `cJSON_PrintUnformatted`
3. `WebServer::sendWsTxt()` for each subscriber (async via httpd work queue)
4. `free(entry)`

**Key property:** Steps 1–4 in the vprintf hook complete in bounded, short time. Steps in `logSenderTask` run
asynchronously and cannot delay the logging caller.

### Thread Safety

**Critical Sections:**

1. `g_active` — `_Atomic bool`, accessed with `memory_order_acquire/release`. No mutex needed.
2. `log_subscribers_mutex_` — protects `log_subscribers_` set in `DockApi`. Acquired with 5 ms timeout in the
   sender task (not in the vprintf hook).
3. Queue — FreeRTOS queue is inherently thread-safe; `xQueueSend` (0 timeout) in vprintf hook,
   `xQueueReceive` (portMAX_DELAY) in sender task.

**Lock Ordering:**

- `log_subscribers_mutex_` is never held while calling into the log router.
- The vprintf hook never acquires `log_subscribers_mutex_`.
- No nested lock acquisition exists in the current design.

**Reentrancy:**

- `log_router_vprintf()` can be called from any task or ISR context (subject to IDF log constraints).
- The vprintf hook uses only stack locals and atomic operations — fully reentrant.

## Constraints and Limitations

### Hard Limits

| Parameter         | Value | Rationale                                                                           |
|-------------------|-------|-------------------------------------------------------------------------------------|
| `LOG_MSG_MAX_LEN` | 100   | Bounds heap allocation per entry; most messages fit comfortably                     |
| `LOG_QUEUE_DEPTH` | 64    | Absorbs log bursts; at 100 bytes/entry = ~6 KB peak heap use                        |
| `MAX_WS_LOG_SUBS` | 2     | Prevents unbounded FD set growth, slow message delivery; covers developer use cases |
| Mutex timeout     | 5 ms  | Prevents blocking in sender task; best-effort delivery                              |

The maximum number of log stream clients has been limited to two for an efficient message delivery. The more clients,
the slower message delivery gets (especially during log bursts, like port detection).  
The main issue is within `WebServer::sendWsTxt()`: messages are sent async using `httpd_queue_work`, which is using an
internal work queue. Messages are silently dropped if the queue is full. At the moment the only "workaround" is using
it in blocking mode, which defeats the async requirement.

This behavior has only been recently fixed:
<https://github.com/espressif/esp-idf/commit/c911c781ae94e40d0df6596a25c53025c5f98fb5>

### Known Limitations

1. **Log Queue Drop Policy:** If the queue is full (64 entries pending), new log entries are dropped silently in the
   vprintf hook. This can occur during very high log output bursts. The fast non-blocking `xQueueSend(0)` is
   intentional — blocking inside the vprintf hook would stall all tasks that emit log messages.

2. **No Log Level Filtering in Router:** All log messages are forwarded when `g_active` is true. Level filtering must
   be implemented in the subscriber if needed.

3. **No Message History:** No ring buffer of past messages is stored. Clients that subscribe only receive messages
   emitted after their subscription.

4. **Tag Parsing is Fragile:** Relies on the consistent ESP-IDF log format `<LEVEL> (<TS>) <TAG>: <TEXT>`. If
   ESP-IDF changes this format, parsing may fail silently (the `log` field will contain the full raw message as
   a fallback).

5. **Sentinel Delivery Failure:** If the queue is full when `log_router_stop()` posts the sentinel, `logSenderTask`
   will not exit until the next real entry creates space. The `DockApi` destructor mitigates this with a 50 ms
   `vTaskDelay` followed by a forced `vTaskDelete`.

6. **`ESP_EARLY_LOGx` / `ESP_DRAM_LOGx` Not Intercepted:** These macros bypass `esp_log_set_vprintf()` and go
   directly to the ROM printf. Only `ESP_LOGx` macros are routed through the log router.

## Future Enhancements

### Potential Features

| Feature             | Priority | Description                                          |
|---------------------|----------|------------------------------------------------------|
| Syslog output       | Medium   | Consume from same queue in a second task, UDP syslog |
| Log level filtering | Low      | Per-subscriber level filter to reduce WS traffic     |
| Tag filtering       | Low      | Subscribe to specific tags only                      |
| Message history     | Low      | Ring buffer of last N entries for new subscribers    |

### Syslog Integration Note

The queue-based architecture makes adding a syslog output straightforward. A second consumer task would call
`log_router_start()` independently, receiving its own queue handle, and format entries as RFC 5424 PDUs sent via UDP.
The `log_router_stop()` / sentinel lifecycle applies identically. The two consumers operate fully independently
with no shared state beyond the router's `g_active` flag.

## References

### ESP-IDF Documentation

- [Logging System](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/log.html)
- `esp_log_set_vprintf()` — Override log output function
- `esp_log_level_set()` — Set log level per tag

### Related Components

- `components/webserver/` — WebSocket server implementation
- `main/ucd_api.cpp` — DockApi with log streaming integration
- `main/ucd_api.h` — DockApi class declaration
