#!/usr/bin/env python3
"""Checks episode 4 from the files: the code bm Code saved in the cartridge
(out/sd/carts/SKYVALE.BM) has the three functions, indented by the editor, and
runs; the recorded game (out/game.txt) shows what the narrator says: Kip stands on a
platform after the hop, falls on the spikes once and goes back to the start, then
jumps the pit and ends on the far side; the sound of the game recording is not
silence; the sounds, tiles and map of the earlier episodes are intact. No pictures.

  python3 video/04-code/verify.py [OUT_DIR]
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
import bmres  # noqa: E402

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "out")
cart = os.path.join(out, "sd", "carts", "SKYVALE.BM")
bad = 0


def report(name, ok, detail=""):
    global bad
    print("%-44s %s %s" % (name, "ok" if ok else "WRONG", "" if ok else detail))
    bad += 0 if ok else 1


f = bmres.read(cart)
lua = f.get(bmres.SEC_LUA).decode()
for needle in ('local lib = require "bmlib"', "lib.tiles({ solid = 1, platform = 2, edge = true })", "music(0)",
               "function _update()", "lib.step(kip)", "function _draw()", "spr(f, kip.x - 3, kip.y - 2, 2, 2, face < 0)"):
    report("code has: " + needle[:36], needle in lua)
report("indented by the editor", re.search(r"^  lib\.tiles", lua, re.M) is not None and re.search(r"^    sfx\(0\)", lua, re.M) is not None)

# the game, played by game.txt, with a log of Kip
dbg = f
dbg.put(bmres.SEC_LUA, (lua + '''
do
  local __u, n = _update, 0
  function _update(...)
    local ox = kip.x
    __u(...)
    n = n + 1
    if kip.x < ox - 50 then log("RESPAWN " .. n) end
    if n % 10 == 0 then log(string.format("F%d %.0f %.0f %s", n, kip.x, kip.y, tostring(kip.ground))) end
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
pos = {int(m.group(1)): (float(m.group(2)), float(m.group(3)), m.group(4) == "true")
       for m in re.finditer(r"F(\d+) (-?\d+) (-?\d+) (true|false)", text)}
respawns = [int(m.group(1)) for m in re.finditer(r"RESPAWN (\d+)", text)]
report("on the platform after the hop (y 162)", pos.get(140, (0, 0, 0))[1] == 162 and pos[140][2], pos.get(140))
report("falls on the spikes once and restarts", len(respawns) == 1 and 240 < respawns[0] < 300, respawns)
report("jumps the pit and lands beyond it", pos.get(460, (0, 0, 0))[0] > 400 and pos[460][1] == 210 and pos[460][2], pos.get(460))
report("ends on the far side, on the ground", pos.get(590, (0, 0, 0))[0] > 540 and pos[590][2], pos.get(590))

# the sound of the game part
wavf = os.path.join(out, "game.wav")
if os.path.exists(wavf):
    with wave.open(wavf) as w:
        raw = w.readframes(w.getnframes())
        ch, rate = w.getnchannels(), w.getframerate()
    data = struct.unpack("<%dh" % (len(raw) // 2), raw)[::ch]      # the first channel

    seg = [math.sqrt(sum(x * x for x in data[i:i + rate]) / rate) for i in range(0, len(data) - rate, rate)]
    print("rms per second of the game:", " ".join("%d" % v for v in seg))
    report("the game has sound (music and effects)", max(seg) > 300 and sum(1 for v in seg if v > 300) >= 6, seg)

# the earlier episodes are still in the file (sounds, tiles, map)
r = subprocess.run([sys.executable, os.path.join(ROOT, "video", "02-sdk", "verify.py"), os.path.join(out, "sd")],
                   capture_output=True, text=True)
# (its last line, "code draws the map", is episode 2's code: replaced on purpose here)
lines = [l for l in r.stdout.splitlines() if l.strip() and not l.startswith(("layers:", "code draws the map"))]
report("episode 2 tiles, flags, map still there", all(l.rstrip().endswith("ok") for l in lines), r.stdout[-300:])
data = bmres.read(cart)
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmaudio  # noqa: E402
bank = bmaudio.unpack(bmaudio.extract(open(cart, "rb").read()))
report("episode 3 sounds and song still there", [x["name"] for x in bank["sounds"][:3]] == ["JUMP", "COIN", "STOMP"]
       and len(bank["songs"]) >= 1)
sys.exit(1 if bad else 0)
