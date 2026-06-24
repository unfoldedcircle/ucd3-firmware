## Release 0.10.3 - 2026-06-24
Changes since the last public release, 0.8.2.

### Fixed
- Improved overall stability and automatic cleanup of stuck client connections.
- Fixed support for IR codes with a data value of `0`, such as the digit “0” on Philips TVs using the RC6 protocol ([bug-tracker#721](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/721)).
- Improved Ethernet connection reliability ([#89](https://github.com/unfoldedcircle/ucd3-firmware/pull/89)).
- UART settings can now be changed without restarting the Dock ([#84](https://github.com/unfoldedcircle/ucd3-firmware/pull/84)).

### Added
- Added an embedded web management interface for configuring and managing the Dock from a browser ([#76](https://github.com/unfoldedcircle/ucd3-firmware/pull/76)).
- Added support for static network configuration, allowing manual IP address settings ([#81](https://github.com/unfoldedcircle/ucd3-firmware/pull/81)).
- Added RS232 support with an optional TCP serial bridge ([#72](https://github.com/unfoldedcircle/ucd3-firmware/pull/72)).
  - On current devices, RS232 mode is limited to port 2. Port 1 may send startup output that some connected RS232 devices could interpret as commands.
  - UART TTL adapters are not supported because they use different signal levels.
- Added a new log router to improve diagnostics and troubleshooting ([#77](https://github.com/unfoldedcircle/ucd3-firmware/pull/77)).
- Added support for learning and sending RAW IR commands ([#85](https://github.com/unfoldedcircle/ucd3-firmware/pull/85), [#87](https://github.com/unfoldedcircle/ucd3-firmware/pull/87)).

### Changed
- If the maximum number of client connections is reached, the least active connection is now closed automatically to allow a new connection ([#58](https://github.com/unfoldedcircle/ucd3-firmware/pull/58)).
- Clients that do not authenticate are now disconnected after 30 seconds ([#59](https://github.com/unfoldedcircle/ucd3-firmware/pull/59)).
- Increased the maximum number of client connections from 7 to 18 ([#60](https://github.com/unfoldedcircle/ucd3-firmware/pull/60)).
- Unified support for firmware revisions 4 and 6 ([#66](https://github.com/unfoldedcircle/ucd3-firmware/pull/66)).

## Beta Release 0.8.2 - 2026-02-14
### Fixed
- Correct initial state for IR LEDs so that they do not light up until an IR command is sent ([#48](https://github.com/unfoldedcircle/ucd3-firmware/pull/48)).
- General fixes and improvements.

### Changed
- SNTP initialization, not displaying an invalid date in the status web-page. Contributed by @Tosko4, thanks! ([#46](https://github.com/unfoldedcircle/ucd3-firmware/pull/46)).

## Beta Release 0.8.1 - 2025-11-03
### Changed
- Increase of overcurrent limitation to 2000mA.

## Beta Release 0.8.0 - 2025-09-23
### Fixed
- Delay function used for sending IR codes was inaccurate for delays greater than 16ms, causing issues for certain IR protocols and native IR repeats ([bug-tracker#484](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/484)).
- Stop active IR repeat if the WebSocket client disconnects.
- General stability improvements.

### Added
- Status led patterns for dock setup, IR learning and OTA.

### Changed
- Not entering WiFi setup mode if connection setup failed ([bug-tracker#612](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/612)).

## Beta Release 0.7.1 - 2025-08-28
### Changed
- Increase of overcurrent limitation to 1850mA ([#30](https://github.com/unfoldedcircle/ucd3-firmware/pull/30)).

## Beta Release 0.7.0 - 2025-07-17
### Bug Fixes
- External port detection with two blasters.
- Show charger shut off error on screen caused by over-current detection ([bug-tracker#501](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/501)).
- PRONTO code parsing with trailing whitespace ([bug-tracker#495](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/495)).
- Setup with a custom password on Remotes runnig firmware version <= 2.6.1 ([bug-tracker#489](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/489)).
- Return correct network connection type (Ethernet or WiFi).
- Show correct charging information in info screen when pressing the control button.
- General stability improvements.

### New Features
- Low voltage charger check.

### Changed
- Improve charging current measurement and over-current detection to prevent charging shutdowns ([#501](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/501)).
- Determination of the charging voltage offset at startup to prevent switching between the charging and non-charging screens.

## Beta Release 0.6.0 - 2025-06-02
### Bug Fixes
- Switch external ports 1 & 2 according to booklet: port 1 is next to ethernet port.
- Startup crash in network check if other initializations take longer than expected.
- Crash while cycling through info screens with dock button.
- Propagate IR-send status code to client, e.g. if sending is not possible when IR learning is active.

### New Features
- IR-Blaster & -Emitter auto detection.
- Show learned IR protocol name in display.

### Changed
- Improve auto-detection of external IR-peripherals, including the Dock Two mono-plug IR-emitter.
- Info screen order and layout, combine network information into one screen.
