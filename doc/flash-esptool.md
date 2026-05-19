# Manually Flashing the Dock 3 with esptool

This article describes how a Dock 3 can be manually flashed with an official firmware from Unfolded Circle from a PC using the Espressif `esptool` in the command line.

Please note that manual flashing is usually not required, except if a custom firmware like ESP Home was used and one would like to restore the original software.


## Setup esptool

1. Download the latest release of Espressif's `esptool` for your operating system and system architecture from the assets section: https://github.com/espressif/esptool/releases
   - If you have an Intel or AMD processor: use the `amd64` version for your OS
   - Newer Apple machines with M1 or newer processor: use the `macos-arm64` version
2. Unpack the downloaded archive file

## Download Dock Firmware

1. Download the Dock 3 firmware release file from [https://github.com/unfoldedcircle/ucd3-firmware/releases](https://github.com/unfoldedcircle/ucd3-firmware/releases)
    1. Choose a release, usually the latest version
    2. Download the attached firmware file under assets: UCD3-firmware_r4-v\<VERSION>.tar.gz
2. Extract the downloaded archive and copy all `.bin` files into the extracted `esptool` directory.
    - Note: the `ucd3-firmware-unsigned.bin` firmware file needs to be flashed with the esptool, the `ucd3-firmware.bin` is the signed firmware for OTA updates.


## Flash Firmware with esptool

1. Connect the Dock 3 with a USB cable to your PC.  
   > Please make sure that you only connect one dock to your PC and no other gadgets besides mouse and keyboard to avoid flashing a different device.
2. Open a terminal and change into the extracted `esptool` directory.
3. Optional, but recommended: erase all data:

```shell
./esptool --chip esp32s3 --before default-reset erase-flash
```
  - The serial port should be automatically detected. Otherwise use the `--port` parameter to specify the serial port.

4. Run the following command to flash the complete firmware:
   
```shell
./esptool --chip esp32s3 --before default-reset write-flash \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0x8d000 ota_data_initial.bin \
  0x90000 ucd3-firmware-unsigned.bin
```

  - The output should look similar to:
```
esptool v5.0.1
Connected to ESP32-S3 on /dev/cu.usbmodem101:
Chip type:          ESP32-S3 (QFN56) (revision v0.2)
Features:           Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 2MB (AP_3v3)
Crystal frequency:  40MHz
USB mode:           USB-Serial/JTAG
MAC:                28:37:2f:0b:c4:3c

Stub flasher running.

Configuring flash size...
Flash will be erased from 0x00000000 to 0x00005fff...
Flash will be erased from 0x00008000 to 0x00008fff...
Flash will be erased from 0x0008d000 to 0x0008efff...
Flash will be erased from 0x00090000 to 0x00260fff...
Wrote 21056 bytes (13409 compressed) at 0x00000000 in 0.2 seconds (706.4 kbit/s).
Hash of data verified.
Wrote 3072 bytes (158 compressed) at 0x00008000 in 0.0 seconds (1019.8 kbit/s).
Hash of data verified.
Wrote 8192 bytes (31 compressed) at 0x0008d000 in 0.0 seconds (1344.5 kbit/s).
Hash of data verified.
Wrote 1904640 bytes (1076372 compressed) at 0x00090000 in 11.8 seconds (1286.1 kbit/s).
Hash of data verified.

Hard resetting via RTS pin...
```

5. Unplug and replug the USB cable. The dock will not automatically restart after flashing!
6. Done!
