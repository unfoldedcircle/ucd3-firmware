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
