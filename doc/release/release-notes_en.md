## Beta Release 0.8.2
### Fixed
- Correct initial state for IR LEDs so that they do not light up until an IR command is sent ([#48](https://github.com/unfoldedcircle/ucd3-firmware/pull/48)).
- General fixes and improvements.

### Changed
- SNTP initialization, not displaying an invalid date in the status web-page. Contributed by @Tosko4, thanks! ([#46](https://github.com/unfoldedcircle/ucd3-firmware/pull/46)).

## Beta Release 0.8.1
### Changed
- Increase of overcurrent limitation to 2000mA.

## Beta Release 0.8.0
### Fixed
- Delay function used for sending IR codes was inaccurate for delays greater than 16ms, causing issues for certain IR protocols and native IR repeats ([#484](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/484)).
- Stop active IR repeat if the WebSocket client disconnects.
- General stability improvements.

### Added
- Status led patterns for dock setup, IR learning and OTA.

### Changed
- Not entering WiFi setup mode if connection setup failed ([#612](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/612)).

## Beta Release 0.7.1
### Changed
- Increase of overcurrent limitation to 1850mA ([#30](https://github.com/unfoldedcircle/ucd3-firmware/pull/30)).

## Beta Release 0.7.0
### Bug Fixes
- External port detection with two blasters.
- Show charger shut off error on screen caused by over-current detection ([#501](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/501)).
- PRONTO code parsing with trailing whitespace ([#495](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/495)).
- Setup with a custom password on Remotes runnig firmware version <= 2.6.1 ([#489](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/489)).
- Return correct network connection type (Ethernet or WiFi).
- Show correct charging information in info screen when pressing the control button.
- General stability improvements.

### New Features
- Low voltage charger check.

### Changed
- Improve charging current measurement and over-current detection to prevent charging shutdowns ([#501](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/501)).
- Determination of the charging voltage offset at startup to prevent switching between the charging and non-charging screens.

## Beta Release 0.6.0
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
