# OTA Firmware Images

The OTA firmware update process is using the IDF API: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/ota.html
Firmware files are uploaded to the dock, either by the UC Remote, or manually by the user with a http client or web-browser.
The dock is passive and not checking for updates or downloading firmware image files.

Firmware images are signed to secure the update process. Only official firmware images can be used for OTA updates.
However, the bootloader is not locked and not using secure boot.
This allows to flash custom firmware over USB, for example by using [ESPHome](https://esphome.io/).
More information:
https://docs.espressif.com/projects/esp-idf/en/stable/esp32/security/secure-boot-v2.html#signed-app-verification-without-hardware-secure-boot

## Image Signing

1. Put RSA 3072 private signing key in `ucd3_firmware_signing_key.pem`
  - ‼️ The private key may not be checked in!
2. Run IDF build
3. The app image will automatically be signed and the public key embedded.

### Disable Image Signing

Disable `CONFIG_SECURE_SIGNED_ON_UPDATE`, or remove all `CONFIG_SECURE_*` options in `sdkconfig.defaults`.

### Key Handling

Create private key:
```shell
espsecure.py generate_signing_key --version 2 --scheme rsa3072 ucd3_firmware_signing_key.pem
```

This _should_ be equivilent to: `openssl genrsa -out ucd3_firmware_signing_key.pem 3072`

Extract public key from private key:
```shell
espsecure.py extract_public_key -v 2 --keyfile  ucd3_firmware_signing_key.pem public.pem 
```

View public key:
```shell
openssl rsa -pubin -in public.pem -text
```

View private key:
```shell
openssl rsa -in ucd3_firmware_signing_key.pem -text -noout
```

## Upload

```shell
curl  --user "admin:$TOKEN" --data-binary "@./build/ucd3-firmware.bin" http://${DOCK_IP}/update
```
