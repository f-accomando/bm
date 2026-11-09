#!/usr/bin/env python3
"""
felpack.py - renders the sounds of carts/tape/sounds/felucca.json with Felucca-Engines (MIT) on the PC and writes
the bank input for scripts/bmaudio.py. Standard library only. The C compiler is $CC (default cc).

  python3 scripts/felpack.py [outdir]        (default build/tape)

Steps:
  1. tables:   third_party/felucca-engines/tools/gen_engine_tables.py --fs 32000  ->  outdir/felucca/fe_tables.h
  2. compile:  tools/felrender/felrender.c  ->  outdir/felucca/felrender
  3. render:   each sound of the list is one note (root note, held time) -> raw WAV
  4. finish:   cut the tail at -60 dB of the peak, fade the last 10 ms, set the peak to -1 dB
               -> outdir/felucca/NAME.wav  (32000 Hz, mono, 16 bits)
  5. bank:     outdir/bank.json  {"samples": [{"name", "wav", "root", "loop": "off"}]}
It prints the size of each sample and the total. It stops with an error when the total is over 2.5 MB.
Every step uses integers, so the same commit of the engines gives the same WAV bytes
(tests/tape/felucca_golden.txt holds the SHA-256 of each file).
"""
import json
import os
import subprocess
import sys
import wave

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
FEL = os.path.join(ROOT, "third_party", "felucca-engines")
LIST = os.path.join(ROOT, "carts", "tape", "sounds", "felucca.json")
RENDER_C = os.path.join(ROOT, "tools", "felrender", "felrender.c")
FS = 32000
BUDGET = 2621440            # 2.5 MB
PEAK = 29204                # -1 dB of 32767: round(32767 * 10 ** (-1 / 20))
FADE_MS = 10
NOTES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi(name):
    """'C2' -> 36, 'C3' -> 48 (C4 = 60, as scripts/bmaudio.py)"""
    n = NOTES[name[0]] + (1 if name[1] == "#" else 0)
    return n + 12 * (int(name[-1]) + 1)


def sh(cmd):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    if r.returncode:
        sys.exit("felpack: %s failed:\n%s%s" % (" ".join(cmd), r.stdout, r.stderr))
    return r.stdout


def finish(raw_path, wav_path):
    """cut the tail at -60 dB of the peak, fade the last 10 ms, set the peak to -1 dB; integer arithmetic"""
    with wave.open(raw_path, "rb") as w:
        if (w.getnchannels(), w.getsampwidth(), w.getframerate()) != (1, 2, FS):
            sys.exit("felpack: %s is not 32000 Hz mono 16-bit" % raw_path)
        b = w.readframes(w.getnframes())
    s = [int.from_bytes(b[i:i + 2], "little", signed=True) for i in range(0, len(b), 2)]
    peak = max(abs(x) for x in s)
    if peak == 0:
        sys.exit("felpack: %s is silent" % raw_path)
    thr = peak // 1000                                  # -60 dB
    last = max(i for i, x in enumerate(s) if abs(x) >= thr)
    s = s[:last + 1]
    f = min(len(s), FS * FADE_MS // 1000)
    for k in range(f):                                  # a straight fade to zero
        i = len(s) - f + k
        s[i] = int(s[i] * (f - k) / f)
    peak = max(abs(x) for x in s)
    out = []
    for x in s:                                         # x * PEAK / peak, rounded away from zero
        v = (abs(x) * PEAK * 2 + peak) // (peak * 2)
        out.append(-v if x < 0 else v)
    with wave.open(wav_path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(FS)
        w.writeframes(b"".join(x.to_bytes(2, "little", signed=True) for x in out))
    return len(out)


def main():
    out = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, "build", "tape")
    d = os.path.join(out, "felucca")
    raw = os.path.join(d, "raw")
    os.makedirs(raw, exist_ok=True)
    sounds = json.load(open(LIST))["sounds"]
    tables = os.path.join(d, "fe_tables.h")
    sh([sys.executable, os.path.join(FEL, "tools", "gen_engine_tables.py"), "--fs", str(FS), tables])
    exe = os.path.join(d, "felrender")
    sh([os.environ.get("CC", "cc"), "-O2", "-std=c99", "-w", "-I" + d, "-I" + os.path.join(FEL, "src"),
        "-o", exe, RENDER_C])
    known = {}
    for line in sh([exe, "-l"]).splitlines():
        eng, n, name = line.split(" ", 2)
        known[(eng, name)] = int(n)
    bank, total, names = [], 0, set()
    print("felpack: %d sounds at %d Hz" % (len(sounds), FS))
    for x in sounds:
        name, eng, preset = x["sample"], x["engine"], x["preset"]
        if len(name) > 8 or name in names:
            sys.exit("felpack: bad or double sample name " + name)
        names.add(name)
        n = known.get((eng, preset))
        if n is None or n != x["n"]:
            sys.exit("felpack: %s %s is preset %s in the source, the list says %s" % (eng, preset, n, x["n"]))
        r = os.path.join(raw, name + ".wav")
        wav = os.path.join(d, name + ".wav")
        sh([exe, eng, str(n), str(midi(x["root"])), str(x["held_ms"]), r])
        frames = finish(r, wav)
        total += frames * 2
        bank.append({"name": name, "wav": "felucca/" + name + ".wav", "root": x["root"], "loop": "off"})
        print("  %-8s %-6s %-11s %s %5.2f s %8d bytes" % (name, eng, preset, x["root"], frames / FS, frames * 2))
    print("felpack: total %d bytes (%.3f MB of %.1f MB)" % (total, total / 1048576, BUDGET / 1048576))
    if total > BUDGET:
        sys.exit("felpack: the samples are over 2.5 MB (%d bytes)" % total)
    json.dump({"samples": bank}, open(os.path.join(out, "bank.json"), "w"), indent=1)
    open(os.path.join(out, "bank.json"), "a").write("\n")


main()
