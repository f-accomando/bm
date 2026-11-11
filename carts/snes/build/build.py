#!/usr/bin/env python3
"""
build.py - bm SNES: joins src/*.lua into one main.lua, makes a blank 256 x 240 sprite sheet (the runtime needs one)
cache needs, and packs the project (.bme) and the cartridge (.bm) with scripts/mkbm.py.

  python3 carts/snes/build/build.py [outdir]      (default build/snes)

Files written in outdir: main.lua, sheet.png, snes.bme, snes.bm.
"""
import os, struct, subprocess, sys, zlib, glob

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "snes")
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
    """An 88x88 cover: a grey pad with four coloured buttons. Original artwork."""
    W = 88
    def px(x, y):
        bg = (24, 32, 48, 255)
        if 8 <= y < 66 and 6 <= x < 82:
            r = 14 if (y < 20 and (x < 16 or x > 72)) else 0
            if (y - 37) ** 2 + (x - 14) ** 2 > 0 and ((x - 14) ** 2 + (y - 37) ** 2) < 170 and False:
                return bg
            c = (170, 172, 180, 255)
            if 16 <= x < 30 and 31 <= y < 43 or 20 <= x < 26 and 27 <= y < 47:      # cross
                return (50, 54, 66, 255)
            for (bx, by, col) in ((66, 28, (200, 60, 60)), (74, 37, (240, 200, 60)),
                                  (58, 37, (60, 160, 90)), (66, 46, (60, 100, 210))):
                if (x - bx) ** 2 + (y - by) ** 2 < 30:
                    return col + (255,)
            if 36 <= x < 52 and 40 <= y < 44:
                return (90, 94, 106, 255)
            return c
        return bg
    png(path, W, W, lambda x, y: bytes(px(x, y)))


def main():
    parts = sorted(glob.glob(os.path.join(HERE, "..", "src", "*.lua")))
    lua = "\n".join(open(p).read() for p in parts)
    if os.environ.get("SNES_PLAY") == "1":         # a build for the bridge: plays the first ROM by itself; the logger is on
        lua = lua.replace("local AUTOPLAY = false", "local AUTOPLAY = true")
    if os.environ.get("SNES_TEST") == "1":         # a test build for the bridge: the first ROM opens by itself, the FPS line is on
        lua = lua.replace("local AUTO = false", "local AUTO = true").replace("local show_fps = false", "local show_fps = true")
        if os.environ.get("SNES_ADAPT") == "1":    # the speed controller on in the test build (no fixed range of frames: the cart plays on)
            lua = lua.replace("if not AUTO then adapt() end", "adapt()").replace("(AUTO and 4 or every)", "every").replace("if not (AUTO and frames >= 3200) then", "if true then").replace("local bench = AUTO and frames >= 600 and frames < 3200", "local bench = false").replace("local MINI, last_ups = false, 0", "local MINI, last_ups = true, 0")
        if os.environ.get("SNES_GPU") == "s":      # a test build that starts in the sharp mode (the default is native 256x256)
            lua = lua.replace('local START_MODE = "native"', 'local START_MODE = "sharp"')
        if os.environ.get("SNES_GPU") in ("2", "3"):     # a test build that starts in a GPU mode (2: 960x540 and x2, 3: 1280x720 and x3)
            lua = lua.replace('local START_MODE = "native"', 'local START_MODE = "gpu%s"' % os.environ["SNES_GPU"])
            if os.environ.get("SNES_GPUEMPTY") == "1":
                lua = lua.replace("local GPU_EMPTY = false", "local GPU_EMPTY = true")
        if os.environ.get("SNES_NOHUD") == "1":    # no statistics on the picture: a clean screenshot
            lua = lua.replace("local show_fps = true", "local show_fps = false").replace("  if AUTO then   ", "  if false then   ").replace("local AUTO = true", "local AUTO = true")
    open(os.path.join(OUT, "main.lua"), "w").write(lua)
    sheet = os.path.join(OUT, "sheet.png")
    png(sheet, 256, 512, lambda x, y: b"\x00\x00\x00\x00")      # room for the picture of snes_to_sheet (256 x 240, and room for the exported sprite tiles below it)
    cv = os.path.join(HERE, "..", "cover.png")
    if not os.path.exists(cv):
        cover(cv)
    mk = os.path.join(ROOT, "scripts", "mkbm.py")
    for ext in ("bme", "bm"):
        subprocess.check_call([sys.executable, mk, "-o", os.path.join(OUT, "snes." + ext),
                               "--lua", os.path.join(OUT, "main.lua"), "--sheet", sheet, "--sheet8",
                               "--cover", cv, "--info", os.path.join(HERE, "..", "bm.info")] + (["--res", os.environ["SNES_RES"]] if os.environ.get("SNES_RES") else []))
    print("built", OUT)


main()
