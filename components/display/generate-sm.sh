#!/bin/bash

set -e

/Applications/ss.cli run --here
mv DisplaySm.cpp src/
mv DisplaySm.h src/
