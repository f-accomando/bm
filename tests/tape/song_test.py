#!/usr/bin/env python3
"""
song_test.py - the exit test of phase 3: a 4-track song saves, loads, and plays the same.

  python3 tests/tape/song_test.py build/host/bmhost-bin TEST_BM_60FPS

TEST_BM_60FPS is a test build (TAPE_TEST=1, carts/tape/build/build.py, carts/tape/test/hook.lua). The hook has three keys:
F9 builds a song (4 tracks, 8 slots, loop over 1..6, patterns A to D, levels, pans, sends, an edited sound on track 2, effects);
F10 logs the state of the song (a canonical text of the table that the save holds); and it logs every queued note
("TAPE_AT time track note velocity tick level pan room echo chorus").
Run A (empty SD): F9, P (SONG mode), Shift+4 (mute track 4), F10, Ctrl+S, Space; it logs the events of 13 s (a loop and a half).
Run B (the same SD): the cartridge loads the save at the start; F10, Space. The two event lists (time, track, note, velocity, level,
pan and sends) and the two state dumps must be equal. The same holds for a song that was saved by _exit() (PS button, Ctrl+Esc).
A save with another version, another magic or a cut file is refused: the state is the demo, with no Lua error. Ctrl+N asks twice
(a second press within 2 s). The size of the save is printed.
"""
import glob, os, re, shutil, subprocess, sys, tempfile

HOST, CART = sys.argv[1:3]
SPACE, F9, F10, KEY_P, KEY_4, KEY_S, KEY_N, SHIFT, CTRL = 0x2C, 0x42, 0x43, 0x13, 0x21, 0x16, 0x11, 0xE1, 0xE0
fails = []


def check(name, ok, detail=""):
    if not ok:
        fails.append(name)
    print(("ok   " if ok else "FAIL ") + name + ("  " + detail if detail and not ok else ""))


def press(frame, *usages):
    return ["%d keys %s" % (frame, " ".join(hex(u) for u in usages)), "%d keys none" % (frame + 2)]


def run(sd, script, seconds):
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write("\n".join(sorted(script, key=lambda l: int(l.split()[0]))) + "\n")
    p = subprocess.run([HOST, CART, "--input", f.name, "--sd", sd, "--seconds", str(seconds)], capture_output=True, text=True)
    os.unlink(f.name)
    out = p.stdout + p.stderr
    ev = re.findall(r"TAPE_AT (\S+) (\d+) (\d+) (\d+) (\d+) (\S+) (\S+) (\S+) (\S+) (\S+)", out)
    states = re.findall(r"TAPE_STATE (.*)", out)
    return ev, states, p.returncode == 0 and " ok" in p.stderr, out


def savefile(sd):
    files = glob.glob(os.path.join(sd, "**", "*.sav"), recursive=True)
    return files[0] if files else None


tmp = tempfile.mkdtemp(prefix="tape-song-")
sdA = os.path.join(tmp, "A")
os.makedirs(sdA)
build = press(5, F9) + press(20, KEY_P) + press(30, SHIFT, KEY_4)

# the demo state: an empty SD, F10 only
_, demo, ok, _ = run(os.path.join(tmp, "demo"), press(40, F10), 1.5)
check("an empty SD starts with the demo song (no Lua error)", ok and len(demo) == 1 and '"BMTAPE"' in demo[0])

# run A: build, dump, save with Ctrl+S, play
evA, stA, ok, out = run(sdA, build + press(40, F10) + press(50, CTRL, KEY_S) + press(60, SPACE), 14)
check("run A ends without a Lua error", ok)
f = savefile(sdA)
check("Ctrl+S wrote the save file", f is not None)
size = os.path.getsize(f) if f else 0
print("save file: %d bytes (%s)" % (size, os.path.basename(f) if f else "none"))
check("the save is small (under 8 KB)", 0 < size < 8192)
check("the song differs from the demo", len(stA) == 1 and stA[0] != demo[0])
check("run A played the song: events of tracks 1 to 3, none of the muted track 4",
      len(evA) > 100 and {int(e[1]) for e in evA} == {1, 2, 3})

