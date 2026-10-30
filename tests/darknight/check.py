#!/usr/bin/env python3
"""make test-darknight: Dark Night (carts/darknight) played by a script of keys in bmhost, on the PC.

  check.py BMHOST DARKNIGHT.bm WORKDIR

The script presses the keys of the game: run, the three dodges (roll, side jumps, backstep), the three saw attacks, SHIFT
(open the saw), an attack with the open saw, the charged attack, the pistol, F9 (the developer key: to the hall door), Q
(lock on), then stands still until the enemies kill the Hunter and he starts again. The game writes one line a
event ("darknight: dodge roll"); the test looks for each of them in the log, in that order. It also checks that the object
ids of main.lua are those of the map kit (art/maps/clinic/build_clinic.py) and that nothing in the log is a Lua error.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "art", "maps", "clinic"))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

W, A, S, D, SPACE, SHIFT = 0x1A, 0x04, 0x16, 0x07, 0x2C, 0xE1
Q, E, U, I, O, P, F9 = 0x14, 0x08, 0x18, 0x0C, 0x12, 0x13, 0x42

SCRIPT = []


def at(frame, *keys):
    SCRIPT.append("%d keys %s" % (frame, " ".join("0x%02X" % k for k in keys) if keys else "none"))


at(20, W); at(48, W); at(50, W, SPACE); at(80)                 # roll forward
at(120, D); at(122, D, SPACE); at(150)                         # side jump to the right
at(200, A); at(202, A, SPACE); at(230)                         # side jump to the left
at(280, SPACE); at(320)                                        # backstep
at(340, U); at(345); at(385, I); at(390); at(430, O); at(435) # the three attacks, each in the chain window of the one before
at(520, SHIFT); at(525)                                        # open the saw
at(560, U); at(565)                                            # an attack with the open saw
at(700, P); at(780)                                            # the charged attack: hold, let go
at(960, E); at(965)                                            # the pistol
at(1050, F9); at(1055)                                         # to the hall door: the door opens, an enemy waits
at(1110, Q); at(1115)                                          # lock on
at(1200, F9); at(1205)                                         # to the atrium: two enemies are there
SCRIPT.append("2700 quit")

EXPECT = ["dodge roll", "dodge right", "dodge left", "dodge back", "attack closed 1", "attack closed 2", "attack closed 3",
          "saw open", "attack open 1", "charge open", "shot", "skip 1", "door opens", "lock on", "skip 2", "hurt hp", "died", "respawn"]


def ids_of_main():
    text = open(os.path.join(ROOT, "carts", "darknight", "main.lua")).read()
    block = re.search(r"local ID = \{(.*?)\}", text, re.S).group(1)
    return {k: int(v) for k, v in re.findall(r"(\w+)\s*=\s*(\d+)", block)}


def main(host, cart, work):
    os.makedirs(work, exist_ok=True)
    fails = []

    def check(cond, msg):
        if not cond:
            fails.append(msg)
            print("FAIL", msg)
    import build_clinic as B
    ids = ids_of_main()
    want = dict(B.IDS)
    want["enemy"], want["start"] = B.ENEMY, B.PLAYER_START
    check(ids == want, "the ids of main.lua are those of the kit: %r vs %r" % (ids, want))
    radius = re.search(r"local RADIUS = \{(.*?)\}", open(os.path.join(ROOT, "carts", "darknight", "main.lua")).read(), re.S).group(1)
    got = {k: float(v) for k, v in re.findall(r"(\w+)\s*=\s*([\d.]+)", radius)}
    check(got == {n: float(r) for n, _f, _c, r in B.OBJECTS}, "the radii of main.lua are those of the kit")
    script = os.path.join(work, "input.txt")
    open(script, "w").write("\n".join(SCRIPT) + "\n")
    out = subprocess.run([host, cart, "--input", script, "--seconds", "46"], capture_output=True, text=True, timeout=400)
    log = out.stdout + out.stderr
    open(os.path.join(work, "run.log"), "w").write(log)
    lines = [l[len("darknight: "):] for l in log.splitlines() if l.startswith("darknight: ")]
    pos = 0
    for want_line in EXPECT:
        for k in range(pos, len(lines)):
            if lines[k].startswith(want_line):
                pos = k + 1
                break
        else:
            check(False, "no event %r after event %d (lines: %s)" % (want_line, pos, ", ".join(lines[:60])))
            break
    check(any(re.match(r"\d+ objects, \d+ enemies", l) for l in lines), "the start line")
    check(re.search(r'bmhost: ".*" \d+ frames, ok', log) is not None, "bmhost ended ok")
    check("error" not in log.lower().replace("no error", ""), "no error in the log")
    print("darknight: %d events, %s" % (len(lines), "ok" if not fails else "FAIL (%d)" % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:4]))
