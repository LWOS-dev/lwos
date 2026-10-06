#!/usr/bin/env python3
import argparse
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description="Export g_8x16_font as raw bytes.")
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    source = args.input.read_text()
    source = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
    match = re.search(r"\bg_8x16_font\s*\[\s*(\d*)\s*\]\s*=\s*\{([^}]*)\}", source)
    if not match:
        parser.error("g_8x16_font initializer not found")
    tokens = match[2].strip().rstrip(",").split(",")
    if any(not re.fullmatch(r"\s*(?:0[xX][0-9a-fA-F]+|[0-9]+)\s*", t) for t in tokens):
        parser.error("initializer must contain only literal byte values")
    values = [int(t.strip(), 16 if t.strip().lower().startswith("0x") else 10) for t in tokens]
    if any(v > 255 for v in values):
        parser.error("font value outside byte range")
    if len(values) != 256 * 16:
        parser.error(f"expected 4096 bytes for 256 8x16 glyphs, got {len(values)}")
    if match[1] and int(match[1]) != len(values):
        print(f"note: array declares {match[1]} bytes; exporting all {len(values)} initializer bytes")
    args.output.write_bytes(bytes(values))
    print(f"{args.output}: {len(values)} bytes")


if __name__ == "__main__":
    main()
