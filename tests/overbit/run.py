#!/usr/bin/env python3
"""
Overbit on the PC (bmhost): the training range played with scripted keys
(Rally from the menu, then each other hero), the whole animation reel and
the benchmark, each checked in the game's log.

  tests/overbit/run.py BUILD_DIR
"""
import os
import re
import shutil
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


def run(build, cart, seconds, script, name, host="bmhost-bin", extra=()):
    path = os.path.join(build, "overbit", f"test-{name}.txt")
    with open(path, "w") as f:
        f.write(script)
    r = subprocess.run([os.path.join(build, "host", host), cart, "--seconds", str(seconds),
                        "--input", path] + list(extra), capture_output=True, text=True)
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
26 keys {keys('DOWN')}
28 keys none
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
                  hold(250, 253, "F1") + hold(260, 263, "F4") + hold(270, 273, "Q") + hold(280, 560, "J"),
                  ["overbit biotic field You", "overbit tactical visor You", "overbit kill You > Dummy"], 10),
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
    check("overbit hero You kaiju" in log, "match: Kaiju chosen in the spawn room", log)
    check("overbit point open" in log, "match: the point opens", log)

    # no graphics options in the title; the calibration (BENCHMARK, or the
    # flags): the first step of the ladder that holds 60 fps is saved on the
    # SD card and is the default of the next start. On the ARM (bmhost has no
    # GPU) the ladder is 640x360 only.
    sd = os.path.join(build, "overbit", "test-cal-sd")
    shutil.rmtree(sd, ignore_errors=True)
    os.makedirs(sd)
    code, log = run(build, os.path.join(build, "overbit", "bench-cal.bm"), 60, "", "cal", "bmhost-bin", ["--sd", sd])
    check(code == 0 and "stopped with an error" not in log and "overbit bench done" in log, "calibration: runs to the end", log)
    check("calibration" in log and "overbit default 640x360 quality" in log, "calibration: the default saved (640x360 on the ARM)", log)
    check("bm: screen 480x270" not in log.split("overbit bench done")[-1], "calibration: the screen is not put back over the default", log)
    code, log = run(build, cart, 1, "", "cal-again", "bmhost-bin", ["--sd", sd])
    check("overbit screen 640x360" in log and "bm: screen 640x360" in log, "calibration: the default is set again at the start", log)
    code, log = run(build, cart, 1, "", "no-save", "bmhost-bin")
    check("overbit screen 640x360" in log, "defaults: the best screen of the console when none is saved", log)
    code, log = run(build, os.path.join(build, "overbit", "range-1080.bm"), 3,
                    f"30 keys {keys('J')}\n150 keys none\n", "range-1080", "bmhost-gpu")
    check(code == 0 and "stopped with an error" not in log and "bm: screen 1920x1080" in log,
          "resolution: the range at 1920x1080 on the GPU", log)
    check("the 3D is drawn by the ARM from here" not in log, "resolution: 1920x1080 without falling back to the ARM", log)

    # a whole match with quick rules: the bots fight over the point until a
    # team wins two rounds
    code, log = run(build, os.path.join(build, "overbit", "match-fast.bm"), 240,
                    f"30 keys {keys('SPACE')}\n32 keys none\n", "match-fast")
    check(code == 0 and "stopped with an error" not in log, "match-fast: no Lua error", log)
    check("overbit point team" in log, "match-fast: the point is captured", log)
    check("overbit kill" in log, "match-fast: the bots fight", log)
    check("overbit match team" in log, "match-fast: a team wins the match", log)

    # a match on the network: two bmhost (BMHOST_NET_ID: two consoles on one
    # PC), one hosts, the other joins from the list; lockstep must give the
    # same match on both (the same events, no desync), on the LAN and through
    # the relay (tools/overbit_relay.py)
    def net_pair(cart, secs, name, relay=False):
        # (the guest looks at the list after 5 s, the host starts after 9 s)
        host = f"""30 keys {keys('DOWN')}\n32 keys none\n40 keys {keys('SPACE')}\n42 keys none
60 keys {keys('SPACE')}\n62 keys none\n540 keys {keys('SPACE')}\n542 keys none
600 keys {keys('SPACE')}\n602 keys none\n640 keys {keys('W')}\n1300 keys none\n"""
        guest = f"""30 keys {keys('DOWN')}\n32 keys none\n40 keys {keys('SPACE')}\n42 keys none
300 keys {keys('DOWN')}\n302 keys none\n305 keys {keys('DOWN')}\n307 keys none\n310 keys {keys('DOWN')}
312 keys none\n320 keys {keys('SPACE')}\n322 keys none\n600 keys {keys('SPACE')}\n602 keys none
640 keys {keys('W', 'D')}\n1300 keys none\n1500 ps\n1520 keys {keys('SPACE')}\n1522 keys none\n"""
        procs = []
        for k, sc in enumerate((host, guest)):
            path = os.path.join(build, "overbit", f"test-{name}-{k}.txt")
            with open(path, "w") as f:
                f.write(sc)
            env = dict(os.environ, BMHOST_NET_ID=str(k))
            procs.append(subprocess.Popen([os.path.join(build, "host", "bmhost-bin"), cart, "--seconds", str(secs),
                                           "--realtime", "--input", path], env=env, stdout=subprocess.PIPE,
                                          stderr=subprocess.STDOUT, text=True))
        logs = [p.communicate()[0] for p in procs]
        ev = [[l for l in g.splitlines() if l.startswith("overbit ") and " net " not in l and "build" not in l
               and "quality" not in l] for g in logs]
        both = "\n".join(logs)
        check(all("overbit net start" in g for g in logs), f"{name}: both consoles start the match", both)
        check(" seat 6 of 2, guest" in logs[1], f"{name}: the guest sits on the red team", both)
        check("desync" not in both, f"{name}: no desync", both)
        # the names: "You" on each console is the other's P1 or P6
        same = [e.replace(" You", " @6").replace(" P1", " You").replace(" @6", " P6") for e in ev[1]]
        n = min(len(ev[0]), len(same))
        check(n > 10 and ev[0][:n - 2] == same[:n - 2], f"{name}: the same match on both ({n} events)", both)
        # the guest leaves with PS (online(): the question, Space says yes):
        # it tells the host, whose bot takes the seat at once (not after 3 s)
        check("bm: leave the online game?" in logs[1] and "bm: left the online game" in logs[1]
              and "overbit net: left with PS" in logs[1], f"{name}: PS asks the guest, yes leaves", both)
        check("overbit net seat 6 left" in logs[0] and "seat 6 gone" not in logs[0],
              f"{name}: the host hears the guest leave", both)
        check("bm: leave the online game?" not in logs[0], f"{name}: only the player leaving is asked", both)

    net_pair(os.path.join(build, "overbit", "net-test.bm"), 33, "net-lan")
    relay = subprocess.Popen([sys.executable, os.path.join(ROOT, "tools", "overbit_relay.py"), "--port", "47390",
                              "--quiet"])
    try:
        net_pair(os.path.join(build, "overbit", "net-relay.bm"), 33, "net-relay")
    finally:
        relay.kill()

    # nnet() (src/ai/net.c), the bots' networks: the console's integers are
    # the Python reference's (scripts/nnetlib.py), on a random network
    sys.path.insert(0, os.path.join(ROOT, "scripts"))
    import numpy as np
    import mkbm
    import nnetlib
    rng = np.random.default_rng(3)
    layers = [(rng.normal(0, 0.5, (32, 24)), rng.normal(0, 0.1, 32), True),
              (rng.normal(0, 0.3, (32, 32)), rng.normal(0, 0.1, 32), True),
              (rng.normal(0, 0.3, (6, 32)), rng.normal(0, 0.1, 6), False)]
    X = rng.normal(0, 0.5, (200, 24))
    q = nnetlib.quantize(layers, X)
    lua = ["local net = nnet(" + nnetlib.lua_string(nnetlib.pack(q)) + ")", "function _init()"]
    for i, x in enumerate(X[:20]):
        args = ",".join(f"{v:.6f}" for v in x)
        lua.append(f"  do local o, k = net:run({{{args}}}), net:pick({{{args}}})")
        lua.append(f"    log(string.format('nnet {i} %d' .. string.rep(' %.4f', 6), k, table.unpack(o))) end")
    lua += ["end", "function _update() end", "function _draw() cls() end"]
    path = os.path.join(build, "overbit", "nnet-test.bm")
    with open(path, "wb") as f:
        f.write(mkbm.pack("\n".join(lua).encode(), title="nnet", author="bm", res=(320, 180)))
    code, log = run(build, path, 0.1, "", "nnet")
    got = {int(l.split()[1]): l.split()[2:] for l in log.splitlines() if l.startswith("nnet ")}
    bad = 0
    for i, x in enumerate(X[:20]):
        y = nnetlib.run_q(q, x)
        g = got.get(i)
        if not g or int(g[0]) != int(np.argmax(y)) + 1 or max(abs(float(a) - b) for a, b in zip(g[1:], y)) > 1e-3 * max(1, abs(y).max()):
            bad += 1
    check(code == 0 and len(got) == 20 and bad == 0, f"nnet: the console's outputs are the reference's ({bad} differ)", log)

    # the benchmark in short: the bots' match on the ARM (bmhost has no GPU),
    # then on the ARM and the GPU (bmhost-gpu: the V3D emulated), the same
    # match each time; the ring; the report
    bench = os.path.join(build, "overbit", "bench-fast.bm")
    for host, rs in (("bmhost-bin", ["ARM"]), ("bmhost-gpu", ["ARM", "GPU", "GPU+AA", "GPU+Q", "GPU+VS1", "GPU+VS", "GPU+VS+Q"])):
        code, log = run(build, bench, 60, "", "bench-" + host, host)
        tag = "gpu" if "gpu" in host else "arm"
        rows = re.findall(r"overbit bench (\S+) \d+x\d+ HIGH: .* (\d+) vtx", log)
        hashes = re.findall(r"overbit bench \S+ \d+x\d+ HIGH match (\d+)", log)
        check(code == 0 and "overbit bench done" in log and "Lua error" not in log,
              f"bench ({tag}): runs to the end", log)
        check("bmhost: report overbit-bench" in log, f"bench ({tag}): its report (report())", log)
        check([r[0] for r in rows] == rs, f"bench ({tag}): the phases {' '.join(rs)}", log)
        # the same match each time (where everyone is at the end); about the
        # same vertices drawn by the GPU with and without MSAA (the ARM draws
        # more: the GPU turns the far heroes coarser sooner, 40_actor; with
        # the vertex shader the ARM transforms fewer)
        vtx = [int(r[1]) for r in rows if r[0] in ("GPU", "GPU+AA")]
        check(len(hashes) == len(rs) and len(set(hashes)) == 1 and (not vtx or max(vtx) - min(vtx) <= max(vtx) // 100),
              f"bench ({tag}): the same match on every renderer", log)
        check(len(re.findall(r"overbit bench ring \S+: \d+ heroes at 60 fps", log)) == len(rs),
              f"bench ({tag}): the ring on each renderer", log)
        check("the 3D is drawn by the ARM from here" not in log, f"bench ({tag}): no fall back to the ARM", log)
    print(f"\noverbit: {'all ok' if not fails else f'{fails} failed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
