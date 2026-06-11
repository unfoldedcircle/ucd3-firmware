# Repository Guidelines for Agents

This file is for AI coding agents and human contributors working on Dock 3 (ESP32‑S3, ESP‑IDF 5.4).  
Humans should also read `README.md` and `CONTRIBUTING.md`.

## Critical Rules & Boundaries

### Always

- Keep changes small, focused, and traceable to a single feature, bug, or explicit request.
- Match the existing code style and follow `doc/code-guidelines.md`.
- Run formatting and host unit tests before opening a pull request.

### Ask First

- Changing OTA/signing behavior or any files related to firmware update and image signing.
- Changing board pin mappings or hardware‑specific configuration (e.g. files in `components/preferences/`).
- Large refactors, cross‑cutting changes, or introducing new external dependencies.

### Never

- Modify vendor or managed code:
  - `components/IRremoteESP8266/**`  
    (Git submodule of a forked and customized https://github.com/crankyoldgit/IRremoteESP8266 project.)
  - `managed_components/**`
- Modify generated state machine code. These files are generated with StateSmith from the `.drawio` diagrams and must **not** be edited by hand:
  - `components/display/src/DisplaySm.cpp` / `DisplaySm.h`
  - `components/network/src/NetworkSm.cpp` / `NetworkSm.h`
- Introduce Arduino/PlatformIO libraries or unapproved third‑party components.
- Commit `sdkconfig`, build directories, or other local build artifacts.

---

## Project Overview

UCD3‑firmware is the ESP‑IDF 5.4 firmware for the Unfolded Circle Dock 3 (UCD3).  
It runs on an ESP32‑S3 and integrates these peripherals:

- Wi‑Fi and Ethernet connectivity (only one interface is active at a time).
- BLE for Wi‑Fi provisioning using Improv‑WiFi.
- Small monochrome OLED screen for status information using LVGL 8.4.
- Control button to cycle through UI screens and confirm actions.
- Two 3.5 mm jack ports for external peripherals: IR blaster, IR extender, RS232, trigger.
- Charging port for the Remote 3.
- Internal IR LEDs for sending and learning.
- Speaker and microphone (currently unused).

---

## Project Structure & Module Organization

- `main/`  
  Application entry point (`main.cpp`), `Kconfig.projbuild`, `idf_component.yml` manifest.

- `components/`  
  Reusable modules, e.g.:
  - `adc/`, `ble/`, `common/`, `display/`, `external_port/`, `improv_wifi/`, `infrared/`, `led/`,
    `log_router/`, `network/`, `preferences/`, `serial_bridge/`, `webserver/`.
  - Each component keeps its own `CMakeLists.txt` and sources.
  - Public headers should be under `<component>/include/`.
  - Internal headers should live under the component root.
  - `IRremoteESP8266` is a Git submodule (see "Never" above).

- `managed_components/`  
  Auto‑fetched ESP‑IDF dependencies, committed for faster CI builds. Treat as read‑only.

- `doc/`  
  Documentation.
  - `doc/release/`: manually written user release notes for OTA updates.
  - Other docs cover OTA, flashing, web UI, and component‑specific behavior (see "Other Documentation").

- `test/`  
  Host‑based unit tests using GoogleTest (no ESP‑IDF dependency). Only "business logic" (e.g. infrared conversions) is tested; mocks and library definitions live in `test/mocks/`.

- `tools/`  
  Build helpers and developer test scripts.

- `webroot/`  
  Embedded management web app.

- Repository root  
  `CMakeLists.txt`, `CheckGit.cmake`, `dependencies.lock`, `frogfs.yaml`, `code_style.sh`, and other top‑level configuration.  
  The `sdkconfig` file is **not** checked in; it is generated from:
  - `sdkconfig.defaults`
  - `sdkconfig.defaults.esp32s3`
  - The model‑specific `sdkconfig.rev4` or `sdkconfig.rev6`

---

## Commit & Pull Request Guidelines

- Commits
  - Small and focused. Subject ≤ ~72 characters, body lines ≤ ~80 characters.
  - Prefer Conventional Commit prefixes: `feat:`, `fix:`, `docs:`, `refactor:`, `chore:`, etc.

- Branches
  - Use prefixes such as `feature/...`, `fix/...`, `chore/...`, `refactor/...`.

- Pull requests
  - Provide a clear description and link related issues.
  - See "Pull Request Best Practices" in `CONTRIBUTING.md` for full details.

---

## General Engineering Rules

Follow `doc/code-guidelines.md` for naming, formatting, and other style conventions.

### Think Before Coding

**Do not assume. Do not hide confusion. Surface tradeoffs.**

Before implementing:

- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them; do not pick silently.
- If a simpler approach exists, say so and propose it.
- If something is unclear, stop, name what is confusing, and ask.

### Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was requested.
- No abstractions for single‑use code.
- No "flexibility" or "configurability" that was not requested.
- If you wrote 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is over‑complicated?" If yes, simplify.

### Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:

- Do not "improve" adjacent code, comments, or formatting.
- Do not refactor things that are not broken.
- Match the existing style, even if you would do it differently.
- If you notice unrelated dead code, mention it in the PR; do not delete it unless asked.

When your changes create orphans:

- Remove imports, variables, or functions that **your** changes made unused.
- Do not remove pre‑existing dead code unless requested.

The test: every changed line should trace directly to the user’s request.

---

## Build Environment

Before opening a PR, run unit tests and code formatting locally; CI assumes this passes  
(see `.github/workflows/code_guidelines.yml`).

### Building

After cloning the repository, update the Git submodules:

```shell
git submodule update --init --recursive
```

Building requires a signing key. The production signing key is only available in the CI environment.

If the build environment does not contain `ucd3_firmware_signing_key.pem` in the repository root:

- For a real image that will be flashed onto a device: ask the user to provide an appropriate key.
- For local build / compile‑only testing: you may generate a development key:

```shell
espsecure.py generate_signing_key --version 2 --scheme rsa3072 ucd3_firmware_signing_key.pem
```

For a clean build, delete the generated `sdkconfig` file and the `build` directory in the root.

Typical development build:

```shell
IDF_TARGET=esp32s3 \
  SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.rev6 \
  IDF_COMPONENT_CHECK_NEW_VERSION=0 \
  idf.py build
```

Adjust `SDKCONFIG_DEFAULTS` if you need a different hardware revision (e.g. `sdkconfig.rev4`).

### Flashing

For manual flashing using Espressif `esptool`, see `doc/flash-esptool.md`.

To upload a locally built firmware to a running dock that has a compatible signing key:

```shell
curl --user "admin:$TOKEN" --data-binary "@./build/ucd3-firmware.bin" "http://${DOCK_IP}/update"
```

Refer to the main README and OTA documentation for signing and warranty constraints before changing OTA behavior.

---

## Source Code Format

Before committing, run the `code_style.sh` formatting script.  
Run it inside a Python 3 virtual environment.

Example setup:

```shell
python -m pip install clang-format==20.1.8
./code_style.sh
```

Use the `--check` parameter to verify formatting without modifying files:

```shell
./code_style.sh --check
```

---

## Testing

Host‑based unit tests use [GoogleTest](https://google.github.io/googletest/).

- Platform‑independent: does not use any ESP‑IDF functionality.
- Tests "business logic" functions, such as infrared conversions.
- All required mocks and library definitions are in the `test/mocks/` subdirectory.

Build tests:

```shell
cd test
cmake -S . -B build
cmake --build build
```

Run tests from inside the `test/` directory:

```shell
./build/common/common
./build/infrared/infrared
./build/improv_wifi/improv_wifi
./build/preferences/preferences
```

---

## Other Documentation

- Project README: `README.md`
- Contributor guidelines: `CONTRIBUTING.md`
- Code guidelines: `doc/code-guidelines.md`
- New WebSocket messages not yet in AsyncAPI documentation: `doc/websocket-api.md`

Component / feature‑specific documentation (keep in sync with code changes):

- Display state machine (source of truth for generated code):  
  `components/display/DisplaySm.drawio`
- Network state machine (source of truth for generated code):  
  `components/network/NetworkSm.drawio`
- Log router component:  
  `doc/log-router.md`
- Serial bridge component:  
  `doc/serial-bridge.md`
- Web management UI:  
  `doc/web-management-ui.md`
