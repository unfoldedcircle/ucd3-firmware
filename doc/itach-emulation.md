# iTach API emulation

The firmware contains an optional emulation of the iTach API for sending IR commands.

This allows 3rd party tools to use the Dock as IR emitter, e.g. the [Home Assistant Global Cache integration](https://www.home-assistant.io/integrations/itach). Only a subset of commands are implemented, and it does not work with all tools expecting a specific device model or API calls.

If the API emulation is enabled, a TCP server is started on port 4998. Telnet can be used for testing commands.

## Enable API emulation

Use the WebSocket Dock-API to change settings:

```json
{
  "type": "dock",
  "id": 1,
  "command": "set_ir_config",
  "itach_emulation": true,
  "itach_beacon": true
}
```

‼️ Changing `itach_emulation` or `itach_beacon` settings will automatically reboot the dock for the changes to take effect.

The `get_ir_config` command can be used to get the current settings.

## Supported Commands

The following commands are supported in the API emulation. A command must be terminated with a carriage return `\r`.

See iTach API specification for detailed information about the commands.

### sendir

Send an IR command.

Format: `sendir,1:$PORT,$ID,$FREQUENCY,$REPEAT,$OFFSET,$DATA`

- `1:$PORT`is the connector address. Prefix `1:` is static, whereas `$PORT` indicates the IR output mask:
  - `1`: internal side IR
  - `2`: external port 1
  - `4`: external port 2
  - `8`: internal top IR
- `$ID`: command number, returned in response
- `$FREQUENCY,$REPEAT,$OFFSET,$DATA`: see iTach API specification

Example for sending an IR command on both internal IR outputs with a repeat count of 2:
```
sendir,1:9,1,37010,2,1,128,64,16,16,16,16,16,48,16,16,16,48,16,16,16,48,16,16,16,16,16,48,16,16,16,16,16,48,16,48,16,16,16,16,16,16,16,16,16,16,16,16,16,48,16,16,16,48,16,16,16,48,16,16,16,16,16,16,16,48,16,16,16,48,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,48,16,16,16,48,16,16,16,16,16,16,16,16,16,16,16,48,16,16,16,2765
```

### stopir

Abort an active IR send command.

### getversion

Returns the firmware version. 

‼️ Dots `.` and plus `+` characters are replaced with a dash `-`.

### getmac

Returns the MAC address.

### blink

Identifies the device with an LCD message / LED blinking.

### get_IRL

Starts IR learning.

‼️ Learned IR codes are **not** returned.

### stop_IRL

Stops IR learning.
