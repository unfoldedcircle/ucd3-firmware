#!/bin/bash

set -e

/Applications/ss.cli run --here
mv NetworkSm.cpp src/
mv NetworkSm.h src/
