#!/usr/bin/env python3
"""Checks the cartridge the SDK saved in the recording (out/sd/carts/SKYVALE.BM):
Kip's frames of episode 1 are still there, the five tiles are drawn as art.py says,
their flags are set, the map has the level on layer 1 and the clouds on layer 2,
and the code draws the map. Reads the file, no pictures.

  python3 video/02-sdk/verify.py [SD_DIR]
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import bmres  # noqa: E402
import importlib.util  # noqa: E402


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


art = load("art2", os.path.join(HERE, "art.py"))
kip = load("kip", os.path.join(HERE, "..", "01-pixel", "art.py"))

sd = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "out", "sd")
path = os.path.join(sd, "carts", "SKYVALE.BM")
if not os.path.exists(path):
    print("no", path)
    sys.exit(1)
f = bmres.read(path)
w, h, rgba = bmres.sheet_get(f)
bad = 0


def report(name, wrong):
    global bad
    print("%-26s %s" % (name, "ok" if not wrong else "%d wrong, first %s" % (len(wrong), wrong[:3])))
    bad += len(wrong)


def px(x, y):
    i = 4 * (y * w + x)
    return None if rgba[i + 3] < 128 else (rgba[i], rgba[i + 1], rgba[i + 2])


def near(got, rgb):
    r, g, b = rgb >> 16, rgb >> 8 & 255, rgb & 255
    return got is not None and all(abs(a - c) <= 8 for a, c in zip(got, (r, g, b)))


# episode 1: Kip's frames are untouched (the art of 01-pixel)
wrong = []
for n, rows in enumerate(kip.FRAMES):
    for y in range(16):
        for x in range(16):
            want, got = rows[y][x], px(16 * n + x, y)
            ok = got is None if want == "." else near(got, kip.COLOURS[want][2])
            if not ok:
                wrong.append((n, x, y))
report("Kip's 7 frames", wrong)

# the tiles
flags = f.get(bmres.SEC_FLAGS)
fw, fh = struct.unpack_from("<HH", flags, 0)
for cell, (name, flag, rows) in art.TILES.items():
    x0, y0 = (cell % 32) * 8, (cell // 32) * 8
    wrong = []
    for y in range(8):
        for x in range(8):
            want, got = rows[y][x], px(x0 + x, y0 + y)
            ok = got is None if want == "." else near(got, art.COLOURS[want][1])
            if not ok:
                wrong.append((x, y, want, got))
    report("tile %d %s" % (cell, name), wrong)
    byte = flags[4 + cell] if 4 + cell < len(flags) else 0
    want = 0 if flag is None else 1 << flag
    report("  flags of %s" % name, [] if byte == want else [(byte, want)])

# the map
layers = bmres.layers_get(f)
print("layers:", [n for n, _ in layers])
mw, mh, _ = bmres.map_get(f)
lv = art.level()
for (name, cells), key in zip(layers, ("main", "layer2")):
    want = lv[key]
    got = {(i % mw, i // mw): c for i, c in enumerate(cells) if c}
    wrong = [(k, got.get(k), want.get(k)) for k in set(want) | set(got) if got.get(k) != want.get(k)]
    report("map layer %s" % name, wrong)
if [n for n, _ in layers][:2] != ["main", "layer2"]:
    bad += 1

# the code
lua = f.get(bmres.SEC_LUA).decode()
ok = "map(0, 0, 0, 88, 80, 34, l)" in lua and "function _draw()" in lua and "-- bm Pixel" not in lua
report("code draws the map", [] if ok else [lua[:80]])
sys.exit(1 if bad else 0)
