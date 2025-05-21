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
  "ext2": true,
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
  "baud_rate": 19200
  "data_bits": 7
  "parity": "even"
  "stop_bits": "1.5"
}
```

- `baud_rate`: 300 - 5000000
- `data_bits`: 5 - 8
- `parity`: `"none"` | `"even"` | `"odd"`
- `stop_bits`: `"1"` | `"1.5"` | `"2"`  ❗️ this must be a string!

### Trigger

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

