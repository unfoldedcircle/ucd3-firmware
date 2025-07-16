# Unfolded Circle Dock 3 Firmware Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## Unreleased

_Changes in the next release_

---

## Beta Release 0.7.0
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

## Beta Release 0.6.0
### Changed
- Improve auto-detection of external IR-peripherals, including the Dock Two mono-plug IR-emitter.
- OSS release: squashed and cleaned-up project for public release.
  - This is the v0.6.0 firmware flashed on the first shipped Dock 3.
  - Future development will continue in this repository.
