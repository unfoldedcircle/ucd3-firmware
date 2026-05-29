# Log Router

## Overview

The Log Router is a lightweight logging infrastructure component that intercepts ESP-IDF log messages and forwards them
to multiple output destinations while preserving the default console/UART output. It was designed specifically for the
ESP32-S3 Dock 3 firmware to enable real-time log streaming to WebSocket clients.

**Primary Use Case:** Real-time debugging and monitoring via WebSocket API or the upcoming web management UI without
compromising serial console output.

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
│  │ FAST PATH (99%+ of runtime):                          │  │
│  │ - Check has_active_callbacks()                        │  │
│  │ - If false: return immediately                        │  │
│  │ - Overhead: single integer comparison                 │  │
│  └───────────────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────────────┐  │
│  │ SLOW PATH (when subscribers active):                  │  │
│  │ - Format message to buffer                            │  │
│  │ - Parse tag and level                                 │  │
│  │ - Forward to all registered callbacks                 │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                            │
            ┌───────────────┴───────────────┐
            │                               │
            ▼                               ▼
┌───────────────────────┐       ┌───────────────────────┐
│  Original vprintf     │       │  Registered Callbacks │
│  (Console/UART)       │       │  - WebSocket (DockApi)│
│  ALWAYS ACTIVE        │       │  - Syslog (future)    │
└───────────────────────┘       └───────────────────────┘
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

| File             | Purpose                                         | Language |
|------------------|-------------------------------------------------|----------|
| `log_router.h`   | Public API header with callback type definition | C        |
| `log_router.c`   | Implementation with vprintf interception        | C        |
| `CMakeLists.txt` | ESP-IDF component registration                  | CMake    |

## Public API

### Type Definitions

```c
typedef esp_err_t (*log_output_callback_t)(
    const char *tag, 
    esp_log_level_t level, 
    const char *message, 
    size_t len,
    void *ctx
);
```

**Callback Parameters:**

- `tag`: Extracted log tag (e.g., "wifi", "API", "CHARGE")
- `level`: ESP log level (ESP_LOG_ERROR, ESP_LOG_WARN, etc.)
- `message`: Full formatted log message including timestamp, level, tag, and log
- `len`: Length of the message string
- `ctx`: User context pointer passed during registration

**Return:** `ESP_OK` on success

### Functions

#### `log_router_init()`

```c
esp_err_t log_router_init(void);
```

Initializes the log router by intercepting ESP-IDF's vprintf function. Must be called before registering callbacks.

**Returns:**

- `ESP_OK`: Success
- Error: Initialization failed

**Side Effects:**

- Calls `esp_log_set_vprintf()` to intercept all log output
- Original vprintf is preserved and always called (console output never lost)

#### `log_router_register_callback()`

```c
esp_err_t log_router_register_callback(
    log_output_callback_t callback, 
    void *ctx, 
    int *callback_id
);
```

Registers a callback to receive all log messages.

**Parameters:**

- `callback`: Function pointer to receive log messages (required, cannot be NULL)
- `ctx`: User context passed to callback on each invocation
- `callback_id`: Output parameter to store callback ID for later unregistration

**Returns:**

- `ESP_OK`: Success
- `ESP_ERR_INVALID_STATE`: Log router not initialized
- `ESP_ERR_INVALID_ARG`: Callback is NULL
- `ESP_ERR_NO_MEM`: Maximum callbacks reached (MAX_LOG_CALLBACKS)

**Example:**

```c
int callback_id;
log_router_register_callback(my_callback, my_context, &callback_id);
```

#### `log_router_unregister_callback()`

```c
esp_err_t log_router_unregister_callback(int callback_id);
```

Unregisters a previously registered callback.

**Parameters:**

- `callback_id`: ID returned from `log_router_register_callback()`

**Returns:**

- `ESP_OK`: Success
- `ESP_ERR_INVALID_STATE`: Log router not initialized
- `ESP_ERR_NOT_FOUND`: Callback ID not found

## Design Decisions

### vprintf Interception via `esp_log_set_vprintf()`

**Rationale:**

