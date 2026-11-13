#!/usr/bin/env python3
"""
sound_test.py - bm Tape in bmhost with --wav: the demo pattern plays for 2 bars (4 s at 120 BPM).

  python3 tests/tape/sound_test.py build/host/bmhost-bin build/bm/tape.bm build/tape-test/demo.wav

Checks: the run ends well; the WAV holds sound in each of the two bars (RMS above a small floor, so the notes that
play_at queued did reach the synthesizer); no run of full-scale samples longer than 24 samples (clipping).
A second run plays each of the 20 sounds live on track 2 (key ] loads the next sound, a key is held for 1 s): every
sound must give an RMS above the floor, so no sound is silent because of a bad tone() table.
A third run plays each of the 24 PCM sounds (sounds 21 to 44; the samples of the bank rendered from Felucca-Engines): each
one must be heard and must not clip.
Phase 3 (MIX, FX, punch-in effects), each one in its own run with the demo pattern:
  - all four track levels at 0 (MIX LEVEL page, encoders held down) give silence;
  - all four pans hard left (MIX PAN) give a left channel at least 4 times louder than the right (the WAV is stereo);
  - the echo wet and the send of track 2 up (FX ECHO) change the WAV after the transport stopped: a longer tail;
  - a test build (TAPE_TEST=1, the fifth argument) logs the queued events: Z (stutter) repeats the events of one step window
    every 6 ticks while held; X (half speed) lets a tick last twice as long while held, goes back on release and the tick count
    never goes back; the latency of both is printed (key down to the first changed event);
  - C (filter sweep) and V (crush) leave the event list as it is and change the WAV while the key is held: the sweep takes high
    frequencies away (the share of the first difference of the signal falls), the crush changes the signal.
The test cannot judge how the sound is: that is for the ear, on the Pi.
"""
import array, math, os, re, subprocess, sys, tempfile, wave

host, cart, wav = sys.argv[1:4]
hook_cart = sys.argv[4] if len(sys.argv) > 4 else None
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
    lines += ["%d keys 0xe6" % f, "%d keys none" % (f + 2), "%d keys 0x07" % (f + 6), "%d keys none" % (f + 66)]
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
    lines += ["%d keys 0xe6" % (10 + 6 * i), "%d keys none" % (12 + 6 * i)]
T0 = 150
for n in range(24):
    f = T0 + 90 * n
    lines += ["%d keys 0xe6" % f, "%d keys none" % (f + 2), "%d keys 0x07" % (f + 6), "%d keys none" % (f + 66)]
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


# ---------------------------------------------------------------- phase 3: MIX, FX, punch-in effects
# encoder down keys O P [ ] (named by their old letters A S D F), up keys - = (E R), punch-in 5 6 7 8 (Z X C V), modules F5 (MIX) and F6 (FX)
KEYS = {"A": 0x12, "S": 0x13, "D": 0x2F, "F": 0x30, "E": 0x2D, "R": 0x2E, "Z": 0x22, "X": 0x23, "C": 0x24, "V": 0x25,
        "SPACE": 0x2C, "F5": 0x3E, "F6": 0x3F, "TAB": 0x2B}


def keys_at(frame, *names):
    return ["%d keys %s" % (frame, " ".join(hex(KEYS[n]) for n in names)), "%d keys none" % (frame + 2)]


def hold(a, b, *names):
    return ["%d keys %s" % (a, " ".join(hex(KEYS[n]) for n in names)), "%d keys none" % b]


def render(script, seconds, cartpath=None, name="x"):
    """One run: the stereo samples as two arrays (left, right) and the TAPE_AT events if the cart is a test build."""
    path = os.path.join(os.path.dirname(wav), "p3-%s.wav" % name)
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write("\n".join(sorted(script, key=lambda l: int(l.split()[0]))) + "\n")
    p = subprocess.run([host, cartpath or cart, "--input", f.name, "--seconds", str(seconds), "--wav", path], capture_output=True, text=True)
    os.unlink(f.name)
    ok = p.returncode == 0 and " ok" in p.stderr
    w = wave.open(path)
    a = array.array("h")
    a.frombytes(w.readframes(w.getnframes()))
    ev = [(float(m[0]), int(m[1]), int(m[2]), int(m[3]), int(m[4])) for m in re.findall(r"TAPE_AT (\S+) (\d+) (\d+) (\d+) (\d+)", p.stdout + p.stderr)]
    return ok, a[0::2], a[1::2], ev


def rms2(ch, a, b):
    seg = ch[int(a * rate):int(b * rate)]
    return math.sqrt(sum(x * x for x in seg) / max(1, len(seg))) / 32768


