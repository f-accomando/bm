#!/usr/bin/env python3
"""
sound_test.py - bm Tape in bmhost with --wav: the demo pattern plays for 2 bars (4 s at 120 BPM).

  python3 tests/tape/sound_test.py build/host/bmhost-bin build/carts/tape.bm build/tape-test/demo.wav

Checks: the run ends well; the WAV holds sound in each of the two bars (RMS above a small floor, so the notes that
play_at queued did reach the synthesizer); no run of full-scale samples longer than 24 samples (clipping).
A second run plays each of the 20 sounds live on track 2 (key ] loads the next sound, a key is held for 1 s): every
sound must give an RMS above the floor, so no sound is silent because of a bad tone() table.
A third run plays each of the 24 PCM sounds (sounds 21 to 44; the samples of the bank rendered from Felucca-Engines): each
one must be heard and must not clip.
The test cannot judge how the sound is: that is for the ear, on the Pi.
"""
import array, math, os, subprocess, sys, tempfile, wave

host, cart, wav = sys.argv[1:4]
os.makedirs(os.path.dirname(wav), exist_ok=True)
fails = []


def check(name, ok):
    if not ok:
        fails.append(name)
    print(("ok   " if ok else "FAIL ") + name)


with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
    f.write("60 keys 0x2c\n62 keys none\n")        # Space at frame 60 (1 s)
p = subprocess.run([host, cart, "--input", f.name, "--seconds", "5.2", "--wav", wav, "--quiet"], capture_output=True, text=True)
os.unlink(f.name)
check("the run ends without a Lua error", p.returncode == 0 and " ok" in p.stderr)

w = wave.open(wav)
rate, ch = w.getframerate(), w.getnchannels()
pcm = array.array("h")
pcm.frombytes(w.readframes(w.getnframes()))
check("the WAV is 16 bits and holds %.1f s" % (len(pcm) / ch / rate), w.getsampwidth() == 2 and len(pcm) / ch / rate > 5)


def rms(a, b):
    seg = pcm[int(a * rate) * ch:int(b * rate) * ch]
    return math.sqrt(sum(x * x for x in seg) / max(1, len(seg))) / 32768


quiet = rms(0, 0.9)
bars = [rms(1.1, 3.1), rms(3.1, 5.1)]
print("RMS before Space %.4f, bar 1 %.4f, bar 2 %.4f" % ((quiet,) + tuple(bars)))
check("the demo is silent before Space", quiet < 0.005)
check("bar 1 holds sound (RMS above 0.02)", bars[0] > 0.02)
check("bar 2 holds sound (RMS above 0.02)", bars[1] > 0.02)

run = longest = 0
for x in pcm:
    run = run + 1 if abs(x) >= 32767 else 0
    longest = max(longest, run)
check("no long run of full-scale samples (longest %d, limit 24)" % longest, longest <= 24)
# the 20 sounds, one after the other on track 2: ] then the key U held for 1 s, 1.5 s for each
lines = []
for n in range(20):
    f = 10 + 90 * n
    lines += ["%d keys 0x30" % f, "%d keys none" % (f + 2), "%d keys 0x18" % (f + 6), "%d keys none" % (f + 66)]
with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
    f.write("\n".join(lines) + "\n")
wav2 = wav.replace(".wav", "-sounds.wav")
p = subprocess.run([host, cart, "--input", f.name, "--seconds", "31", "--wav", wav2, "--quiet"], capture_output=True, text=True)
os.unlink(f.name)
check("the 20 sounds: the run ends without a Lua error", p.returncode == 0 and " ok" in p.stderr)
w = wave.open(wav2)
pcm = array.array("h")
pcm.frombytes(w.readframes(w.getnframes()))
levels = [rms((10 + 90 * n + 6) / 60 + 0.05, (10 + 90 * n + 66) / 60) for n in range(20)]
print("RMS of the sounds: " + " ".join("%.3f" % x for x in levels))
quiet = [n + 1 for n, x in enumerate(levels) if x < 0.01]
run = longest = 0
for x in pcm:
    run = run + 1 if abs(x) >= 32767 else 0
    longest = max(longest, run)
check("the 20 sounds: no long run of full-scale samples (longest %d, limit 24)" % longest, longest <= 24)
check("every one of the 20 sounds is heard (RMS above 0.01; silent: %s)" % (quiet or "none"), not quiet)
# the 24 PCM sounds: 19 presses of ] bring sound 20, then ] and the key U held for 1 s for each of sounds 21 to 44
lines = []
for i in range(19):
    lines += ["%d keys 0x30" % (10 + 6 * i), "%d keys none" % (12 + 6 * i)]
T0 = 150
for n in range(24):
    f = T0 + 90 * n
    lines += ["%d keys 0x30" % f, "%d keys none" % (f + 2), "%d keys 0x18" % (f + 6), "%d keys none" % (f + 66)]
with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
    f.write("\n".join(lines) + "\n")
wav3 = wav.replace(".wav", "-pcm.wav")
p = subprocess.run([host, cart, "--input", f.name, "--seconds", str((T0 + 90 * 24) / 60 + 1), "--wav", wav3, "--quiet"], capture_output=True, text=True)
os.unlink(f.name)
check("the 24 PCM sounds: the run ends without a Lua error", p.returncode == 0 and " ok" in p.stderr)
w = wave.open(wav3)
pcm = array.array("h")
pcm.frombytes(w.readframes(w.getnframes()))
levels = [rms((T0 + 90 * n + 6) / 60 + 0.05, (T0 + 90 * n + 66) / 60) for n in range(24)]
print("RMS of the PCM sounds: " + " ".join("%.3f" % x for x in levels))
quiet = [n + 21 for n, x in enumerate(levels) if x < 0.01]
run = longest = 0
for x in pcm:
    run = run + 1 if abs(x) >= 32767 else 0
    longest = max(longest, run)
check("the 24 PCM sounds: no long run of full-scale samples (longest %d, limit 24)" % longest, longest <= 24)
check("every one of the 24 PCM sounds is heard (RMS above 0.01; silent: %s)" % (quiet or "none"), not quiet)
print("tape sound: %s" % ("FAIL" if fails else "ok"))
sys.exit(1 if fails else 0)
