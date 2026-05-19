#!/usr/bin/env python3
"""
Decode ESP32-S3 efuse BLOCK3 data from espefuse.py dump output.

Input example:
    "56454401 53365a4d 44435543 00000033 03000036 00000000 00000000 00000000"
or a shorter line like:
    "56454401 53365a4d 44435543 00000033 03000036"
"""

import sys

def decode_block3(hex_string: str):
    # Split into 32-bit words (8 hex digits each)
    words = hex_string.strip().split()
    # Pad to exactly 8 words with zeros
    words += ["00000000"] * (8 - len(words))

    # Convert each little-endian word to bytes
    raw_bytes = bytearray()
    for w in words:
        value = int(w, 16)
        raw_bytes.extend(value.to_bytes(4, 'little'))

    # The block is 32 bytes total; we only need the first 25 according to spec
    # (extra bytes are reserved)
    data = raw_bytes[:25]

    # Extract fields
    version = data[0]
    serial_bytes = data[1:9]
    model_bytes = data[9:16]
    hw_rev_bytes = data[16:19]
    feature_bits = data[19] if len(data) > 19 else 0

    # Decode ASCII strings, strip null padding
    serial = serial_bytes.decode('ascii').rstrip('\x00')
    model = model_bytes.decode('ascii').rstrip('\x00')
    hw_rev = hw_rev_bytes.decode('ascii').rstrip('\x00')

    # Feature bits
    poe = (feature_bits >> 0) & 1
    charging_dock = (feature_bits >> 1) & 1

    # Output
    print(f"Version:         0x{version:02x} ({version})")
    print(f"Serial number:   {serial}")
    print(f"Model number:    {model}")
    print(f"HW revision:     {hw_rev}")
    print(f"Feature bits:    0x{feature_bits:02x}")
    print(f"  - PoE:          {poe}")
    print(f"  - Charging dock:{charging_dock}")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        input_str = sys.argv[1]
    else:
        # Read from stdin if no argument
        input_str = sys.stdin.read().strip()
    if not input_str:
        print("Error: No input provided.")
        sys.exit(1)

    decode_block3(input_str)
