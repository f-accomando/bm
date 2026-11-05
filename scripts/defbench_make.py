#!/usr/bin/env python3
"""Makes build/carts/defbench.bm, the corpus of the Deflate Bench utility (src/kernel/defbench.c).

Takes the unique sections of the cartridges in the repository, joins those of one type until a cap and stores each
type as a DATA blob: u32 raw size, u32 CRC-32 of the raw bytes, zlib stream.

  python3 scripts/defbench_make.py [--mb 6] [--level 6] [-o build/carts/defbench.bm]
"""
import argparse
import glob
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TYPES = {1: "lua", 2: "sheet", 5: "sheet8", 8: "mesh", 9: "anim", 3: "map", 6: "audio"}


def sections(path):
    d = open(path, "rb").read()
    if len(d) < 144 or d[:6] != b"BMCART":
        return
    for i in range(d[17]):
        t, off, size, _ = struct.unpack_from("<IIII", d, 128 + 16 * i)
        if t in TYPES and size:
            yield t, d[off:off + size]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mb", type=float, default=6, help="raw MB per blob at most")
    ap.add_argument("--level", type=int, default=6)
    ap.add_argument("-o", "--output", default=os.path.join(ROOT, "build", "carts", "defbench.bm"))
    ap.add_argument("--root", default=os.path.join(ROOT, "..", "bm33"), help="where the cartridges are")
    a = ap.parse_args()
    cap = int(a.mb * 1048576)
    files = sorted(glob.glob(os.path.join(a.root, "**", "*.bm"), recursive=True))
    files += sorted(glob.glob(os.path.join(a.root, "**", "*.bme"), recursive=True))
    seen, parts, size = set(), {}, {}
    for f in files:
        for t, body in sections(f):
            h = hashlib.sha1(body).digest()
            if h in seen or size.get(t, 0) + len(body) > cap:
                continue
            seen.add(h)
            parts.setdefault(t, []).append(body)
            size[t] = size.get(t, 0) + len(body)
    items = []
    for t, name in TYPES.items():
        if t not in parts:
            continue
        raw = b"".join(parts[t])
        z = zlib.compress(raw, a.level)
        items.append((name, struct.pack("<II", len(raw), zlib.crc32(raw)) + z))
        print(f"{name:7} raw {len(raw)/1024:9.0f} KB  zip {len(z)/1024:9.0f} KB  {len(raw)/len(z):5.2f}x")
    tmp = tempfile.mkdtemp()
    lua = os.path.join(tmp, "main.lua")
    open(lua, "w").write("-- Deflate Bench data, not a game\n")
    cmd = [sys.executable, os.path.join(ROOT, "scripts", "mkbm.py"), "-o", a.output, "--lua", lua,
           "--title", "Deflate Bench", "--author", "bm"]
    for name, blob in items:
        p = os.path.join(tmp, name + ".bin")
        open(p, "wb").write(blob)
        cmd += ["--data", f"{name}={p}"]
    os.makedirs(os.path.dirname(a.output), exist_ok=True)
    subprocess.check_call(cmd)
    print(a.output, os.path.getsize(a.output) // 1024, "KB")


main()
