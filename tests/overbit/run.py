#!/usr/bin/env python3
"""
Overbit on the PC (bmhost): the training range played with scripted keys,
the whole animation reel and the benchmark, each checked in the game's log.

  tests/overbit/run.py BUILD_DIR
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")

# USB usages of the keys (as the game reads them with keydown)
K = {"W": 0x1A, "A": 0x04, "S": 0x16, "D": 0x07, "J": 0x0D, "K": 0x0E, "E": 0x08, "Q": 0x14,
     "SHIFT": 0xE1, "SPACE": 0x2C, "LEFT": 0x50, "RIGHT": 0x4F, "UP": 0x52, "F1": 0x3A, "F4": 0x3D, "ENTER": 0x28}


def keys(*names):
    return " ".join(hex(K[n]) for n in names) if names else "none"


def run(build, cart, seconds, script, name):
    path = os.path.join(build, "overbit", f"test-{name}.txt")
    with open(path, "w") as f:
        f.write(script)
    r = subprocess.run([os.path.join(build, "host", "bmhost-bin"), cart, "--seconds", str(seconds),
                        "--input", path], capture_output=True, text=True)
    log = r.stdout + r.stderr
    return r.returncode, log


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else "build"
    fails = 0

    def check(cond, what, log=""):
        nonlocal fails
        print(("ok   " if cond else "FAIL ") + what)
        if not cond:
            fails += 1
            print(log[-2000:])

    cart = os.path.join(build, "carts", "overbit.bm")
    # the range: from the menu, fire at the dummy until its mech breaks and
    # its pilot falls; the field, flight, rockets; then Redline (dev: F4)
    script = f"""
30 keys {keys('SPACE')}
32 keys none
100 keys {keys('J')}
700 keys {keys('K')}
760 keys {keys('SHIFT')}
764 keys none
900 keys {keys('E')}
904 keys none
1000 keys {keys('F1')}
1002 keys none
1010 keys {keys('F4')}
1012 keys none
1020 keys {keys('Q')}
1022 keys none
1300 keys {keys('F4')}
1302 keys none
1310 keys {keys('Q')}
1312 keys none
1500 keys {keys('ENTER')}
1502 keys none
"""
    code, log = run(build, cart, 27, script, "range")
    check(code == 0, "range: no Lua error", log)
    check("overbit kill You > Dummy" in log, "range: the dummy goes down (mech, then pilot)", log)
    check("overbit eject Dummy (mech destroyed)" in log, "range: the dummy's pilot ejects", log)
    check("overbit eject You (redline)" in log, "range: Redline ejects the pilot", log)
    check("overbit redline boom" in log, "range: the mech blows up", log)
    check("overbit pit stop You" in log, "range: Pit Stop brings a new mech", log)

    # the reel, every shot
    reel = os.path.join(build, "overbit", "reel.bm")
    code, log = run(build, reel, 108, "", "reel")
    shots = [l for l in log.splitlines() if l.startswith("reel ")]
    check(code == 0, "reel: no Lua error", log)
    check(len(shots) >= 41 and "FIRST PERSON" in log, f"reel: {len(shots)} shots", log)

    # the benchmark (on the PC every step is fast: it runs to the end)
    script = f"""
30 keys {keys('SPACE') if False else 'none'}
"""
    bench = os.path.join(build, "overbit", "bench.bm")
    code, log = run(build, bench, 40, "", "bench")
    check(code == 0 and "mechs at 60 fps" in log, "bench: runs to the end", log)
    print(f"\noverbit: {'all ok' if not fails else f'{fails} failed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
