#!/usr/bin/env python3
"""Checks episode 5 from the files: the slime the assistant drew in bm Pixel is in
sprite 8 of the sheet (green, 16x16); the code bm Code saved has the lists, the stomp
and the HUD the assistant gave (adapted with Replace: Kip's face, the English label)
and compiles (the game runs); the recorded game (game.txt) collects the three coins,
stomps a slime, falls on the spikes once and stomps the second slime; the sound is not
silence; earlier episodes' data (tiles, flags, map, sounds) are intact. No pictures.

  python3 video/05-assistant/verify.py [OUT_DIR]
"""
import math
import os
import re
import struct
import subprocess
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmres    # noqa: E402
import bmaudio  # noqa: E402

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "out")
cart = os.path.join(out, "sd", "carts", "SKYVALE.BM")
bad = 0


def report(name, ok, detail=""):
    global bad
    print("%-46s %s %s" % (name, "ok" if ok else "WRONG", "" if ok else detail))
    bad += 0 if ok else 1


f = bmres.read(cart)

# the slime: sprite 8 (x 128, y 0), green
w, h, rgba = bmres.sheet_get(f)
solid = [(rgba[4 * (y * w + 128 + x)], rgba[4 * (y * w + 128 + x) + 1], rgba[4 * (y * w + 128 + x) + 2])
         for y in range(16) for x in range(16) if rgba[4 * (y * w + 128 + x) + 3] >= 128]
green = [c for c in solid if c[1] > c[0] + 20 and c[1] > c[2] + 20]
report("the slime is in sprite 8", len(solid) >= 80, len(solid))
report("and it is green", len(green) >= 40, len(green))

lua = f.get(bmres.SEC_LUA).decode()
for needle in ("lib.sweep(coins)", "lib.sweep(slimes)", "kip.vy > 0 and kip.y + kip.h - s.y < 8", "hurt_t = 90",
               'print("SCORE " .. score, 8, 8, 0xFFFFFF)', "spr(0, SCREEN_W - 20 * i, 8, 2, 2)", "camera()"):
    report("code has: " + needle[:34], needle in lua)
report("the heart and the Italian label are replaced", "48," not in lua and "PUNTI" not in lua)
report("the code ends well (no 'endend')", "endend" not in lua and lua.rstrip().endswith("end"))

dbg = f
dbg.put(bmres.SEC_LUA, (lua + '''
do
  local __u, n = _update, 0
  function _update(...)
    local sc, lv = score, lives
    __u(...)
    n = n + 1
    if score ~= sc then log(string.format("SCORE %d at frame %d", score, n)) end
    if lives ~= lv then log(string.format("LIVES %d at frame %d", lives, n)) end
  end
end
''').encode())
sd = os.path.join(out, "sd-verify")
os.makedirs(os.path.join(sd, "carts"), exist_ok=True)
dpath = os.path.join(sd, "carts", "DBG.BM")
bmres.write(dpath, dbg)
r = subprocess.run([os.path.join(ROOT, "build", "host", "bmhost-bin"), dpath, "--sd", sd, "--seconds", "10", "--input",
                    os.path.join(out, "game.txt")], capture_output=True, text=True)
text = r.stdout + r.stderr
report("the game runs without error", "ok," in text and "ERROR" not in text, text[-200:])
sc = [(int(a), int(b)) for a, b in re.findall(r"SCORE (\d+) at frame (\d+)", text)]
lv = [(int(a), int(b)) for a, b in re.findall(r"LIVES (\d+) at frame (\d+)", text)]
report("three coins by the first hop (10, 20, 30)", [s for s in sc if s[1] < 105][:3] and [s[0] for s in sc[:3]] == [10, 20, 30], sc[:4])
report("the first slime is stomped (+100)", any(b - a == 100 for (a, _), (b, _) in zip([(0, 0)] + sc, sc)), sc)
report("exactly one life lost: the spikes (3 -> 2)", lv == [(2, lv[0][1])] and 190 < lv[0][1] < 230, lv)
report("the second slime is stomped too", len([1 for (a, _), (b, _) in zip([(0, 0)] + sc, sc) if b - a == 100]) == 2, sc)

wavf = os.path.join(out, "game.wav")
with wave.open(wavf) as wv:
    raw = wv.readframes(wv.getnframes())
    ch, rate = wv.getnchannels(), wv.getframerate()
data = struct.unpack("<%dh" % (len(raw) // 2), raw)[::ch]
seg = [math.sqrt(sum(x * x for x in data[i:i + rate]) / rate) for i in range(0, len(data) - rate, rate)]
print("rms per second of the game:", " ".join("%d" % v for v in seg))
report("the game has sound", sum(1 for v in seg if v > 300) >= 6, seg)

r = subprocess.run([sys.executable, os.path.join(ROOT, "video", "02-sdk", "verify.py"), os.path.join(out, "sd")],
                   capture_output=True, text=True)
lines = [l for l in r.stdout.splitlines() if l.strip() and not l.startswith(("layers:", "code draws the map"))]
report("episode 2 tiles, flags, map still there", all(l.rstrip().endswith("ok") for l in lines), r.stdout[-300:])
bank = bmaudio.unpack(bmaudio.extract(open(cart, "rb").read()))
report("episode 3 sounds and song still there", [x["name"] for x in bank["sounds"][:3]] == ["JUMP", "COIN", "STOMP"]
       and len(bank["songs"]) >= 1)
sys.exit(1 if bad else 0)
