#!/usr/bin/env python3
"""Checks episode 6 from the files: the map (out/sd/carts/SKYVALE.BM) has the three layers
of art.py cell by cell; the tree (sprite 9), the flag (10) and its pole (11) are in the sheet;
the code has the camera, the layers at three speeds, the flag and the two screens, and runs;
the recorded game (game.txt, the bot's buttons) goes from the title to the flag without
losing a life; the sound is not silence; the earlier episodes' data (tiles, flags, sounds,
Kip's frames) are intact. No pictures.

  python3 video/06-level/verify.py [OUT_DIR]
"""
import importlib.util
import math
import os
import struct
import subprocess
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmres    # noqa: E402
import bmaudio  # noqa: E402


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


art = load("a6", os.path.join(HERE, "art.py"))
replay = load("rp", os.path.join(ROOT, "video", "07-play", "replay.py"))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "out")
cart = os.path.join(out, "sd", "carts", "SKYVALE.BM")
bad = 0


def report(name, ok, detail=""):
    global bad
    print("%-46s %s %s" % (name, "ok" if ok else "WRONG", "" if ok else detail))
    bad += 0 if ok else 1


f = bmres.read(cart)
layers = bmres.layers_get(f)
mw, mh, _ = bmres.map_get(f)
lv = art.level()
report("three layers", [n for n, _ in layers] == ["main", "layer2", "layer3"], [n for n, _ in layers])
for (name, cells), key in zip(layers, ("main", "layer2", "layer3")):
    got = {(i % mw, i // mw): c for i, c in enumerate(cells) if c}
    want = lv[key]
    wrong = [(k, got.get(k), want.get(k)) for k in set(got) | set(want) if got.get(k) != want.get(k)]
    report("map layer %s: %d cells" % (name, len(want)), not wrong, wrong[:3])

w, h, rgba = bmres.sheet_get(f)


def stats(idx):
    solid = grey = red = 0
    for y in range(16):
        for x in range(16):
            i = 4 * (y * w + 16 * idx + x)
            if rgba[i + 3] >= 128:
                solid += 1
                r, g, b = rgba[i:i + 3]
                grey += abs(r - g) < 14 and r > 150
                red += r > 200 and g < 90
    return solid, grey, red


s9, s10, s11 = stats(9), stats(10), stats(11)
report("the tree (sprite 9)", s9[0] >= 80, s9)
report("the flag: grey pole, red cloth (sprite 10)", s10[1] >= 20 and s10[2] >= 40, s10)
report("the pole (sprite 11)", s11[0] == 32 and s11[1] == 32, s11)

lua = f.get(bmres.SEC_LUA).decode()
for needle in ('local cam, state, flag_x = 0, "title", 1200', 'if state == "title" then', "cam = math.max(0, math.min(kip.x - 240, 640))",
               "local function layer(l, s)", "layer(2, 0.25)", "layer(3, 0.5)", "layer(1, 1)", "spr(20, flag_x - 3, 144, 2, 2)", "spr(22, flag_x - 3, y, 2, 2)",
               'print("COURSE CLEAR"', "200, 560, 680, 960"):
    report("code has: " + needle[:36], needle in lua)

i = replay.replay(cart, os.path.join(HERE, "game.txt"), os.path.join(out, "sd-verify"), 40)
report("the game runs without error", i["ok"], i["text"][-200:])
report("title to flag, no life lost", i["clear"] is not None and not i["lives"] and i["clear"][2] == 3, (i["clear"], i["lives"]))
report("coins and a stomp on the way", i["coins"] >= 8 and i["stomps"] >= 1, (i["coins"], i["stomps"]))

wavf = os.path.join(out, "game.wav")
with wave.open(wavf) as wv:
    raw = wv.readframes(wv.getnframes())
    ch, rate = wv.getnchannels(), wv.getframerate()
data = struct.unpack("<%dh" % (len(raw) // 2), raw)[::ch]
seg = [math.sqrt(sum(x * x for x in data[j:j + rate]) / rate) for j in range(0, len(data) - rate, rate)]
print("rms per second of the game:", " ".join("%d" % v for v in seg))
report("the game has sound", sum(1 for v in seg if v > 300) >= 8, seg)

r = subprocess.run([sys.executable, os.path.join(ROOT, "video", "02-sdk", "verify.py"), os.path.join(out, "sd")], capture_output=True, text=True)
lines = [l for l in r.stdout.splitlines() if l.startswith(("tile", "  flags"))]
report("episode 2 tiles and their flags", len(lines) == 10 and all(l.rstrip().endswith("ok") for l in lines), r.stdout[-300:])
kip = load("kip", os.path.join(ROOT, "video", "01-pixel", "art.py"))
wrong = 0
for n, rows in enumerate(kip.FRAMES):
    for y in range(16):
        for x in range(16):
            j = 4 * (y * w + 16 * n + x)
            want = rows[y][x]
            got = None if rgba[j + 3] < 128 else tuple(rgba[j:j + 3])
            rgb = None if want == "." else kip.COLOURS[want][2]
            ok = got is None if rgb is None else (got is not None and all(abs(a - c) <= 8 for a, c in zip(got, (rgb >> 16, rgb >> 8 & 255, rgb & 255))))
            wrong += not ok
report("episode 1: Kip's frames", wrong == 0, wrong)
bank = bmaudio.unpack(bmaudio.extract(open(cart, "rb").read()))
report("episode 3: sounds and song", [x["name"] for x in bank["sounds"][:3]] == ["JUMP", "COIN", "STOMP"] and len(bank["songs"]) >= 1)
sys.exit(1 if bad else 0)
