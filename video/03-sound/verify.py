#!/usr/bin/env python3
"""Checks what bm Sound saved in the recording (out/sd/carts/SKYVALE.BM) and what it
played (out/sound.wav): the three sounds and three effects of the script, a song
with patterns from riff, the tiles and map of episode 2 still there, and sound in
the wav (not silence). No pictures.

  python3 video/03-sound/verify.py [OUT_DIR]
"""
import math
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import bmaudio  # noqa: E402

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "out")
path = os.path.join(out, "sd", "carts", "SKYVALE.BM")
bad = 0


def report(name, ok, detail=""):
    global bad
    print("%-34s %s %s" % (name, "ok" if ok else "WRONG", "" if ok else detail))
    bad += 0 if ok else 1


data = bmaudio.extract(open(path, "rb").read())
bank = bmaudio.unpack(data) if data else None
report("the cartridge has an AUDIO bank", bank is not None)
if bank:
    names = [x["name"] for x in bank["sounds"][:3]]
    report("sounds JUMP COIN STOMP", names == ["JUMP", "COIN", "STOMP"], names)
    jump = bank["sounds"][0]
    report("JUMP bends from -12", jump["pitch"] == -12 and jump["pitch_time"] > 0, (jump["pitch"], jump["pitch_time"]))
    report("COIN is a pluck", str(bank["sounds"][1]["wave"]).lower() == "pluck", bank["sounds"][1]["wave"])
    report("STOMP is noise, bends down", str(bank["sounds"][2]["wave"]).lower() == "noise" and bank["sounds"][2]["pitch"] == -12,
           (bank["sounds"][2]["wave"], bank["sounds"][2]["pitch"]))
    sfx = {x["name"]: x for x in bank["sfx"] if x["name"]}
    report("effects JUMP COIN STOMP", set(sfx) >= {"JUMP", "COIN", "STOMP"}, list(sfx))
    for n, count in (("JUMP", 5), ("COIN", 2), ("STOMP", 3)):
        steps = [t for t in sfx.get(n, {}).get("steps", []) if t not in ("", "---", "--- 00 00 00")]
        report("  %s has %d notes" % (n, count), len(steps) >= count, sfx.get(n, {}).get("steps"))
    if "JUMP" in sfx:
        report("  JUMP faster than 60 ms", sfx["JUMP"]["ms"] < 60, sfx["JUMP"]["ms"])
    report("a song with patterns from riff", len(bank["songs"]) >= 1 and bank["songs"][0]["order"] and len(bank["patterns"]) >= 1,
           (len(bank["songs"]), len(bank["patterns"])))

# episode 2 is still in the file: the map, the tiles and the code
r = subprocess.run([sys.executable, os.path.join(HERE, "..", "02-sdk", "verify.py"), os.path.join(out, "sd")], capture_output=True, text=True)
report("episode 2: tiles, flags, map, code", r.returncode == 0, r.stdout[-300:])

# the sound: not silence, and loud where the music plays
wav = os.path.join(out, "sound.wav")
if os.path.exists(wav):
    raw = open(wav, "rb").read()
    pos = raw.find(b"data")
    samples = struct.unpack_from("<%dh" % ((len(raw) - pos - 8) // 2), raw, pos + 8)
    rate = 48000
    def rms(a, b):
        seg = samples[int(a * rate):int(b * rate)]
        return math.sqrt(sum(x * x for x in seg) / max(1, len(seg)))
    total = len(samples) / rate
    levels = [rms(t, t + 5) for t in range(0, int(total) - 5, 5)]
    print("rms per 5 s:", " ".join("%d" % v for v in levels))
    report("the recording has sound", max(levels) > 300, max(levels))
    report("silent at the start (nothing played yet)", levels[0] < 100, levels[0])
sys.exit(1 if bad else 0)