- IDF `ESP_LOG*` functions are used in the project
- `esp_log_set_vprintf()` is used in ESP-IDF 5.x API
- Returns original vprintf function pointer for chaining
- Minimal overhead when properly optimized

### Fast Path Optimization

**Decision:** Implement early-exit fast path when no callbacks are active.

**Rationale:**

- Log streaming is inactive 99%+ of runtime
- Every log call incurs overhead only when subscribers exist
- Critical for embedded system with limited CPU resources

### Console Output Always Active

**Decision:** Original vprintf is always called, console output cannot be disabled.

**Rationale:**

- Serial console is critical for debugging and recovery
- Log router is additive, not replacement
- Prevents accidental loss of console access

### Callback Execution in Logging Context

**Decision:** Callbacks execute synchronously within the vprintf call.

**Rationale:**

- Simpler implementation
- No queue management overhead
- WebSocket sending is already async (httpd work queue)

**Trade-offs:**

- **Pro:** No buffer management, no message loss due to queue overflow
- **Con:** Blocking if callback is slow

**Mitigation:** `DockApi::sendLogToSubscribers()` uses non-blocking mutex take with timeout.

**Future Improvement:** If performance becomes an issue, implement a FreeRTOS queue or ring-buffer with a dedicated
sender task.

### Message Parsing in Router

**Decision:** Log router parses messages to extract tag and level, not just forward raw text.

**Rationale:**

- Callbacks receive structured metadata (tag, level) without re-parsing
- Enables filtering by tag or level in callbacks
- Consistent parsing logic across all callbacks

**Parsed Format:** `<LEVEL> (<TIMESTAMP>) <TAG>: <TEXT>`

**Example:** `I (273131) CHARGE: 0 mV`

- Level: `ESP_LOG_INFO`
- Timestamp: `273131` ms
- Tag: `CHARGE`
- Text: `0 mV`

## WebSocket Log Streaming Integration

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    DockApi (ucd_api.cpp)                    │
│  ┌───────────────────────────────────────────────────────┐  │
│  │ log_callback_id_                                      │  │
│  │ log_subscribers_ (std::set<int>)                      │  │
│  │ log_subscribers_mutex_                                │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
         │
         │ log_router_register_callback(logCallback, this, &id)
         ▼
┌─────────────────────────────────────────────────────────────┐
│                   Log Router                                │
│  Forwards all ESP_LOG* messages to logCallback()            │
└─────────────────────────────────────────────────────────────┘
         │
         │ Callback invokes sendLogToSubscribers()
         ▼
┌─────────────────────────────────────────────────────────────┐
│              sendLogToSubscribers()                         │
│  - Parse timestamp from message                             │
│  - Trim whitespace from text                                │
│  - Build JSON with cJSON (auto-escaping)                    │
│  - Send to all subscribers via WebServer::sendWsTxt()       │
└─────────────────────────────────────────────────────────────┘
         │
         │ Async send via httpd work queue
         ▼
┌─────────────────────────────────────────────────────────────┐
│              WebSocket Clients                              │
│  Receive: {"type":"event","msg":"log","level":"I",...}      │
└─────────────────────────────────────────────────────────────┘
```

### Subscription Management

**Data Structures in `DockApi`:**

```c
std::set<int> log_subscribers_;           // WebSocket socket FDs
SemaphoreHandle_t log_subscribers_mutex_; // Protects subscriber set
int log_callback_id_;                     // Log router callback ID
```

**Lifecycle:**

1. **Constructor:** Initialize log router, register callback
2. **Client connects:** Not yet subscribed
3. **Client sends `enable_log_events: true`:** Add to `log_subscribers_`
4. **Client sends `enable_log_events: false`:** Remove from `log_subscribers_`
5. **Client disconnects:** Automatically removed in `WS_DISCONNECTED` handler
6. **Destructor:** Unregister callback, delete mutex

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
  "id": 101,
  "command": "enable_log_events",
  "enable": true,
  "code": 200
}
```

#### Unsubscribe from Log Streaming

**Request:**