# run B: the same SD, the cartridge loads the save at the start
evB, stB, ok, _ = run(sdA, press(40, F10) + press(60, SPACE), 14)
check("run B ends without a Lua error", ok)
check("the state after the load equals the state that was saved", len(stB) == 1 and stB == stA)
n = min(len(evA), len(evB))
check("the events of run B equal the events of run A (%d events: time, track, note, velocity, level, pan, sends)" % len(evA),
      len(evA) == len(evB) and evA == evB, "%d against %d" % (len(evA), len(evB)))
sends = {t: {(e[6], e[7], e[8], e[9]) for e in evB if e[1] == t} for t in "123"}
check("the pans and sends of the events are the saved ones (%s)" % sends,
      sends == {"1": {("-0.500", "0.100", "0.000", "0.000")}, "2": {("0.000", "0.300", "0.200", "0.000")},
                "3": {("0.400", "0.200", "0.400", "0.300")}})
check("the levels of the events are the saved ones (volume = level x velocity / 127)",
      all(abs(float(e[5]) - {"1": 0.8, "2": 0.6, "3": 0.9}[e[1]] * int(e[3]) / 127) < 0.0002 for e in evB))
at = lambda tick: {(e[1], e[2], e[3]) for e in evB if int(e[4]) == tick}
check("the loop (slots 1..6, 576 ticks) is kept: the play passes tick 576 and starts again with the first events",
      max(int(e[4]) for e in evB) > 576 and at(576) == at(0) != set())

# a song saved by _exit() (the PS button): the save is written by the exit, not by Ctrl+S
sdC = os.path.join(tmp, "C")
os.makedirs(sdC)
_, stC, ok, _ = run(sdC, press(5, F9) + press(20, KEY_P) + press(40, F10) + ["100 ps"], 3)
check("a run that ends with the PS button writes the save", ok and savefile(sdC) is not None)
_, stD, ok, _ = run(sdC, press(40, F10), 1.5)
check("the song saved by _exit() loads with the same state", ok and len(stC) == 1 and stC == stD)

# refused saves: another version, another magic, a cut file; the state is the demo
good = open(f, "rb").read()
cases = [("another version", good.replace(b'["ver"]=1', b'["ver"]=99', 1)), ("another magic", good.replace(b"BMTAPE", b"BMXXXX", 1)),
         ("a cut file", good[:len(good) // 2]), ("an empty file", b""), ("a bad pattern text", good.replace(b'["song"]=false', b'["song"]=5', 1) if b'["song"]=false' in good else good + b"x")]
for name, data in cases:
    sd = os.path.join(tmp, "bad")
    shutil.rmtree(sd, ignore_errors=True)
    shutil.copytree(sdA, sd)
    open(savefile(sd), "wb").write(data)
    _, st, ok, out = run(sd, press(40, F10) + press(60, SPACE), 2)
    check("a save with %s is refused: the demo loads, no Lua error" % name, ok and st == demo and "stopped with an error" not in out)

# Ctrl+N: the first press asks, the second within 2 s starts the demo; a second press after 2 s asks again
sd = os.path.join(tmp, "new")
shutil.copytree(sdA, sd)
_, st, ok, _ = run(sd, press(10, F10) + press(20, CTRL, KEY_N) + press(30, F10) + press(40, CTRL, KEY_N) + press(50, F10)
                   + press(300, CTRL, KEY_N) + press(310, F10), 6)
check("Ctrl+N: the first press changes nothing", ok and len(st) == 4 and st[0] == stA[0] and st[1] == st[0])
check("Ctrl+N: the second press within 2 s starts the demo", len(st) == 4 and st[2] == demo[0])
check("Ctrl+N: a press on the demo after 2 s only asks (no change)", len(st) == 4 and st[3] == demo[0])
sd = os.path.join(tmp, "new2")
shutil.copytree(sdA, sd)
_, st, ok, _ = run(sd, press(20, CTRL, KEY_N) + press(220, CTRL, KEY_N) + press(240, F10), 6)
check("Ctrl+N: two presses more than 2 s apart do not replace the song", ok and len(st) == 1 and st[0] == stA[0])

print("tape song: %s" % ("FAIL" if fails else "ok"))
shutil.rmtree(tmp, ignore_errors=True)
sys.exit(1 if fails else 0)
