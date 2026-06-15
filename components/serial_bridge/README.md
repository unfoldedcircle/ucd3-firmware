# Serial Bridge Component

## Overview

The Serial Bridge component provides bidirectional RS232-to-network bridging for the two
configurable external ports on the UCD3 dock. Each external port can be independently
configured to operate in RS232 mode, enabling serial communication with AV devices such
as receivers, projectors, and media players.

The component offers two parallel data paths:

1. **Raw TCP Server** (optional): GlobalCache iTach-compatible TCP passthrough on ports
   4999 and 5000. Up to 8 simultaneous clients per port. Data is forwarded 1:1 without
   framing or interpretation.

2. **WebSocket JSON API**: Line-based or chunk-based serial data forwarding to subscribed
   WebSocket clients. Supports configurable buffering, terminator character, and idle
   timeouts for protocol-aware message framing.

Both paths can operate simultaneously on the same serial port.


## Architecture

### Task-based Design (not Interrupt-driven)

Each serial port runs a dedicated FreeRTOS task when in RS232 mode. This decision was
made for the following reasons:

- UART ISRs are already handled internally by the IDF UART driver (FIFO thresholds,
  ring buffer management)
- lwIP socket operations (`send()`, `recv()`, `select()`) are not ISR-safe
- Per-port task isolation: a failure on one port does not affect the other
- Minimal overhead: ~4 KB stack per task

The bridge task is pinned to **Core 0** at **priority 5**. Core 1 is reserved for
time-critical IR sending (priority 18).

### Resource Budget

| Resource               | Per Port   | Total (2 Ports) |
|------------------------|------------|-----------------|
| Task Stack             | 4096 Byte  | 8192 Byte       |
| UART RX Buffer         | 2048 Byte  | 4096 Byte       |
| UART TX Buffer         | 512 Byte   | 1024 Byte       |
| UART Event Queue       | 320 Byte   | 640 Byte        |
| Bridge struct          | ~220 Byte  | ~440 Byte       |
| Task rx_buf            | 256 Byte   | 512 Byte        |
| TCP Sockets (lwIP)     | ~4 KB max  | ~8 KB max       |
| Serial line buffer     | 512 Byte*  | 1024 Byte*      |

*Line buffer size is configurable (1–16384 bytes).


## TCP Server

### Port Mapping (GlobalCache-compatible)

| Port Index | UART       | TCP Port |
|------------|------------|----------|
| 1          | UART_NUM_1 | 4999     |
| 2          | UART_NUM_2 | 5000     |

### Configuration

TCP servers are optional and can be enabled/disabled globally via the `Config` class:

```cpp
config->enableSerialTcp(true);   // Enable TCP servers
config->isSerialTcpEnabled();    // Query current state
```

When disabled, the bridge task still runs (servicing the UART for WebSocket callbacks)
but does not open TCP listening sockets.

#### Multi-Client Behavior

- Maximum 8 simultaneous TCP connections per port
- The 9th connection attempt is accepted and immediately closed (connection refused)
- UART RX → TCP: Received serial data is broadcast to ALL connected TCP clients
- TCP → UART: Any connected client can send data; all data is forwarded to the UART
  in order of arrival
- There is no arbitration or locking between clients sending concurrently. Bytes from
  different clients may interleave at the UART if they send simultaneously.

#### Socket Configuration

| Option          | Value     | Purpose                              |
|-----------------|-----------|--------------------------------------|
| `SO_REUSEADDR`  | enabled   | Quick rebind after restart           |
| `TCP_NODELAY`   | enabled   | Low latency for control protocols    |
| `SO_KEEPALIVE`  | enabled   | Detect dead connections              |
| `TCP_KEEPIDLE`  | 60s       | Time before first keep-alive probe   |
| `TCP_KEEPINTVL` | 10s       | Interval between probes              |
| `TCP_KEEPCNT`   | 3         | Probes before declaring dead         |
| `SO_SNDTIMEO`   | 5s        | Send timeout (prevents blocking)     |
| `O_NONBLOCK`    | enabled   | Non-blocking I/O                     |

#### Error Handling

