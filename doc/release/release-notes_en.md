## Release 0.10.5 - 2026-07-13

### Fixed
- Improved IR sending reliability and compatibility, especially for repeated commands, long button presses, and devices that are sensitive to timing. The Dock now also stops sending as soon as the controlling client disconnects ([#99](https://github.com/unfoldedcircle/ucd3-firmware/pull/99), [#100](https://github.com/unfoldedcircle/ucd3-firmware/pull/100), [#101](https://github.com/unfoldedcircle/ucd3-firmware/pull/101)).
- Fixed a crash that could happen when a client sends an invalid sign-in message ([#98](https://github.com/unfoldedcircle/ucd3-firmware/pull/98)).
- Fixed incorrect IP addresses shown in logs for some client connections.

### Changed
- The web interface now shows a warning before changing the access token, since connected clients need to sign in again afterward ([#96](https://github.com/unfoldedcircle/ucd3-firmware/pull/96)).
- After changing the access token, all connected clients are disconnected, so they have to reconnect with the new token ([#97](https://github.com/unfoldedcircle/ucd3-firmware/pull/97)).

## Release 0.10.4 - 2026-06-25
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
