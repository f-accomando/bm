#!/usr/bin/env python3
"""bmz: the .bmz container, a zlib-compressed copy of one .bm, .bme or resource file (core/src/bm/bmz.h, docs/BM-FORMAT.md).

  python3 core/scripts/bmz.py pack pong.bm [more files] [-o DIR]    DIR/pong.bmz (default DIR: bmz)
  python3 core/scripts/bmz.py unpack bmz/pong.bmz [-r ROOT]         ROOT/bm/pong.bm (default ROOT: the SD card folder ".")
  python3 core/scripts/bmz.py list bmz/pong.bmz                     the header

The console reads the .bmz files in /bmz of the SD card and extracts each into its folder
(/bm, /bme, /bmm, /bmk, /bmi, /bms, /bmt, /bmc).
"""
import argparse
import os
import struct
import sys
import zlib

MAGIC = b"BMZ1"
HEAD = 32
MAX_RAW = 128 << 20
DIRS = {".bm": "bm", ".bme": "bme", ".bmm": "bmm", ".bmk": "bmk", ".bmi": "bmi", ".bms": "bms", ".bmt": "bmt", ".bmc": "bmc"}


def check_name(name):
    base, ext = os.path.splitext(name)
    if ext not in DIRS or not 1 <= len(base) <= 8 or name != name.lower():
        raise SystemExit(f"{name}: a lowercase 8.3 name with one of {' '.join(DIRS)}")
    if not all(c.isascii() and (c.isalnum() or c in "_-") for c in base):
        raise SystemExit(f"{name}: a-z, 0-9, _ and - only")


def check_kind(name, raw):
    cart = name.endswith((".bm", ".bme"))
    ok = raw[:8] in (b"BMCART\0\0", b"BM33CART") if cart else raw[:8] == b"BMRES\0\0\0"
    if not ok or len(raw) < 16:
        raise SystemExit(f"{name}: the content is not what the extension says")


def pack(raw, name, level=9):
    check_name(name)
    check_kind(name, raw)
    if not 0 < len(raw) <= MAX_RAW:
        raise SystemExit(f"{name}: size zero or above {MAX_RAW >> 20} MB")
    head = MAGIC + struct.pack("<HHII", 1, 0, len(raw), zlib.crc32(raw)) + name.encode().ljust(16, b"\0")
    assert len(head) == HEAD
    return head + zlib.compress(raw, level)


def parse(data):
    """(name, raw size, crc, zlib stream) or SystemExit"""
    if len(data) < HEAD + 2 or data[:4] != MAGIC:
        raise SystemExit("not a .bmz file")
    version, flags, size, crc = struct.unpack_from("<HHII", data, 4)
    if version != 1 or flags:
        raise SystemExit("a .bmz version this tool does not know")
    name = data[16:32]
    if b"\0" not in name or name.rstrip(b"\0").count(b"\0"):
        raise SystemExit("bad name inside")
    name = name.rstrip(b"\0").decode("ascii", "replace")
    check_name(name)
    if not 0 < size <= MAX_RAW:
        raise SystemExit("the size is zero or too big")
    return name, size, crc, data[HEAD:]


def unpack(data):
    name, size, crc, z = parse(data)
    try:
        raw = zlib.decompressobj().decompress(z, size + 1)
    except zlib.error as e:
        raise SystemExit(f"{name}: the compressed data is broken ({e})")
    if len(raw) != size:
        raise SystemExit(f"{name}: the compressed data is broken (size)")
    if zlib.crc32(raw) != crc:
        raise SystemExit(f"{name}: the CRC-32 does not match")
    check_kind(name, raw)
    return name, raw


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pack")
    p.add_argument("files", nargs="+")
    p.add_argument("-o", "--out", default="bmz")
    p.add_argument("--level", type=int, default=9)
    u = sub.add_parser("unpack")
    u.add_argument("files", nargs="+")
    u.add_argument("-r", "--root", default=".")
    ls = sub.add_parser("list")
    ls.add_argument("files", nargs="+")
    a = ap.parse_args()
    for f in a.files:
        if a.cmd == "pack":
            name = os.path.basename(f).lower()
            z = pack(open(f, "rb").read(), name, a.level)
            os.makedirs(a.out, exist_ok=True)
            out = os.path.join(a.out, os.path.splitext(name)[0] + ".bmz")
            open(out, "wb").write(z)
            print(f"{out}: {os.path.getsize(f)} -> {len(z)} bytes")
        elif a.cmd == "unpack":
            name, raw = unpack(open(f, "rb").read())
            d = os.path.join(a.root, DIRS[os.path.splitext(name)[1]])
            os.makedirs(d, exist_ok=True)
            open(os.path.join(d, name), "wb").write(raw)
            print(f"{os.path.join(d, name)}: {len(raw)} bytes")
        else:
            name, size, crc, z = parse(open(f, "rb").read())
            print(f"{f}: {name} -> /{DIRS[os.path.splitext(name)[1]]}, {size} bytes raw, {len(z)} compressed, crc {crc:08x}")


if __name__ == "__main__":
    sys.exit(main())
