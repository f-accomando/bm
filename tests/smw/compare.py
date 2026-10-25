#!/usr/bin/env python3
"""Plays the same input in the emulator and in the port and compares Mario tick by tick.

    python3 tests/smw/compare.py ROM.sfc [TICKS]       (needs the venv with stable-retro; make build/host/bmhost-bin first)

The emulator runs with Mario unhurtable; the port is built with SMW_GOD=1 SMW_TRACE=1 (damage does nothing,
one log line a tick). The input is the same for both: right and run held, jump held 24 ticks of every 40.
Prints the error of x and y every 100 ticks and where the two first differ by more than 8 pixels."""
import math, os, re, subprocess, sys, tempfile
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT + "/scripts")


def keys_at(t):
    return ["right", "y"] + (["b"] if t % 40 < 24 else [])


def emulator(rom, ticks):
    from smw_capture import Game, RIGHT, Y, B
    g = Game(rom)
    g.to_first_level()
    out = []
    for t in range(ticks):
        g.poke(0x1497, 0x7F)
        g.step([RIGHT, Y] + ([B] if t % 40 < 24 else []))
        w = g.wram()
        out.append((w[0x94] | w[0x95] << 8, w[0x96] | w[0x97] << 8, w[0x13E4], w[0x7B] if w[0x7B] < 128 else w[0x7B] - 256))
    return out


def port(ticks):
    env = dict(os.environ, SMW_TRACE="1", SMW_GOD="1")
    tmp = tempfile.mkdtemp()
    subprocess.run([sys.executable, ROOT + "/carts/smw/build/build.py", ROOT + "/carts/smw/extracted", tmp], check=True,
                   capture_output=True, env=env)
    lines, last = [], None
    for t in range(ticks):                       # tick t runs in this frame of the 60 Hz host
        f = math.ceil((t + 1) * 1.2) - 1
        k = " ".join(keys_at(t))
        if k != last:
            lines.append("%d pad 1 %s" % (f, k))
            last = k
    inp = tmp + "/in.txt"
    open(inp, "w").write("\n".join(lines) + "\n")
    out = subprocess.run([ROOT + "/build/host/bmhost-bin", tmp + "/smw.b16", "--square", "--seconds", str(ticks * 1.2 / 60 + 2),
                          "--input", inp], capture_output=True, text=True)
    res = {}
    for m in re.finditer(r"T (\d+) ([-\d.]+) ([-\d.]+) ([-\d.]+) ([-\d.]+) (\w+) ([-\d.]+)", out.stdout + out.stderr):
        res[int(m.group(1))] = (float(m.group(2)), float(m.group(3)), m.group(6), float(m.group(4)), float(m.group(7)))
    return res


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    ticks = int(sys.argv[2]) if len(sys.argv) > 2 else 1200
    em = emulator(sys.argv[1], ticks)
    pt = port(ticks)
    first = None
    for lo in range(0, ticks, 100):
        ex, ey, n = 0.0, 0.0, 0
        for t in range(lo, min(lo + 100, ticks)):
            if t + 1 not in pt:
                continue
            px, py = pt[t + 1][0], pt[t + 1][1]
            dx, dy = abs(px - em[t][0]), abs(py - em[t][1])
            ex, ey, n = ex + dx, ey + dy, n + 1
            if first is None and (dx > 8 or dy > 8):
                first = (t, em[t], (px, py))
        print("ticks %4d-%4d  mean |dx| %6.2f  mean |dy| %6.2f  (emulator x %d)" % (lo, lo + 99, ex / max(n, 1), ey / max(n, 1), em[min(lo + 99, ticks - 1)][0]))
    print("first difference over 8 pixels:", first)
    if os.environ.get("DETAIL"):
        lo, hi = [int(v) for v in os.environ["DETAIL"].split("-")]
        for t in range(lo, hi, 3):
            if t + 1 in pt:
                print("%4d emu %5d,%4d vx %3d pm %3d | port %7.1f,%6.1f vx %5.1f pm %5.1f | d %5.1f,%5.1f" % (t, em[t][0], em[t][1], em[t][3], em[t][2],
                      pt[t + 1][0], pt[t + 1][1], pt[t + 1][3], pt[t + 1][4], pt[t + 1][0] - em[t][0], pt[t + 1][1] - em[t][1]))


if __name__ == "__main__":
    main()
