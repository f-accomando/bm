#!/usr/bin/env python3
"""
build.py - bm PSX: joins src/*.lua into one main.lua and packs the project (.bme) and the cartridge (.bm)
with core/scripts/mkbm.py. The cover is bm/psx/cover.png (core/scripts/mkemucovers.py).

  python3 bm/psx/build/build.py [outdir]      (default build/psx)
"""
import glob, os, shutil, struct, subprocess, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "psx")
os.makedirs(OUT, exist_ok=True)
lua = "\n".join(open(p, encoding="utf-8").read() for p in sorted(glob.glob(os.path.join(HERE, "..", "src", "*.lua"))))
open(os.path.join(OUT, "main.lua"), "w", encoding="utf-8").write(lua)
def chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
open(os.path.join(OUT, "sheet.png"), "wb").write(                 # a blank 16x16 sheet, as before
    b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 16, 16, 8, 6, 0, 0, 0))
    + chunk(b"IDAT", zlib.compress(b"\x00" * (16 * 65))) + chunk(b"IEND", b""))
mk = os.path.join(ROOT, "core", "scripts", "mkbm.py")
for ext in ("bme", "bm"):
    subprocess.check_call([sys.executable, mk, "-o", os.path.join(OUT, "psx." + ext), "--lua", os.path.join(OUT, "main.lua"), "--sheet", os.path.join(OUT, "sheet.png"), "--sheet8",
                           "--cover", os.path.join(HERE, "..", "cover.png"), "--info", os.path.join(HERE, "..", "bm.info")])
shutil.copyfile(os.path.join(OUT, "psx.bme"), os.path.join(HERE, "..", "psx.bme"))
print("built", OUT)
