#!/usr/bin/env python3
"""
build.py - bm NES: joins src/*.lua into one main.lua, makes a blank 256 x 240 sprite sheet (the runtime needs one for
nes_to_sheet), and packs the project (.bme) and the cartridge (.bm) with scripts/mkbm.py.

  python3 carts/nes/build/build.py [outdir]      (default build/nes)

Files written in outdir: main.lua, sheet.png, nes.bme, nes.bm.
"""
import glob, os, shutil, struct, subprocess, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "nes")
os.makedirs(OUT, exist_ok=True)


def png(path, w, h, pixel):
    raw = b"".join(b"\x00" + b"".join(pixel(x, y) for x in range(w)) for y in range(h))
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(data)


def cover(path):
    """An 88x88 cover: a grey pad with a black cross and two red buttons. Original artwork."""
    W = 88
    def px(x, y):
        bg = (40, 20, 24, 255)
        if 20 <= y < 68 and 6 <= x < 82:
            if 14 <= x < 30 and 40 <= y < 46 or 20 <= x < 24 and 34 <= y < 52:         # the cross
                return (20, 20, 24, 255)
            for bx, col in ((58, (210, 40, 50)), (70, (210, 40, 50))):
                if (x - bx) ** 2 + (y - 52) ** 2 < 28:
                    return col + (255,)
            if 38 <= x < 46 and 50 <= y < 54 or 48 <= x < 56 and 50 <= y < 54:           # select and start
                return (60, 60, 66, 255)
            if y < 30:
                return (150, 150, 156, 255)
            return (200, 200, 206, 255)
        return bg
    png(path, W, W, lambda x, y: bytes(px(x, y)))


def main():
    parts = sorted(glob.glob(os.path.join(HERE, "..", "src", "*.lua")))
    lua = "\n".join(open(p, encoding="utf-8").read() for p in parts)
    if os.environ.get("NES_PLAY") == "1":           # a build for the bridge: opens the first ROM by itself and holds Start now and then
        lua = lua.replace("local AUTOPLAY = false", "local AUTOPLAY = true")
    open(os.path.join(OUT, "main.lua"), "w", encoding="utf-8").write(lua)
    sheet = os.path.join(OUT, "sheet.png")
    png(sheet, 256, 240, lambda x, y: b"\x00\x00\x00\x00")      # room for the picture of nes_to_sheet (256 x 240)
    cv = os.path.join(HERE, "..", "cover.png")
    if not os.path.exists(cv):
        cover(cv)
    mk = os.path.join(ROOT, "scripts", "mkbm.py")
    for ext in ("bme", "bm"):
        subprocess.check_call([sys.executable, mk, "-o", os.path.join(OUT, "nes." + ext),
                               "--lua", os.path.join(OUT, "main.lua"), "--sheet", sheet, "--sheet8",
                               "--cover", cv, "--info", os.path.join(HERE, "..", "bm.info")])
    if os.environ.get("NES_PLAY") != "1":
        shutil.copyfile(os.path.join(OUT, "nes.bme"), os.path.join(HERE, "..", "nes.bme"))
    print("built", OUT)


main()
