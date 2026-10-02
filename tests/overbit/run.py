#!/usr/bin/env python3
"""
Overbit on the PC (bmhost): the training range played with scripted keys
(Rally from the menu, then each other hero), the whole animation reel and
the benchmark, each checked in the game's log.

  tests/overbit/run.py BUILD_DIR
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")

# USB usages of the keys (as the game reads them with keydown)
K = {"W": 0x1A, "A": 0x04, "S": 0x16, "D": 0x07, "J": 0x0D, "K": 0x0E, "E": 0x08, "Q": 0x14,
     "SHIFT": 0xE1, "SPACE": 0x2C, "LEFT": 0x50, "RIGHT": 0x4F, "UP": 0x52, "DOWN": 0x51, "F1": 0x3A, "F4": 0x3D,
     "F5": 0x3E, "ENTER": 0x28, "H": 0x0B}


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
30 keys {keys('DOWN')}
32 keys none
34 keys {keys('SPACE')}
36 keys none
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

    # every other hero in the range: its abilities and its ultimate (F1 the
    # dev kit, F4 a full ultimate, F5 lose this life), checked in the log
    def hold(f0, f1, *names):
        return [f"{f0} keys {keys(*names)}", f"{f1} keys none"]

    HEROES = {
        "kaiju": (hold(10, 110, "W") + hold(110, 300, "W", "J") + hold(320, 380, "E") + hold(380, 384, "E", "J") +
                  hold(400, 404, "SHIFT") + hold(420, 520, "K") +
                  hold(530, 533, "F1") + hold(540, 543, "F4") + hold(550, 553, "Q") +
                  hold(640, 643, "F5") + hold(700, 760, "J") + hold(770, 773, "F4") + hold(780, 783, "Q"),
                  ["overbit limit break You", "overbit eject You (mech destroyed)", "overbit call mech You"], 15),
        "sarge": (hold(10, 60, "W", "SHIFT") + hold(60, 200, "J") + hold(210, 213, "K") + hold(230, 233, "E") +
                  hold(250, 253, "F1") + hold(260, 263, "F4") + hold(270, 273, "Q") + hold(280, 400, "J"),
                  ["overbit biotic field You", "overbit tactical visor You", "overbit kill You > Dummy"], 8),
        "frost": (hold(10, 60, "W") + hold(60, 200, "W", "J") + hold(210, 213, "K") + hold(230, 245, "DOWN") +
                  hold(250, 253, "E") + hold(260, 272, "UP") + hold(290, 293, "SHIFT") + hold(360, 363, "SHIFT") +
                  hold(400, 403, "F1") + hold(410, 413, "F4") + hold(420, 423, "Q"),
                  ["overbit ice wall You", "overbit cryo-freeze You", "overbit blizzard You", "overbit frozen Dummy"], 11),
        "fuse": (hold(10, 40, "W") + hold(50, 200, "J") + hold(210, 213, "SHIFT") + hold(250, 253, "K") +
                 hold(300, 303, "E") + hold(400, 403, "F1") + hold(410, 413, "F4") + hold(420, 423, "Q") +
                 hold(450, 600, "W") + hold(600, 603, "J"),
                 ["overbit mine You", "overbit boom wheel You"], 11),
        "rail": (hold(10, 40, "W") + hold(40, 200, "J") + hold(210, 213, "K") + hold(260, 263, "E") +
                 hold(330, 334, "W", "SHIFT") + hold(334, 356, "W") + hold(356, 359, "W", "SPACE") +
                 hold(420, 423, "F1") + hold(430, 433, "F4") + hold(440, 443, "Q") + hold(460, 463, "K"),
                 ["overbit rail You", "overbit disruptor You", "overbit overclock You"], 9),
        "orbit": (hold(10, 22, "RIGHT") + hold(30, 100, "J") + hold(110, 122, "LEFT") + hold(130, 200, "K") +
                  hold(250, 253, "E") + hold(280, 283, "SPACE") + hold(292, 340, "SPACE") + hold(360, 363, "SHIFT") +
                  hold(400, 403, "F1") + hold(410, 413, "F4") + hold(420, 423, "Q"),
                  ["overbit torpedoes You", "overbit hyper ring You", "overbit orbital ray You"], 9),
        "akari": (hold(10, 22, "RIGHT") + hold(30, 100, "J") + hold(110, 122, "LEFT") + hold(130, 133, "K") +
                  hold(170, 182, "RIGHT") + hold(190, 193, "SHIFT") + hold(220, 223, "E") +
                  hold(260, 263, "F1") + hold(270, 273, "F4") + hold(280, 283, "Q"),
                  ["overbit swift step You > Buddy", "overbit suzu You", "overbit kitsune rush You"], 7),
    }
    for hero, (lines, wants, secs) in HEROES.items():
        script = "\n".join(lines) + "\n"
        code, log = run(build, os.path.join(build, "overbit", f"range-{hero}.bm"), secs, script, f"range-{hero}")
        check(code == 0 and "stopped with an error" not in log, f"{hero}: no Lua error", log)
        for w in wants:
            check(w in log, f"{hero}: {w[8:]}", log)

    # the reel, every shot
    reel = os.path.join(build, "overbit", "reel.bm")
    code, log = run(build, reel, 340, "", "reel")
    shots = [l for l in log.splitlines() if l.startswith("reel ")]
    check(code == 0, "reel: no Lua error", log)
    check(len(shots) >= 90 and "FIRST PERSON" in log, f"reel: {len(shots)} shots", log)
    for name in ("KAIJU", "SARGE", "FROST", "FUSE", "RAIL", "ORBIT", "AKARI"):
        check(f" {name}" in log, f"reel: {name.lower()}'s shots", log)

    # the match from the menu: the hero select (Rally), out of the spawn,
    # back in to change hero (H: Kaiju), the point opens after 20 s
    script = f"""
30 keys {keys('SPACE')}
32 keys none
60 keys {keys('SPACE')}
62 keys none
100 keys {keys('H')}
102 keys none
110 keys {keys('DOWN')}
112 keys none
120 keys {keys('SPACE')}
122 keys none
130 keys {keys('W')}
600 keys none
"""
    code, log = run(build, cart, 26, script, "match")
    check(code == 0 and "stopped with an error" not in log, "match: no Lua error", log)
    check("overbit match start: rally" in log, "match: starts with Rally", log)
    check("overbit hero kaiju" in log, "match: Kaiju chosen in the spawn room", log)
    check("overbit point open" in log, "match: the point opens", log)

    # a whole match with quick rules: the bots fight over the point until a
    # team wins two rounds
    code, log = run(build, os.path.join(build, "overbit", "match-fast.bm"), 240,
                    f"30 keys {keys('SPACE')}\n32 keys none\n", "match-fast")
    check(code == 0 and "stopped with an error" not in log, "match-fast: no Lua error", log)
    check("overbit point team" in log, "match-fast: the point is captured", log)
    check("overbit kill" in log, "match-fast: the bots fight", log)
    check("overbit match team" in log, "match-fast: a team wins the match", log)

    # the benchmark (on the PC every step is fast: it runs to the end)
    script = f"""
30 keys {keys('SPACE') if False else 'none'}
"""
    bench = os.path.join(build, "overbit", "bench.bm")
    code, log = run(build, bench, 40, "", "bench")
    check(code == 0 and "heroes at 60 fps" in log, "bench: runs to the end", log)
    print(f"\noverbit: {'all ok' if not fails else f'{fails} failed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