def hf_share(ch, a, b):
    """The RMS of the first difference over the RMS of the signal: a low pass lowers it."""
    seg = ch[int(a * rate):int(b * rate)]
    d = math.sqrt(sum((seg[i] - seg[i - 1]) ** 2 for i in range(1, len(seg))) / max(1, len(seg) - 1))
    r = math.sqrt(sum(x * x for x in seg) / max(1, len(seg)))
    return d / max(r, 1.0)


# level 0 on all four tracks: silence
SP = 250                                                 # the frame of Space in the MIX runs
ok, L, R, _ = render(keys_at(5, "F5") + hold(10, 240, "A", "S", "D", "F") + keys_at(SP, "SPACE"), 7, name="lvl0")
check("level 0 on all four tracks: the run ends without a Lua error", ok)
quiet0 = max(rms2(L, SP / 60 + 0.3, 6.8), rms2(R, SP / 60 + 0.3, 6.8))
ok, L, R, _ = render(keys_at(5, "F5") + keys_at(SP, "SPACE"), 7, name="lvl1")
loud = rms2(L, SP / 60 + 0.3, 6.8)
print("RMS with the levels at 0: %.5f; at the default levels: %.4f" % (quiet0, loud))
check("level 0 on all four tracks gives silence (RMS below 0.001; with the levels up %.3f)" % loud, quiet0 < 0.001 and loud > 0.05)

# pan hard left on all four tracks: the left channel is louder
ok, L, R, _ = render(keys_at(5, "F5") + keys_at(10, "TAB") + hold(20, 130, "A", "S", "D", "F") + keys_at(140, "SPACE"), 8, name="panl")
check("pan hard left: the run ends without a Lua error", ok)
l, r = rms2(L, 3.0, 7.5), rms2(R, 3.0, 7.5)
ok, L2, R2, _ = render(keys_at(5, "F5") + keys_at(140, "SPACE"), 8, name="pan0")
l0, r0 = rms2(L2, 3.0, 7.5), rms2(R2, 3.0, 7.5)
print("RMS left %.4f, right %.4f with the pan hard left; left %.4f, right %.4f with the pan in the middle" % (l, r, l0, r0))
check("pan hard left gives a left channel at least 4 times the right one (the WAV is stereo)", l > 4 * r and abs(l0 - r0) < 0.2 * max(l0, r0))

# echo: the wet and the send of track 2 up; the transport stops at 5 s; the tail is measured from 0.4 s to 2.4 s after the stop
STOP = 130 + 300
base = keys_at(5, "F6") + keys_at(130, "SPACE") + keys_at(STOP, "SPACE")
ok, L0, R0, _ = render(base, 9, name="echo0")
ok2, L1, R1, _ = render(base + hold(10, 110, "E", "R"), 9, name="echo1")
check("echo: the runs end without a Lua error", ok and ok2)
t0, t1 = STOP / 60 + 0.4, STOP / 60 + 2.4
tail0, tail1 = rms2(L0, t0, t1) + rms2(R0, t0, t1), rms2(L1, t0, t1) + rms2(R1, t0, t1)
diff = sum(abs(a - b) for a, b in zip(L0[int(t0 * rate):int(t1 * rate)], L1[int(t0 * rate):int(t1 * rate)]))
print("echo tail RMS (left + right): %.4f without, %.4f with the wet and the send up" % (tail0, tail1))
check("echo wet and send up change the WAV after the notes stop: a tail at least 2 times louder", diff > 0 and tail1 > 2 * tail0 and tail1 > 0.005)

# punch-in effects. Z X C V are held from frame ON to frame OFF while the demo plays (Space at 60)
PLAY, ON, OFF = 60, 300, 420
punch = {k: keys_at(PLAY, "SPACE") + hold(ON, OFF, k) for k in "ZXCV"}
if not hook_cart:
    check("the fifth argument (a test build) is missing: the punch-in event checks cannot run", False)
