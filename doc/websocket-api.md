# Dock WebSocket-API

AsyncAPI definition: https://github.com/unfoldedcircle/core-api/tree/main/dock-api

❗️ The latest API additions for Dock 3 might not yet be included. Non-finalized messages are described in this document.

WebSocket server on Dock 3:

- Port: `80`
- Protocol: `ws`.  
  `wss` is not supported!
- Payload: json text messages
- `id` field is optional, if set it will be echoed back in `req_id`
- Authentication:
  - Supported messages without authentication: `get_sysinfo`
  - Send `auth` message after connection:
- Request messages will be confirmed with `code: 200`. 
  - Codes other than 200 indicates a failure.
  - Codes follow the https status codes, e.g. 400 = bad request etc.

```json
{
    "type": "auth",
    "token": "0000"
}
```

Example request from the Core-API to send an IR code on external port 2:
```json
{
  "type": "dock",
  "command": "ir_send",
  "code": "17;0x2A4C0A8A0282;48;2",
  "format": "hex",
  "int_side": false,
  "int_top": false,
  "ext1": false,
  "ext2": true
}
```

Example request using a PRONTO code:
```json
{
  "type": "dock",
  "command": "ir_send",
  "code": "0000 0070 0000 0064 0080 0040 0010 0010 0010 0010 0010 0030 0010 0010 0010 0030 0010 0010 0010 0030 0010 0010 0010 0010 0010 0030 0010 0010 0010 0010 0010 0030 0010 0030 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0030 0010 0010 0010 0030 0010 0010 0010 0030 0010 0010 0010 0010 0010 0010 0010 0030 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0030 0010 0010 0010 0030 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0ACD 0080 0040 0010 0010 0010 0010 0010 0030 0010 0010 0010 0030 0010 0010 0010 0030 0010 0010 0010 0010 0010 0030 0010 0010 0010 0010 0010 0030 0010 0030 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0030 0010 0010 0010 0030 0010 0010 0010 0030 0010 0010 0010 0010 0010 0010 0010 0030 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0030 0010 0010 0010 0030 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0010 0ACD",
  "repeat": 1,
  "format": "pronto",
  "int_side": false,
  "int_top": false,
  "ext1": true,
  "ext2": false
}
```

## Development Features

New messages currently in development

### API Feature Flags

Feature flags contain new or optional features a client can use.
They also allow an easy way to keep backward compatibility.

Feature flags are encoded as bit fields in an integer value and returned in the `get_sysinfo` message,
and also in the `auth_required` message. Field name: `features`.

- Bit 0: support for disabling IR repeat response messages. Default: disabled.
- Bit 1: `ir_send` command supports the `hold` parameter to send ir command for x milliseconds.

#### Optimized IR Repeat Handling

New feature flag field in `ir_send` request message:
- Field: `f`, type number.
- Bit 0: do not send a response message if an active IR repeat sequence is extended.

This lowers processing overhead and allows sending `ir_send` repeat messages in shorter intervals.

### Sending an IR Code for a Specific Time

Some devices require pressing a button for a specific time to trigger a different action.
For example long pressing the power button will switch off the device.

This could be simulated with a proper repeat count. But this requires to know the IR protocol specific timings

The new optional `hold` field in the `ir_send` message allows to send the code for a specific time.  
Important: this time is the **minimal duration** the IR signal is sent and not the exact time the signal is stopped.
An IR message will always be completed. This means that the actual send time will mostly be longer.

Example request from the Core-API to send a Sony TV volume up command for 2 seconds using the external port 2:
```json
{
  "type": "dock",
  "command": "ir_send",
  "code": "4;0x490;12;0",
  "format": "hex",
  "hold": 2000,
  "int_side": false,
  "int_top": false,
  "ext1": false,
  "ext2": true
}
```

- The `hold` parameter will override the `repeat` field if present!
  - Total signal time: MAX(hold, "minimal IR signal duration")
  - Example: Sony (protocol 4) requires a minimum of 3 IR messages, each 45ms apart:
    - minimal signal duration = 135ms
    - any `hold` value < 135 is ignored and the IR signal will be active for 135ms
    - if `hold` is set to 150: the signal will be active for 180ms because 4 x 45ms = 180ms
- If using a `format: hex` message:
  - The repeat count in `code` is ignored.
  - The protocol specific minimal repeat count is still being used. For example Sony (protocol 4), uses a minimal repeat count of 2.

### External Port Operation Mode