| Situation                    | Behavior                                      |
|------------------------------|-----------------------------------------------|
| Client disconnect            | Close socket, free slot, continue operation    |
| `send()` returns EAGAIN      | Drop data for this client, log warning         |
| `send()` returns other error | Close client                                   |
| `select()` error             | Log, delay 100ms, retry                        |
| TCP bind failure             | Task continues without TCP (callback-only)     |

## Bridge Task Loop

The bridge task uses a select()-based event loop with a 20ms timeout:

```
while (running) {
    1. Reset task watchdog
    2. If TCP enabled:
       - select() on listen_fd + all client_fds (20ms timeout)
       - Accept new connections
       - Read from TCP clients → uart_write_bytes()
    3. If TCP disabled:
       - vTaskDelay(20ms)
    4. Drain UART event queue (non-blocking):
       - UART_DATA → read bytes → broadcast to TCP + notify rx_callback
       - UART_FIFO_OVF / UART_BUFFER_FULL → flush + reset queue
       - UART_FRAME_ERR / UART_PARITY_ERR → log warning
    5. Notify idle tick (rx_callback with NULL data for timeout processing)
}
```

### Task Watchdog

The bridge task subscribes to the ESP-IDF Task Watchdog Timer (TWDT) for 24/7
reliability:

- `esp_task_wdt_add(NULL)` — Subscribe at task start
- `esp_task_wdt_reset()` — Feed watchdog every loop iteration (~20ms)
- `esp_task_wdt_delete(NULL)` — Unsubscribe before task deletion

If the task fails to feed the watchdog within the configured timeout
(`CONFIG_ESP_TASK_WDT_TIMEOUT_S`, typically 5s), the system logs an error or panics
depending on configuration.

## Data Flow

```
    ┌─────────────────────────────────────────────────────────────────────┐
    │                          bridge_task                                │
    │                     (1 task per serial port)                        │
    ├─────────────────────────────────────────────────────────────────────┤
    │                                                                     │
    │  TCP → UART:                                                        │
    │                                                                     │
    │  TCP Client 1 ──┐                                                   │
    │  TCP Client 2 ──┼── recv() ──► uart_write_bytes() ──► UART TX       │
    │  TCP Client N ──┘                                                   │
    │                                                                     │
    │                                                                     │
    │  UART → TCP + Callback:                                             │
    │                                                                     │
    │  UART RX ──► uart_event_queue ──► uart_read_bytes()                 │
    │                                         │                           │
    │                               ┌─────────┴──────────┐                │
    │                               ▼                    ▼                │
    │                   broadcast_to_clients()    rx_callback()           │
    │                               │                    │                │
    │                               ▼                    │                │
    │                     send() to each                 │                │
    │                     TCP Client 1..N                │                │
    │                                                    │                │
    └────────────────────────────────────────────────────┼────────────────┘
                                                         │
                                                         ▼
    ┌────────────────────────────────────────────────────────────────────┐
    │                     DockApi (httpd task)                           │
    ├────────────────────────────────────────────────────────────────────┤
    │                                                                    │
    │  handleSerialRx()                                                  │
    │       │                                                            │
    │       ├── CHUNK mode: send immediately via sendSerialEvent()       │
    │       │                                                            │
    │       ├── LINE mode: buffer until terminator or timeout            │
    │       │       │                                                    │
    │       │       ├── terminator found → flushSerialBuffer()           │
    │       │       ├── buffer full → flushSerialBuffer()                │
    │       │       └── idle tick + timeout elapsed → flushSerialBuffer()│
    │       │                                                            │
    │       └── sendSerialEvent()                                        │
    │                 │                                                  │
    │                 ▼                                                  │
    │    Build JSON: {"type":"event","msg":"serial_data","port":N,...}   │
    │                 │                                                  │
    │                 ▼                                                  │
    │    web_->sendWsTxt(fd, msg) → subscribed WS clients only           │
    │                                                                    │
    │                                                                    │
    │  WS → UART (send_serial command):                                  │
    │                                                                    │
    │  WS Client ──► processSendSerial()                                 │
    │                      │                                             │
    │                      ▼                                             │
    │        serial_bridge_send_to_uart() ──► uart_write_bytes()         │
    │                                              │                     │
    │                                              ▼                     │
    │                                           UART TX                  │
    │                                                                    │
    └────────────────────────────────────────────────────────────────────┘
```

