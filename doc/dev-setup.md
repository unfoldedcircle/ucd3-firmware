# Developer Setup

- [Espressif IDF 5.3.1](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html)
- [Visual Studio Code](https://code.visualstudio.com/) with [ESP-IDF extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension)
- [clang-format](https://clang.llvm.org/docs/ClangFormat.html)

## Using Visual Studio Code

❗️ If the .vscode project settings folder is checked-in, it will contain user-specific and hard-coded project & tool paths!

- Projects have to be reconfigured with the to the user's environment.
- Use the VSCode IDF plugin configuration to setup the environment.
  - Command: Configure ESP-IDF Extension
  - Use existing setup / install desired version
- Delete the .vscode/settings.json file (or even the whole .vscode folder) and reconfigure from scratch if it's not working.
- See "Environment Setup" in https://learnesp32.com/videos/course-introduction/course-introduction

📢 Please let us know if there's a better way on how to work with project settings in VSCode. This seems not to be made for Git 🙁
E.g. the long standing issue to extend project settings: https://github.com/microsoft/vscode/issues/15909

## UART vs JTAG

This is a very confusing topic... When it works it works, but can cause hours of troubleshooting after IDF updates! At least on macOS, other environments might work better.

Recommended setting if debugger is not used:
- Set to UART (or "ESP USB bridge" when running "Set Espressif Target").
- Do NOT start the OpenOCD server
- Flashing appears to be faster than using JTAG, and the combined "Build, Flash and Monitor" command works

Debugging:
- Set to JTAG
- Start OpenOCD server

Troubleshooting:
- Run the "Set Espressif Target"
- Restart OpenOCD server
- Make sure OpenOCD server starts and stops without errors
- Unplug and replug ESP32 USB
- Reboot PC
- Start with a fresh .vscode/settings.json file