Get operation mode:
```json
{
  "type": "dock",
  "id": 123,
  "command": "get_port_mode",
  "port": 1
}
```

Set operation mode:
```json
{
  "type": "dock",
  "id": 124,
  "command": "set_port_mode",
  "port": 1,
  "mode": "TRIGGER_5V"
}
```

Supported `mode` settings:
- `NOT_CONFIGURED`
- `IR_BLASTER`
- `IR_EMITTER_MONO`
- `IR_EMITTER_STEREO`
- `TRIGGER_5V`
- `RS232`

Default UART settings: 9600 8N1

For `mode: RS232`, the UART settings can be configured with additional fields:

```json
{
  "type": "dock",
  "command": "set_port_mode",
  "port": 1,
  "mode": "RS232",
  "baud_rate": 19200,
  "data_bits": 7,
  "parity": "even",
  "stop_bits": "1.5"
}
```

- `baud_rate`: 300 - 5000000
- `data_bits`: 5 - 8
- `parity`: `"none"` | `"even"` | `"odd"`
- `stop_bits`: `"1"` | `"1.5"` | `"2"`  ❗️ this must be a string!

### Trigger

Enable trigger (output high):
```json
{
  "type": "dock",
  "command": "set_port_trigger",
  "port": 1,
  "trigger": true
}
```

Disable trigger (output low):
```json
{
  "type": "dock",
  "command": "set_port_trigger",
  "port": 1,
  "trigger": false
}
```

Trigger impulse:
```json
{
  "type": "dock",
  "command": "set_port_trigger",
  "port": 1,
  "trigger": true,
  "duration": 2000
}
```

- `duration`: time in milliseconds to set output trigger high

### Log Message Events

Enable log message forwarding as WebSocket event messages:
```json
{
  "type": "dock",
  "command": "enable_log_events",
  "enable": true
}
```

Example log event message:
```json
{
    "type": "event",
    "msg": "log",
    "level": "I",
    "tag": "websrv",
    "ts": 89981,
    "log": "Set connection 38 authenticated: 1"
}
```

## RS232 Communication

Send data:
```json
{
  "type": "dock",
  "command": "send_serial",
  "port": 1,
  "data": "Hello RS232\n"
}
```

Enable serial data receive events:
```json
{
  "type": "dock",
  "command": "enable_serial_events",
  "port": 1,
  "enable": true
}
```

Serial data event:
```json
{
  "type": "event",
  "msg": "serial_data",
  "port": 1,
  "data": "<RECEIVED_DATA_AS_UTF8>"
}
```

Enable TCP serial server:
```json
{
  "type": "dock",
  "command": "set_serial_tcp",
  "enable": true
}
```

## Static Network Configuration

### Set DNS servers

```json
{
    "type": "dock",
    "id": 123,
    "command": "set_dns",
    "dns1": "1.1.1.1",
    "dns2": "8.8.8.8"
}
```

- DNS is a global configuration, not per interface.
- Too clear a DNS server: use an empty string.
- Manually configured DNS servers take priority over DHCP assigned servers.
- The dock will automatically reboot after changing the configuration. 

### Set a static IPv4 configuration

```json
{
    "type": "dock",
    "id": 123,
    "command": "set_network",
    "interface": "eth",
    "mode": "static",
    "ip": "192.168.16.88",
    "mask": "255.255.255.0",
    "gw": "192.168.16.1"
}
```

- Configuration is per `interface`: `eth` or `wifi`
- `mode`: `static` or `dhcp`

### Get network configuration

Request:
```json
{
    "type": "dock",
    "id": 123,
    "command": "get_network"
}
```

Response:
```json
{
    "req_id": 123,
    "type": "dock",
    "msg": "get_network",
    "eth": {
        "mode": "static",
        "ip": "192.168.16.88",
        "mask": "255.255.255.0",
        "gw": "192.168.16.1"
    },
    "wifi": {
        "mode": "dhcp"
    },
    "sntp_enabled": false,
    "sntp1": "pool.ntp.org",
    "active": {
        "interface": "eth",
        "ip": "192.168.16.88",
        "mask": "255.255.255.0",
        "gw": "192.168.16.1",
        "ipv6": {
            "addresses": [
                {
                    "address": "FE80::9270:69FF:FE8D:3063",
                    "type": "link_local"
                },
                {
                    "address": "FDE4:BE4C:EBA9:D140:9270:69FF:FE8D:3063",
                    "type": "unique_local"
                }
            ]
        }
    },
    "code": 200
}
```