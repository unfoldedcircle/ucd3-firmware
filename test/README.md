# Unit Tests

Host based unit tests using [GoogleTest](https://google.github.io/googletest/).

- Platform independent, doesn't use any IDF functionality.
- Only tests "business logic" functions like infrared conversions.
- All required mocks and library definitions are in the `./mocks` subdirectory.

Build:
```shell
cmake -S . -B build
cmake --build build
```

Run:
```shell
./build/common/common
./build/infrared/infrared
./build/improv_wifi/improv_wifi
./build/preferences/preferences
```

## Espressif IDF Unit Tests

IDF support is quite limited:

- Focus on tests running on-the-device. We don't want that...
- Host based tests only run on Linux, not even on macOS :-(
- Supported component mocks are quite limited

Further information:

- https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/unit-tests.html
- https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/host-apps.html

Guide on how to use GoogleTest with the IDF Linux host tests and component mocking:

- https://higaski.at/working-with-or-rather-around-the-esp32-build-system/
