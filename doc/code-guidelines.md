# Code Guidelines

Writing C++ code is prefered but not mandatory. Use C or C++ where it makes sense.

Use [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) for writing C++ code with the following
exceptions:
  - indentation: 4 spaces (instead of 2)
  - maximum line length: 120 (instead of 80)
  - `lowerCamelCase` method names in classes (instead of `UpperCamelCase`)
  - `snake_case` function names outside classes
  - `.cpp` for implementation files (instead of `.cc`)

The most important aspect is to use consistency, not following the coding guidelines to the letter.
- If in doubt: consult the Google style guide.
- If the existing code violates the style guide:  
  Either suggest a refactoring or adding a new exception to the code guidelines :-)

❗️ Do not use C++ exception handling. Not because of the Google C++ style guide, but because it is [disabled by default in IDF](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/cplusplus.html#exception-handling).

## 3rd Party Libraries

The use of 3rd party libraries is encouraged if certain functionality is not provided by the IDF framework.

- Check IDF framework and IDF components first if it really requires an external library.
  - Prefer IDF over external libraries, unless there's a justified reason to do otherwise.
  - The Unfolded Circle core team needs to approve any external library.  
    This also applies to external libraries in the IDF component registry.
  - The decision needs to be documented in a GitHub issue or pull request.
- Verify that the license is compatible with the project.
- ‼️ Using the Arduino wrapper or PlatformIO libraries are not allowed.

## Source Code Formatter

A [ClangFormat](https://clang.llvm.org/docs/ClangFormat.html) configuration has been included in the project: [.clang-format](../.clang-format).

Helper script to check all project sources:
```shell
./code_style.sh --check
```

Helper script to auto-format all project sources:
```shell
./code_style.sh
```

❗️ The source code format is verified with a GitHub build action.

Excluded libs:

- [components/IRremoteESP8266/](../components/IRremoteESP8266/): 3rd party code

### Visual Studio Code Integration

Install the [Clang-Format Visual Studio Code extension](https://marketplace.visualstudio.com/items?itemName=xaver.clang-format).

To automatically format a file on save, add the following to your `.vscode/settings.json` file:
```json
{
    "editor.formatOnSave": true
}
```

## Code Analysis

TODO use Clang-Tidy

- https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/tools/idf-clang-tidy.html
- Not working in IDF 5.3.1: https://github.com/espressif/esp-idf/issues/14913

## Summary

A short summary of the most important points:

### Naming Conventions

- Variables: `snake_case`, e.g `next_value`.
- Function names: `snake_case`, e.g. `timer_callback`.
- Method names: `CamelCase`, e.g. `DoSomething`.
- Class names: `lowerCamelCase`, e.g. `MyClass`.
- Constants: `kCamelCase`, e.g. `kMaxValue`.
- C++ implementation files: `.cpp`

### Header Files

- Header file extension: `.h`
- Add copyright and SPDX license identifier:
  - The project is released under GPLv3 or later.
  - Some included components use a different license. Please check source files and corresponding readme file.
  - Also add copyright header to implementation file.
  - When adding functionality to an existing file, an additional `SPDX-FileCopyrightText:` line with the author's name
    can be added.

```c
// SPDX-FileCopyrightText: Copyright (c) $YEAR $AUTHOR $CONTACT
//
// SPDX-License-Identifier: GPL-3.0-or-later
```

- Include guards: use `#pragma once`
- C header files: add `extern "C"` definition:

```c
#ifdef __cplusplus
extern "C" {
#endif

// ...

#ifdef __cplusplus
}
#endif
```

- Include order, preferably separated by an empty line:
  1. related header file first (in implementation file).
  2. C standard library headers
  3. C++ standard library headers
  4. IDF SDK headers
  5. Project file headers

### Formatting

- Indentation: use 4 spaces, no tabs.
- Limit line length to 120 characters. Longer lines are allowed for specific cases.
- Braces: use [One True Brace](https://en.wikipedia.org/wiki/Indentation_style#One_True_Brace) style:
  - Opening brace is on the same line as the declaration.
  - Always use braces for single statement blocks.

```c
void foo_bar(bool condition) {
    if (condition) {
        foo();
    } else {
        bar();
    }
}
``` 
