#!/usr/bin/env python3
"""Merge language JSON files into a single lang.js for embedding."""

import json
import sys
from pathlib import Path

LANG_DIR = Path(__file__).parent.parent / "webroot" / "lang"
OUTPUT = Path(__file__).parent.parent / "webroot" / "lang.js"


def filename_to_code(name):
    """Map filenames to language codes. pt-BR.json -> pt"""
    code = name.stem.lower()
    if "-" in code:
        code = code.split("-")[0]
    return code


def main():
    languages = {}

    # Load English first as reference
    en_file = LANG_DIR / "en.json"
    if not en_file.exists():
        print("ERROR: en.json not found!", file=sys.stderr)
        sys.exit(1)

    with open(en_file, "r", encoding="utf-8") as fh:
        languages["en"] = json.load(fh)

    # Load other languages, only include differing values
    for f in sorted(LANG_DIR.glob("*.json")):
        code = filename_to_code(f)
        if code == "en":
            continue

        with open(f, "r", encoding="utf-8") as fh:
            data = json.load(fh)

        diff = {k: v for k, v in data.items() if v != languages["en"].get(k, "")}
        if diff:
            languages[code] = diff

    # Generate JS output
    js_output = "var LANG_DATA=" + json.dumps(languages, ensure_ascii=False, separators=(",", ":")) + ";\n"

    OUTPUT.write_text(js_output, encoding="utf-8")

    num_keys = len(languages["en"])
    num_langs = len(languages)
    raw_size = len(js_output.encode("utf-8"))
    print(f"Generated {OUTPUT}: {num_langs} languages, {num_keys} keys, {raw_size} bytes")


if __name__ == "__main__":
    main()
