# ESP eFuses

## Layout BLOCK3

- 1 byte version field: `0x01`
- 8 byte serial number string ('123456XY')
- 7 byte model number string ('UCD3')
- 3 bytes hw revision string ('4')
- 1 byte hw feature bit array:
  - Bit 0: PoE
  - Bit 1: charging dock
- 5 bytes empty. For future use.

Note: `MAC_CUSTOM` starts at bit 200. This _should_ be usable for other things, as long as `esp_read_mac(mac, ESP_MAC_EFUSE_CUSTOM)` isn't used.

## Testing with virtual device

Since blowing efuses is permanent, use the virtual device mode first to make sure everything is ok.

- The `virt.bin` file is automatically created. To start from scratch, simply delete the file.
- The `device.bin` is a binary file to write. Use Hex Fiend tool on Mac or similar to create test data.
  - It's best that this file is exactly 32 bytes long, so it matches the complete BLOCK3. Otherwise use `--offset` parameter.

```
espefuse.py -c esp32s3 --virt --path-efuse-file virt.bin burn_block_data BLOCK3 device.bin
espefuse.py v4.8.1

=== Run "burn_block_data" command ===
[03] BLOCK3               size=32 bytes, offset=00 - > [01 31 32 33 34 35 36 58 59 55 43 44 33 00 00 00 34 00 00 03 00 00 00 00 00 00 00 00 00 00 00 00].

Check all blocks for burn...
idx, BLOCK_NAME,          Conclusion
[03] BLOCK3               is empty, will burn the new value
. 
This is an irreversible operation!
Type 'BURN' (all capitals) to continue.
BURN
BURN BLOCK3  - OK (write block == read block)
Reading updated efuses...
Successful
```

Dump efuses:
```shell
espefuse.py -c esp32s3 --virt --path-efuse-file virt.bin dump
```

Get device summary:
```shell
espefuse.py -c esp32s3 --virt --path-efuse-file virt.bin summary
```

## Dump efuse

```shell
espefuse.py --port $SERIAL_DEV dump
```

## Write efuse

‼️ Test with a virtual device first before writing efuses in a real device!  
‼️ Do not use individual `burn_bit` calls: `BLOCK3` can only be written once due to Reed-Solomon encoding in newer ESP32 models!  
See: <https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/efuse.html#supported-coding-scheme>

Real device:
```shell
espefuse.py --port $SERIAL_DEV burn_block_data --offset 0 BLOCK3 file.bin
```

## Seal the deal

Write protect the user data block to prevent force-writes leading to a corrupt data due to RS encoding:

```shell
espefuse.py --port $SERIAL_DEV write_protect_efuse USER_DATA
```

⚠️ Write protect all system configuration flags:
```shell
espefuse.py --port $SERIAL_DEV burn_efuse WR_DIS 0xFFFFFFFF
```

## Resources

- https://docs.espressif.com/projects/esptool/en/latest/esp32s3/espefuse/index.html
- https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/efuse.html