Summary:
```
    ┌──────────────┐         ┌──────────────┐         ┌──────────────┐
    │  TCP Client  │◄───────►│              │◄───────►│   UART Port  │
    │  (raw TCP)   │  send/  │ bridge_task  │  read/  │              │
    │  port 4999+  │  recv   │              │  write  │  UART_NUM_x  │
    └──────────────┘         └──────┬───────┘         └──────────────┘
                                    │ rx_callback             ▲
                                    ▼                         │
                             ┌──────────────┐                 │
                             │   DockApi    │                 │
                             │ (buffering)  │                 │
                             │              │   serial_bridge_send_to_uart()
                             │ sendWsTxt()  │─────────────────┘
                             └──────┬───────┘
                                    │
                                    ▼
                             ┌──────────────┐
                             │  WS Client   │
                             │  (JSON API)  │
                             └──────────────┘
```

## WebSocket API Messages

All serial WebSocket commands require an authenticated connection and use the standard
dock message format with `"type": "dock"`.

### Enable/Disable Serial Events

Subscribe to serial data events for a specific port. Events are delivered only to
clients that have explicitly enabled them.

Request:
```json
{
  "type": "dock",
  "command": "enable_serial_events",
  "port": 1,
  "enable": true
}
```

Response:
```json
{
  "type": "dock",
  "msg": "enable_serial_events",
  "code": 200
}
```

### Serial Data Event

Sent to subscribed clients when serial data is received. Delivery depends on the
configured buffering mode:

- **Chunk mode**: Each UART read chunk is sent immediately as a separate event
- **Line mode**: Data is buffered until the terminator character is received, the
  buffer is full, or the idle timeout expires

```json
{
  "type": "event",
  "msg": "serial_data",
  "port": 1,
  "data": "MV45\r"
}
```

The `data` field contains valid UTF-8. Raw bytes 0x80–0xFF are treated as Latin-1
(ISO-8859-1) and converted to proper UTF-8 encoding. Control characters 0x00–0x1F
are JSON-escaped by cJSON (`\u00XX`).

### Send Serial Data

Send data to an RS232 port via the UART TX line.

Request:
```json
{
  "type": "dock",
  "command": "send_serial",
  "port": 1,
  "data": "MVUP\r"
}
```

Response:
```json
{
  "type": "dock",
  "msg": "send_serial",
  "code": 200
}
```

Error codes:

- `400` — Invalid port number or missing data field
- `409` — Port is not configured in RS232 mode
- `500` — UART write failed

### Configure Serial Buffering

Configure the buffering behavior for WebSocket serial events. All fields are optional;
omitted fields retain their current value. Configuration is persisted.

Request:
```json
{
  "type": "dock",
  "command": "set_serial_config",
  "port": 1,
  "buffering": "line",
  "terminator": "\r",
  "buffer_size": 512,
  "timeout_ms": 100
}
```

| Field         | Type   | Default | Description                              |
|---------------|--------|---------|------------------------------------------|
| `port`        | int    | —       | Required. 1-based port number.           |
| `buffering`   | string | "chunk" | `"chunk"` or `"line"`                    |
| `terminator`  | string | "\n"    | Single character used as line terminator |
| `buffer_size` | int    | 512     | Buffer size in bytes (1–16384)           |
| `timeout_ms`  | int    | 100     | Idle timeout in ms. 0 = no timeout.      |


Response:
```json
{
  "type": "dock",
  "msg": "set_serial_config",
  "code": 200
}
```

Error codes:

- `400` — Invalid port, unknown buffering mode, or buffer_size > 16384
- `500` — Buffer allocation failed

### Get Serial Configuration

Request:
```json
{
  "type": "dock",
  "command": "get_serial_config",
  "port": 1
}
```

Response:
```json
{
  "type": "dock",
  "msg": "get_serial_config",
  "port": 1,
  "buffering": "line",
  "terminator": "\r",
  "buffer_size": 512,
  "timeout_ms": 100,
  "uart": {
    "baud_rate": 115200,
    "data_bits": 8,
    "parity": "none",
    "stop_bits": "1"
  }
  "code": 200
}
```

