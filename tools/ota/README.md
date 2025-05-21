# OTA Update & Server Deployment

## Release Notes

- OTA release notes must be written manually for each release.
- Release notes are stored in the directory [/doc/release](../../doc/release/).
  - Release notes are language specific and are intended for regular GitHub releases.
  - However, release notes are not bound to a specific version: the current file content will be included into the firmware release.
- File pattern: `release-notes_${LANG}.md`
  - `$LANG`: 2-character language identifier, e.g. `en`, `fr`, `de`, etc.

## Create a new OTA update

Required parameters:
- signed `/build/ucd3-firmware.bin` file from the IDF build
- board revision
- version

```bash
./create_ota_update.sh ../../build/ucd3-firmware.bin 1.1 0.8.0
```

The OTA update files will be written into subdirectory `./release`.

Requirements:
- Node.js
- [git-semver](https://github.com/mdomke/git-semver) 

## Upload an update to the OTA update server

Required parameters:
- board revision
- version

```bash
./upload-ota.sh 1.1 0.8.0
```

Requirements:
- Update files created from `create_ota_update.sh` in subdirectory `./release`.
- Node.js
- OTA server hostname in OTA_SERVER env variable
- SSH key configuration to access the OTA server