```json
{
  "type": "dock",
  "id": 102,
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
7. Truncate to 500 characters max

#### JSON Escaping

**Decision:** No manual JSON escaping required.

**Rationale:** cJSON automatically escapes special characters when serializing:

- `"` → `\"`
- `\` → `\\`
- Control characters → `\uXXXX` or escaped sequences

**Note:** Newlines, tabs, and carriage returns within text are preserved (not replaced with spaces) to maintain original
log message integrity.

## Performance Considerations

### Fast Path (No Subscribers)

**Overhead per log call:**

- 1 integer comparison: `s_next_id == 1`
- No memory operations
- No string formatting
- No callback dispatch

**Impact:** Negligible (< 1% CPU overhead)

### Slow Path (Subscribers Active)

**Overhead per log call:**

1. `va_copy` × 2 (for original vprintf and callback formatting)
2. `vsnprintf` to format message (256 byte buffer)
3. String parsing (tag, level extraction)
4. Mutex take/give for subscriber list
5. cJSON object creation and serialization
6. WebSocket send for each subscriber (async via httpd queue)

**Mitigation:**

- Small buffer size (256 bytes) reduces formatting time
- Non-blocking mutex with 5ms timeout prevents deadlocks
- WebSocket sending is asynchronous (httpd work queue)
- Message truncation at 500 characters limits JSON size

### Thread Safety

**Critical Sections:**

1. `log_subscribers_mutex_` protects `log_subscribers_` set
2. Callback registration uses static array with implicit protection (init happens once at startup)

**Lock Ordering:**

- Always acquire `log_subscribers_mutex_` before any WebSocket operations
- Timeout-based acquisition prevents deadlocks (5ms, best effort to minimally block the log context)

**Reentrancy:**

- `log_router_vprintf()` can be called from any task/context
- Stack buffer is thread-safe, since `vprintf` might not be serialized for every low-level log message or when logging
  from multiple cores.

## Constraints and Limitations

### Hard Limits

| Parameter           | Value          | Rationale                                             |
|---------------------|----------------|-------------------------------------------------------|
| `MAX_LOG_CALLBACKS` | 1              | Only WebSocket API currently uses it                  |
| `MAX_LOG_LEN`       | 256 bytes      | Fits most log messages, reduces stack usage           |
| Message truncation  | 500 characters | Safety feature to prevent excessive WebSocket payload |
| Mutex timeout       | 5ms            | Prevents blocking in logging path                     |

### Known Limitations

1. **Synchronous Callbacks:** Callbacks execute in the logging context. If a callback blocks or is slow, it delays all
   subsequent log messages across the entire system.

2. **No Log Level Filtering in Router:** All log messages are forwarded to callbacks regardless of level. Filtering must
   be implemented in each callback if needed.

3. **No Message Queuing:** If WebSocket send queue is full, log messages are dropped. No retry or buffering mechanism
   exists.

4. **Tag Parsing is Fragile:** Relies on consistent ESP-IDF log format. If ESP-IDF changes format, parsing may fail
   silently (log field will contain full message as fallback).

5. **Maximum 1 Callback:** `MAX_LOG_CALLBACKS` is set to 1. Adding syslog or other outputs requires increasing this
   limit. This is a simple change of the `MAX_LOG_CALLBACKS` define.

## Future Enhancements

### Potential Features

| Feature             | Priority | Description                                 |
|---------------------|----------|---------------------------------------------|
| Syslog output       | Medium   | Add UDP syslog callback for network logging |
| Log level filtering | Low      | Per-callback level filter to reduce traffic |
| Tag filtering       | Low      | Subscribe to specific tags only             |

### Potential Improvements

1. **Asynchronous Queue:** Add FreeRTOS queue or a ring-buffer between log router and callbacks. Callbacks would queue
   messages and a
   dedicated task would process them.

2. **Log Buffering:** Store last N log messages in RAM for retrieval on client connect (history on subscription).

## References

### ESP-IDF Documentation

- [Logging System](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/log.html)
- `esp_log_set_vprintf()` - Override log output function
- `esp_log_level_set()` - Set log level per tag

### Related Components

- `components/webserver/` - WebSocket server implementation
- `main/ucd_api.cpp` - DockApi with log streaming integration
- `main/ucd_api.h` - DockApi class declaration
