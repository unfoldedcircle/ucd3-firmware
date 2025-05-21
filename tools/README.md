# Tools

## ir_repeat

The [ir_repeat](ir_repeat/) tool is a simple Node.js client tool to test sending continuous IR repeat codes to a dock.

## ir_torture

The [ir_torture](ir_torture/) tool is a stress test tool to continuously send the same IR code to a dock.

## git-semver

Local copy of the Linux and macOS binary of [git-semver](https://github.com/mdomke/git-semver) 6.9.0 to simplify GitHub actions. License: MIT

## ota

OTA firmware update creation and OTA server upload scripts.

## git-version.sh

Helper script to determine [SemVer](https://semver.org/) compatible version string from git repository.

This script is called from the build process and requires `git-semver`. On Linux the local binary will be used if not installed.
