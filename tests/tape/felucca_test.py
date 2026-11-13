#!/usr/bin/env python3
"""
felucca_test.py - the 24 PCM sounds of bm Tape, rendered on the PC from Felucca-Engines (core/scripts/felpack.py).

  python3 tests/tape/felucca_test.py build/tape build/bm/tape.bm

Checks: the 24 WAV files of build/tape/felucca/ exist (one for each entry of bm/tape/sounds/felucca.json), are 32000 Hz
mono 16 bits, are not silent and have a peak between -1.5 dB and -0.9 dB; the SHA-256 of each file equals the value
in tests/tape/felucca_golden.txt (the same commit of the engines gives the same bytes); the PCM total is 2.5 MB or less;
the sample names have 8 characters or less; the Lua source names the same 24 samples as the list; the bank of the
cartridge lists the 24 names (core/scripts/bmaudio.py).
"""
import array, hashlib, json, math, os, re, subprocess, sys, wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
tape, cart = sys.argv[1:3]
fails = []


def check(name, ok):
    if not ok:
        fails.append(name)
    print(("ok   " if ok else "FAIL ") + name)


sounds = json.load(open(os.path.join(ROOT, "bm", "tape", "sounds", "felucca.json")))["sounds"]
names = [x["sample"] for x in sounds]
gold = {}
for line in open(os.path.join(HERE, "felucca_golden.txt")):
    if line.strip() and not line.startswith("#"):
        n, h, size = line.split()
        gold[n] = (h, int(size))
check("the list holds 24 sounds with names of 8 characters or less", len(names) == 24 and all(len(n) <= 8 for n in names) and len(set(names)) == 24)
check("the golden file holds the same 24 names", list(gold) == names)

total, bad_fmt, silent, peaks, hashes = 0, [], [], [], []
for n in names:
    path = os.path.join(tape, "felucca", n + ".wav")
    if not os.path.exists(path):
        bad_fmt.append(n + " (missing)")
        continue
    data = open(path, "rb").read()
    total += len(data) - 44
    if (n in gold and hashlib.sha256(data).hexdigest() != gold[n][0]) or n not in gold:
        hashes.append(n)
    w = wave.open(path)
    if (w.getnchannels(), w.getsampwidth(), w.getframerate()) != (1, 2, 32000):
        bad_fmt.append(n)
    pcm = array.array("h")
    pcm.frombytes(w.readframes(w.getnframes()))
    peak = max((abs(x) for x in pcm), default=0)
    if peak == 0:
        silent.append(n)
        continue
    db = 20 * math.log10(peak / 32768)
    peaks.append(db)
    if not -1.5 <= db <= -0.9:
        bad_fmt.append("%s (peak %.2f dB)" % (n, db))
check("the 24 WAV files exist, are 32000 Hz mono 16-bit, with a peak from -1.5 dB to -0.9 dB (bad: %s)" % (bad_fmt or "none"), not bad_fmt)
check("no WAV file is silent (silent: %s)" % (silent or "none"), not silent)
if peaks:
    print("peaks %.3f .. %.3f dB" % (min(peaks), max(peaks)))
check("the SHA-256 of each WAV equals tests/tape/felucca_golden.txt (different: %s)" % (hashes or "none"), not hashes)
print("PCM total %d bytes (%.3f MB)" % (total, total / 1048576))
check("the PCM total is 2.5 MB or less", total <= 2621440)

lua = open(os.path.join(ROOT, "bm", "tape", "src", "25_sounds.lua")).read()
in_lua = re.findall(r'pcm\("[^"]+", "([A-Z0-9]+)", "[a-z]+"\)', lua)
check("25_sounds.lua names the 24 samples of the list in the same order", in_lua == names)

out = subprocess.run([sys.executable, os.path.join(ROOT, "core", "scripts", "bmaudio.py"), "show", cart], capture_output=True, text=True).stdout
listed = re.findall(r"^\s+sample\s+\d+ (\S+)", out, re.M)
check("the bank of %s lists the 24 names (%s ... %s)" % (os.path.basename(cart), listed[:1], listed[-1:]), listed == names)
print("tape felucca: %s" % ("FAIL" if fails else "ok"))
sys.exit(1 if fails else 0)