else:
    base_ev = render(keys_at(PLAY, "SPACE"), 10, hook_cart, "ev0")[3]
    start = PLAY / 60 + 0.06                                # the transport's t0: Space plus the 60 ms start delay
    press = ON / 60 - start                                 # the key down, in seconds from the transport start
    rel = OFF / 60 - start
    okz, _, _, evz = render(punch["Z"], 10, hook_cart, "evZ")
    okx, _, _, evx = render(punch["X"], 10, hook_cart, "evX")
    check("stutter and half speed: the runs end without a Lua error", okz and okx)
    # stutter: from the first stutter event on, each block of 6 ticks holds the same events (track, note, velocity)
    after = [e for e in evz if e[0] >= press and e[0] < rel - 0.2]
    first_diff = next((e for e, b in zip(evz, base_ev) if e != b), None)
    blocks = {}
    for e in after:
        blocks.setdefault(e[4] // 6, set()).add((e[1], e[2], e[3], e[4] % 6))
    stut = [blocks[k] for k in sorted(blocks)]
    run_len = 0
    if first_diff:
        k0 = first_diff[4] // 6
        run = [blocks[k] for k in sorted(blocks) if k >= k0 and (k - k0) < 12 and k in blocks]
        run_len = len(run)
        repeated = len(run) >= 8 and all(b == run[0] for b in run[1:]) and len(run[0]) > 0
    else:
        repeated = False
    check("stutter (Z held 2 s): the events of one step window repeat every 6 ticks (%d blocks, %d events each)" % (run_len, len(stut[0]) if stut else 0), repeated)
    check("stutter: the event list differs from the run without Z and goes back to the pattern after the release",
          first_diff is not None and evz[-10:] == base_ev[-10:])
    if first_diff:
        print("stutter latency: key down at %.3f s, first changed event at %.3f s (%.0f ms)" % (press, first_diff[0], (first_diff[0] - press) * 1000))
        check("stutter latency is at most the look-ahead (150 ms) plus a frame", 0 <= first_diff[0] - press <= 0.19)
    # half speed: the time of a tick doubles while X is held
    def slope(ev, a, b):
        pts = [(e[0], e[4]) for e in ev if a <= e[0] <= b]
        d = [(pts[i + 1][0] - pts[i][0]) / (pts[i + 1][1] - pts[i][1]) for i in range(len(pts) - 1) if pts[i + 1][1] > pts[i][1]]
        return sum(d) / len(d) if d else 0
    nominal = 2.5 / 120
    held, free = slope(evx, press + 0.5, rel - 0.3), slope(evx, 0, press - 0.3)
    print("half speed: seconds a tick lasts %.4f before, %.4f while X is held (nominal %.4f)" % (free, held, nominal))
    check("half speed (X held 2 s): a tick lasts twice as long while held and as before outside", abs(held - 2 * nominal) < 0.0006 and abs(free - nominal) < 0.0003)
    n_hold = lambda ev: sum(1 for e in ev if press + 0.3 <= e[0] <= rel - 0.3)
    check("half speed halves the event rate while held (%d events against %d in the same time)" % (n_hold(evx), n_hold(base_ev)),
          0.35 * n_hold(base_ev) <= n_hold(evx) <= 0.65 * n_hold(base_ev))
    check("half speed: the tick count and the times never go back at the release (no jump)",
          all(b[4] >= a[4] and b[0] >= a[0] for a, b in zip(evx, evx[1:])))
    first_x = next((e for e, b in zip(evx, base_ev) if e != b), None)
    if first_x:
        print("half speed latency: key down at %.3f s, first event that is later than in the plain run at %.3f s (%.0f ms)" % (press, first_x[0], (first_x[0] - press) * 1000))
    # filter sweep and crush: the events stay the same, the WAV changes
    for key, label in (("C", "filter sweep"), ("V", "crush")):
        okp, Lp, Rp, evp = render(punch[key], 10, hook_cart, "ev" + key)
        okw, Lw, Rw, _ = render(punch[key], 10, cart, "wav" + key)
        okb, Lb, Rb, _ = render(keys_at(PLAY, "SPACE"), 10, cart, "wavbase")
        check("%s: the runs end without a Lua error" % label, okp and okw and okb)
        check("%s: the event list is the one of the plain run" % label, evp == base_ev)
        a, b = ON / 60 + 0.4, OFF / 60
        dd = sum(abs(x - y) for x, y in zip(Lw[int(a * rate):int(b * rate)], Lb[int(a * rate):int(b * rate)]))
        hw, hb = hf_share(Lw, a, b), hf_share(Lb, a, b)
        print("%s: first-difference share %.3f held, %.3f plain; summed difference %d" % (label, hw, hb, dd))
        if key == "C":
            check("filter sweep (C held 2 s): the WAV changes and loses high frequencies (share %.3f against %.3f)" % (hw, hb), dd > 100000 and hw < 0.8 * hb)
        else:
            seg = [(x - y) for x, y in zip(Lw[int(a * rate):int(b * rate)], Lb[int(a * rate):int(b * rate)])]
            rel_diff = math.sqrt(sum(x * x for x in seg) / len(seg)) / 32768 / rms2(Lb, a, b)
            check("crush (V held 2 s): the WAV changes (the difference is %.0f%% of the signal's RMS)" % (rel_diff * 100), rel_diff > 0.05)
        after_a, after_b = OFF / 60 + 0.8, OFF / 60 + 2.4
        check("%s: after the release the sound returns (share %.3f against %.3f)" % (label, hf_share(Lw, after_a, after_b), hf_share(Lb, after_a, after_b)),
              abs(hf_share(Lw, after_a, after_b) - hf_share(Lb, after_a, after_b)) < 0.15 * hf_share(Lb, after_a, after_b))

print("tape sound: %s" % ("FAIL" if fails else "ok"))
sys.exit(1 if fails else 0)
