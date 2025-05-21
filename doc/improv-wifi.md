# Improv Wi-Fi

The ESP32-S3 in UCD3 no longer supports classic BT with RFcomm.

Therefore, we are using the [Improv Wi-Fi BLE](https://www.improv-wifi.com/ble/) standard to set up the dock on a Wi-Fi network.

If the dock is connected to Ethernet, the Improv Wi-Fi service is inactive, and the dock can be configured directly with the web-configurator. The web-configurator supports Improv Wi-Fi with firmware version `v2.2.7` and newer.

The dock's Wi-Fi network can also be configured with a compatible web browser using the web-based [Improv via BLE](<https://www.improv-wifi.com/>). **Note:** Chrome on macOS has a known Bluetooth communication bug that prevents sending the Wi-Fi SSID and password with Improv Wi-Fi!

## Custom Extensions

The Improv Wi-Fi standard is limited to identifying a device and configuring a Wi-Fi network.
However, the Dock setup with the web-configurator also requires setting a friendly device name and an optional access token for the WebSocket API.

The `RPC Command` (characteristic `00467768-6228-2272-4663-277478268003`) has been enhanced with a new _Send device parameters_ command.

### Send device parameters

- **Command ID**: `200`
- A parameter consists of: length, type, optional data bytes.
- The format is based on the BLE advertisement, which allows to introduce new parameter types in the future, without breaking backward compatibility.
- Multiple parameters can be included as long as the total length of the command request is less than 256 bytes.

| **Byte** | **Description**    |
| -------- | ------------------ |
| 01       | command            |
| xx       | data length        |
| yy       | parameter n length |
| zz       | parameter n type   |
|          | parameter n bytes  |
| CS       | checksum           |

Defined parameters:

| **Value** | **Description**      | **UCD3 max length** |
| --------- | -------------------- | ------------------- |
| 0x01      | Friendly device name | 40                  |
| 0x02      | WS-API access token  | 40                  |


#### Example

```C
uint8_t data[] = {
    // command and data length
    200, 22,
    // FRIENDLY_NAME param
    11, 0x01, 'H', 'e', 'l', 'l', 'o', ' ', 't', 'e', 's', 't',
    // ACCESS_TOKEN param
    9, 0x02, '1', '2', '3', '4', '5', '6', '7', '8',
    // checksum
    10
};
```
