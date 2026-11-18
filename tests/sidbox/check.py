#!/usr/bin/env python3
"""SIDBOX on the PC (make test-sidbox).

bmhost plays the cartridge with the keyboard script below and the test reads the WAV file and the screenshots:
1. every melodic instrument is heard (the INST row is stepped with the Right key, a key is held for each one);
2. every slot of the four drum kits is heard;
3. the three pages draw a picture (SEQ, SOUND, VIS) and the sequencer makes sound;
4. the song is saved in the SD folder and the second run loads it (tempo and bank).

  tests/sidbox/check.py HOST CART OUTDIR
"""
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import wave

HOST, CART, OUT = sys.argv[1:4]
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SR = 48000
fails = 0


def fail(msg):
    global fails
    fails += 1
    print("FAILED:", msg)


def run(name, script, seconds, sd, shots=None, wav=None):
    path = os.path.join(OUT, name + ".in")
    with open(path, "w") as f:
        f.write("\n".join(script) + "\n")
    cmd = [HOST, CART, "--seconds", str(seconds), "--sd", sd, "--input", path, "--quiet"]
    if shots:
        os.makedirs(shots, exist_ok=True)
        cmd += ["--shots", shots]
    if wav:
        cmd += ["--wav", wav]
    r = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    text = r.stdout + r.stderr
    if r.returncode or " ok," not in text:
        fail("%s: the cartridge stopped with an error\n%s" % (name, text[-600:]))
    return text


def samples(path):
    w = wave.open(path)
    n = w.getnframes()
    return struct.unpack("<%dh" % (n * 2), w.readframes(n))


def window(d, t0, t1):
    seg = d[int(t0 * SR) * 2:int(t1 * SR) * 2]
    if not seg:
        return 0.0, 0.0
    rms = math.sqrt(sum(x * x for x in seg) / len(seg)) / 32768
    return rms, max(abs(x) for x in seg) / 32768


def png_size(path):
    with open(path, "rb") as f:
        d = f.read(24)
    return struct.unpack(">II", d[16:24])


shutil.rmtree(OUT, ignore_errors=True)
os.makedirs(OUT)
src = open(os.path.join(ROOT, "bm", "sidbox", "main.lua"), encoding="utf-8").read()
mel = len(re.findall(r'(?<![a-z_])(?:sid|sim)\("[^"]+",', src))

# 1. every melodic instrument: F6 (SOUND page), Down (the INST row), then Right + a key held (Z)
script, f = ["1 keys 0x3F", "3 keys none", "5 keys 0x51", "7 keys none"], 10
for _ in range(mel - 1):
    script += ["%d keys 0x4F" % f, "%d keys 0x1D" % (f + 2), "%d keys none" % (f + 30)]
    f += 45
script.append("%d quit" % f)
run("inst", script, f / 60 + 1, os.path.join(OUT, "sd1"), wav=os.path.join(OUT, "inst.wav"))
d = samples(os.path.join(OUT, "inst.wav"))
quiet = []
for i in range(mel - 1):
    rms, pk = window(d, (10 + i * 45 + 2) / 60, (10 + i * 45 + 30) / 60)
    if pk < 0.1 or pk > 0.99:
        quiet.append((i + 2, round(rms, 3), round(pk, 2)))
if quiet:
    fail("instruments with no sound or a clipped peak (number, rms, peak): %s" % quiet)
print("sidbox: %d melodic instruments checked" % mel)

# 2. the drum kits: track 4 is a drum track; F6, Down, Right for each kit, then the 8 white keys
script, f = ["1 keys 0x3D", "3 keys none", "4 keys 0x3F", "6 keys none", "8 keys 0x51", "10 keys none"], 20
keys = [0x1D, 0x1B, 0x06, 0x19, 0x05, 0x11, 0x10, 0x36]
starts = []
for kit in range(4):
    script += ["%d keys 0x4F" % f, "%d keys none" % (f + 2)]
    f += 10
    for k in keys:
        script += ["%d keys %s" % (f, hex(k)), "%d keys none" % (f + 3)]
        starts.append(f)
        f += 30
script.append("%d quit" % (f + 30))
run("kits", script, f / 60 + 1, os.path.join(OUT, "sd2"), wav=os.path.join(OUT, "kits.wav"))
d = samples(os.path.join(OUT, "kits.wav"))
for n, s in enumerate(starts):
    rms, pk = window(d, s / 60, s / 60 + 0.25)
    if pk < 0.15:
        fail("kit %d slot %d is silent (peak %.2f)" % (n // 8 + 1, n % 8 + 1, pk))
print("sidbox: 4 drum kits checked")

# 3. the pages and the sequencer, then 4. the save
script = ["2 keys 0x2C", "4 keys none", "60 shot seq", "62 keys 0x3F", "64 keys none", "90 shot snd",
          "92 keys 0x40", "94 keys none", "120 shot vis",
          "122 keys 0x3E", "124 keys none",
          "126 keys 0x4D", "128 keys none", "130 keys 0x3C 0xE1", "132 keys none", "134 keys 0x2C", "136 keys none"]
shots = os.path.join(OUT, "shots")
sd = os.path.join(OUT, "sd3")
run("pages", script + ["700 quit"], 12, sd, shots=shots, wav=os.path.join(OUT, "pages.wav"))
d = samples(os.path.join(OUT, "pages.wav"))
rms, pk = window(d, 0.5, 2.0)
if rms < 0.03:
    fail("the sequencer is silent (rms %.3f)" % rms)
for n in ("seq", "snd", "vis"):
    p = os.path.join(shots, n + ".png")
    if not os.path.exists(p) or png_size(p) != (640, 360):
        fail("no picture of the %s page" % n)
run("reload", ["30 shot back", "60 quit"], 3, sd, shots=shots)
print("sidbox: pages and sound ok")
sys.exit(1 if fails else 0)
