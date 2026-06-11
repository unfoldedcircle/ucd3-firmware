# Unfolded Circle Dock 3 Firmware Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## Unreleased

_Changes in the next release_

---

## Release 0.10.0 - 2026-06-11

### Added
- Embedded web management UI ([#76](https://github.com/unfoldedcircle/ucd3-firmware/pull/76)).
- Static network configuration ([#81](https://github.com/unfoldedcircle/ucd3-firmware/pull/81)).
- PoE voltage mode configuration for rev6 ([#79](https://github.com/unfoldedcircle/ucd3-firmware/pull/79)).
- Log router with WebSocket streaming support ([#77](https://github.com/unfoldedcircle/ucd3-firmware/pull/77)).
- TCP/RS232 serial bridge with WebSocket API ([#72](https://github.com/unfoldedcircle/ucd3-firmware/pull/72)).
- Use gzip encoding for web server file serving ([#75](https://github.com/unfoldedcircle/ucd3-firmware/pull/75)).
- ETag support ([#73](https://github.com/unfoldedcircle/ucd3-firmware/pull/73)).
- Status web page: redirect paths without a trailing slash ([#71](https://github.com/unfoldedcircle/ucd3-firmware/pull/71)).

### Fixed
- Set correct mode in port_mode event message.
- IR hold time requires repeat parameter to be set.
- Add missing `get_serial_tcp` WS message.
- ExternalPort UART re-initialization ([#69](https://github.com/unfoldedcircle/ucd3-firmware/pull/69)).
- Align ADC calibration scheme usage.
- Proper InfraredService singleton creation.
- Add cast to iot_button_set_param to prevent compiler warning.

### Changed
- Update documentation, add README links in components.
- Replace sync log-router callbacks with queue and sender task ([#82](https://github.com/unfoldedcircle/ucd3-firmware/pull/82)).
- Add error logging in Preferences.
- Network state machine event handling improvements ([#68](https://github.com/unfoldedcircle/ucd3-firmware/pull/68)).
- Update managed components ([#74](https://github.com/unfoldedcircle/ucd3-firmware/pull/74)).
- Do not block timer tasks ([#70](https://github.com/unfoldedcircle/ucd3-firmware/pull/70)).
- Unify firmware revisions 4 and 6 ([#66](https://github.com/unfoldedcircle/ucd3-firmware/pull/66)).
- Update guidelines and add AGENTS.md ([#83](https://github.com/unfoldedcircle/ucd3-firmware/pull/83)).

## Release 0.9.1 - 2026-05-18
### Fixed
- IR code validation fails for data value 0 ([#62](https://github.com/unfoldedcircle/ucd3-firmware/issues/62)).
- OTA image build now supports multiple revisions. No more manual interventions required ([#64](https://github.com/unfoldedcircle/ucd3-firmware/pull/64)).

## Release 0.9.0 - 2026-04-28
### Fixed
- Double allocation of eth_handle ([#53](https://github.com/unfoldedcircle/ucd3-firmware/pull/53)).
- Memory leak in BLE device name advertisement ([#54](https://github.com/unfoldedcircle/ucd3-firmware/pull/54)).
- Memory leak in WebSocket send, buffer under-read in file ext check ([#55](https://github.com/unfoldedcircle/ucd3-firmware/pull/55)).
- Semaphore and socket leak in GlobalCache server ([#56](https://github.com/unfoldedcircle/ucd3-firmware/pull/56)).

### Changed
- Update GitHub actions, clean up ([#57](https://github.com/unfoldedcircle/ucd3-firmware/pull/57)).
- Enable TCP keep alive and connection lru purge ([#58](https://github.com/unfoldedcircle/ucd3-firmware/pull/58)).
- Disconnect unauthenticated WS clients after 30s ([#59](https://github.com/unfoldedcircle/ucd3-firmware/pull/59)).
- Increase max sockets from 10 to 32 ([#60](https://github.com/unfoldedcircle/ucd3-firmware/pull/60)).

## Beta Release 0.8.2 - 2026-02-14
### Fixed
- Improv-wifi RPC response length and checksum calculation ([#47](https://github.com/unfoldedcircle/ucd3-firmware/pull/47)).
- Correct initial state for inverted IR emitters ([#48](https://github.com/unfoldedcircle/ucd3-firmware/pull/48)).
- Use free() instead of delete for cJSON strings ([#49](https://github.com/unfoldedcircle/ucd3-firmware/pull/49)).

### Added
- Pinout diagram ([#45](https://github.com/unfoldedcircle/ucd3-firmware/pull/45)).
- Hardware revision 6 support ([#51](https://github.com/unfoldedcircle/ucd3-firmware/pull/51)).

### Changed
- SNTP initialization, not displaying an invalid date in the status web-page by @Tosko4 in ([#46](https://github.com/unfoldedcircle/ucd3-firmware/pull/46)).
- Update GitHub build actions ([#50](https://github.com/unfoldedcircle/ucd3-firmware/pull/50)).
    
## Beta Release 0.8.1 - 2025-11-03
### Changed
- Increase of overcurrent limitation to 2000mA ([#41](https://github.com/unfoldedcircle/ucd3-firmware/pull/41)).

## Beta Release 0.8.0 - 2025-09-23
### Fixed
- Delay function used for sending IR codes was inaccurate for delays greater than 16ms, causing issues for certain IR protocols and native IR repeats ([#34](https://github.com/unfoldedcircle/ucd3-firmware/pull/34), [feature-and-bug-tracker#484](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/484)).
- Stop active IR repeat if WebSocket client disconnects.
- Memory leak in async ir_send responses.

### Added
- Optional system runtime statistics ([#28](https://github.com/unfoldedcircle/ucd3-firmware/pull/28)).
- Status led patterns for dock setup, IR learning and OTA ([#37](https://github.com/unfoldedcircle/ucd3-firmware/pull/37)).
- Sending an IR code for a specific time.

### Changed
- Support higher modulation frequencies and better PWM precision for sending IR ([#29](https://github.com/unfoldedcircle/ucd3-firmware/pull/29)).
- Enhance IR repeat functionality ([#35](https://github.com/unfoldedcircle/ucd3-firmware/pull/35)).
- Do not enter WiFi setup mode if connection setup failed ([feature-and-bug-tracker#612](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/612)).

## Beta Release 0.7.1 - 2025-08-28
### Added
- Documentation for firmware flashing with esptool and web-serial ([#26](https://github.com/unfoldedcircle/ucd3-firmware/pull/26), [#27](https://github.com/unfoldedcircle/ucd3-firmware/pull/27)).
### Changed
- Increase of overcurrent limitation to 1850mA ([#30](https://github.com/unfoldedcircle/ucd3-firmware/pull/30)).

## Beta Release 0.7.0 - 2025-07-17
### Fixed
- External port detection with two blasters.
- Show error on screen for over-current detection ([#17](https://github.com/unfoldedcircle/ucd3-firmware/issues/17)).
- PRONTO code parsing with trailing whitespace ([#13](https://github.com/unfoldedcircle/ucd3-firmware/pull/13)).
- Send authentication error message response to allow setup with a custom password on Remotes <= 2.6.1 ([#16](https://github.com/unfoldedcircle/ucd3-firmware/pull/16)).
- Show correct charging changes in info screen ([#6](https://github.com/unfoldedcircle/ucd3-firmware/pull/6)).
- Invalid ethernet connection state in get_sysinfo ([#12](https://github.com/unfoldedcircle/ucd3-firmware/pull/12)).
- Memory leak in failed ota authentication ([#14](https://github.com/unfoldedcircle/ucd3-firmware/pull/14)).
- Use after free for non-raw PRONTO code error message.

### Added
- Low voltage charger check ([#18](https://github.com/unfoldedcircle/ucd3-firmware/issues/18)).

### Changed
- Improve ADC reading and allow higher charging current to compensate different voltage threshold on production docks.
- Disable IR decoding of AC protocols ([#7](https://github.com/unfoldedcircle/ucd3-firmware/pull/7)).
- Use both internal IR outputs if no active output has been specified.
- Determine vcc charger offset at startup to prevent toggling charging / non charging screens.

## Beta Release 0.6.0 - 2025-06-02
### Changed
- Improve auto-detection of external IR-peripherals, including the Dock Two mono-plug IR-emitter.
- OSS release: squashed and cleaned-up project for public release.
  - This is the v0.6.0 firmware flashed on the first shipped Dock 3.
  - Future development will continue in this repository.
