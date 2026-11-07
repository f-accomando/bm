#!/usr/bin/env python3
"""mkbmf.py - Create BareMetal Font (.bmf) files from C font sources or raw data."""

import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(REPO_ROOT, "build", "bmf")

def parse_c_glyphs(c_file_path, height):
    with open(c_file_path, "r", encoding="utf-8") as f:
        text = f.read()
    # Find glyph rows: { 0x.., 0x.. }
    rows = []
    pattern = re.compile(r"\{\s*([0-9a-fx,\s]+)\s*\}")
    for m in pattern.finditer(text):
        bytes_str = m.group(1).split(",")
        nums = [int(b.strip(), 0) for b in bytes_str if b.strip()]
        if len(nums) == height:
            rows.extend(nums)
    if len(rows) != 256 * height:
        raise ValueError(f"Expected {256 * height} bytes from {c_file_path}, got {len(rows)}")
    return bytes(rows)

def make_bmf(name, width, height, glyphs_bytes):
    magic = b"BMFONT\x01\x00"
    dims = bytes([width, height])
    count = (256).to_bytes(2, "little")
    res = b"\x00" * 4
    name_bytes = name.encode("ascii")[:15].ljust(16, b"\x00")
    header = magic + dims + count + res + name_bytes
    return header + glyphs_bytes

def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    
    # 1. Terminus 8x16
    term_c = os.path.join(REPO_ROOT, "src", "gfx", "font8x16.c")
    term_bytes = parse_c_glyphs(term_c, 16)
    with open(os.path.join(OUT_DIR, "terminus.bmf"), "wb") as f:
        f.write(make_bmf("Terminus", 8, 16, term_bytes))
    print(f"Generated build/bmf/terminus.bmf ({len(term_bytes) + 32} bytes)")

    # 2. Inter 8x16
    inter_c = os.path.join(REPO_ROOT, "src", "gfx", "font_inter8x16.c")
    inter_bytes = parse_c_glyphs(inter_c, 16)
    with open(os.path.join(OUT_DIR, "inter.bmf"), "wb") as f:
        f.write(make_bmf("Inter", 8, 16, inter_bytes))
    print(f"Generated build/bmf/inter.bmf ({len(inter_bytes) + 32} bytes)")

    # 3. 6x12
    f6x12_c = os.path.join(REPO_ROOT, "src", "gfx", "font6x12.c")
    f6x12_bytes = parse_c_glyphs(f6x12_c, 12)
    with open(os.path.join(OUT_DIR, "6x12.bmf"), "wb") as f:
        f.write(make_bmf("6x12", 6, 12, f6x12_bytes))
    print(f"Generated build/bmf/6x12.bmf ({len(f6x12_bytes) + 32} bytes)")

    # 4. 8x14
    f8x14_c = os.path.join(REPO_ROOT, "src", "gfx", "font8x14.c")
    f8x14_bytes = parse_c_glyphs(f8x14_c, 14)
    with open(os.path.join(OUT_DIR, "8x14.bmf"), "wb") as f:
        f.write(make_bmf("8x14", 8, 14, f8x14_bytes))
    print(f"Generated build/bmf/8x14.bmf ({len(f8x14_bytes) + 32} bytes)")

if __name__ == "__main__":
    main()