## Buffering Modes
### Chunk Mode
Every UART read chunk (up to 256 bytes per bridge task iteration) is immediately
forwarded as a JSON event. No buffering, no interpretation. This provides the lowest
latency but may split protocol messages across multiple events.
Suitable for: Binary protocols, custom framing, or when the client handles reassembly.

### Line Mode (default)
Data is accumulated in a per-port buffer until one of these conditions is met:

1. Terminator character received — Buffer contents (including the terminator) are
   flushed as a single JSON event
2. Buffer full — Contents are flushed to prevent data loss
3. Idle timeout — If no new data arrives within `timeout_ms` after the last byte,
   the buffer is flushed. This prevents data from being stuck in the buffer indefinitely
   when a device sends a partial response without a terminator.

Suitable for: AV control protocols (Denon/Marantz `\r`, PJ Link, Sony, etc.)

### Buffer Allocation

- Buffer size < 1024 bytes: Allocated from internal RAM
- Buffer size >= 1024 bytes: Attempt PSRAM first, fall back to internal RAM
- Maximum configurable size: 16384 bytes (16 KB)

### Timeout Implementation

The timeout is checked via the bridge task’s idle tick mechanism. Every loop iteration
(~20ms), the bridge task calls the rx_callback with `data=NULL, len=0`. The DockApi
handler checks if `timeout_ms` has elapsed since the last received byte and flushes
if so.

Effective timeout resolution: ~20ms (determined by the bridge task’s select() timeout).
For a configured 100ms timeout, actual flush occurs between 100–120ms after the last
byte. This is acceptable for AV control protocol use cases.

## Lifecycle Management

### Bridge Start/Stop

The bridge task is started when a port enters RS232 mode and stopped when the port
changes to any other mode (IR, trigger, etc.):

Port mode change to RS232:
  1. ExternalPort::changeMode(RS232) initializes UART driver
  2. DockApi starts the bridge: serial_bridge_start()
  3. Bridge task opens TCP server (if enabled) and begins UART processing

Port mode change away from RS232:
  1. DockApi stops the bridge: serial_bridge_stop()  [synchronous, blocks up to 500ms]
  2. All TCP clients are disconnected, sockets closed
  3. ExternalPort::changeMode(new_mode) deinitializes UART driver

The stop-before-deinit ordering prevents race conditions where the bridge task would
access a destroyed UART driver.

### Runtime TCP Enable/Disable
TCP servers can be toggled at runtime without restarting the bridge:

set_serial_tcp command:
  1. Persist new setting via Config
  2. Stop all running bridges
  3. Update tcp_enabled flag on each bridge instance
  4. Restart all bridges that were previously running

## Limitations and Design Notes

1. `No flow control between TCP clients`:  
   If multiple TCP clients send data concurrently, their bytes may interleave on the UART TX line.
   The bridge provides no mutual exclusion for senders. This is by design and is typically not a
   problem for request-response AV protocols.


2. `Data loss on slow TCP clients`:  
   If a TCP client’s send buffer is full (`EAGAIN`/`EWOULDBLOCK`), data destined for that client is dropped.
   Other clients are unaffected.


3. `No binary WebSocket transport`:  
   The WebSocket API uses JSON with UTF-8 text.  
   Raw bytes 0x80–0xFF are Latin-1-to-UTF-8 converted. Null bytes (0x00) are
   JSON-escaped as `\u0000`. For binary-heavy protocols, use the raw TCP interface.


4. `Line buffer mutex ordering`:  
   The lock hierarchy is always `SerialPortBuffer::mutex` → `serial_event_mutex_`.
   This ordering must be maintained to prevent deadlocks.


5. `Idle tick overhead`:  
   The rx_callback is invoked with NULL data every ~20ms for timeout processing,
   even when no serial data is flowing. The handler returns immediately if no subscribers
   are registered or the buffer is empty.


6. `UART buffer sizing`:  
   The UART RX ring buffer is set to 2048 bytes to prevent overflow during TCP passthrough
   at high baud rates. At 115200 baud, approximately 230 bytes accumulate during a 20ms select()
   timeout — well within the 2048 byte buffer.
