#!/usr/bin/env python3
"""Runs a short test on a Pi through the bridge (tools/pi_bridge.py must be running: the owner types the console password there).

  python3 tools/pi_test.py --cart build/carts/pong.bm [--wait 15] [--gpu] [--bench] [--kernel build/kernel.img] [--restore build/kernel.img]
  python3 tools/pi_test.py --line "gpu"             # any monitor line

Steps, in this order, each one printed with its answer:  kernel (then the reboot is waited for), cart (--play), wait, gpu, bench, restore.
A step fails when the answer has a refusal ("refused", "wrong password", "No such file", "not a kernel", "file too big") or when the bridge is down.
The answer to --play only says the cartridge started: an error raised later by the cartridge shows on the Pi's own screen and is NOT seen here
(see docs/design/pi-test.md, request 1 and 2). Exit 0 = every step passed. The log is kept in out/pi_test.txt."""
import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
FAIL_WORDS = ("refused", "wrong password", "No such file", "not a kernel", "file too big", "bridge is not running", "error:")


def pi(*args):
    r = subprocess.run([sys.executable, os.path.join(HERE, "pi.py")] + list(args), capture_output=True, text=True, timeout=400)
    out = (r.stdout + r.stderr).strip()
    if r.returncode != 0 or "Traceback" in out:
        out += "\nerror: the client failed (exit %d)" % r.returncode
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel")
    ap.add_argument("--cart")
    ap.add_argument("--wait", type=int, default=10)
    ap.add_argument("--gpu", action="store_true")
    ap.add_argument("--bench", action="store_true")
    ap.add_argument("--line", action="append", default=[])
    ap.add_argument("--restore")
    a = ap.parse_args()
    steps = []
    if a.kernel:
        steps.append(("kernel " + a.kernel, ["kernel", a.kernel]))
    if a.cart:
        steps.append(("play " + a.cart, ["play", a.cart]))
        steps.append(("wait %d s" % a.wait, None))
    if a.gpu:
        steps.append(("gpu", ["line", "gpu"]))
    if a.bench:
        steps.append(("b3d", ["line", "b3d"]))
    for ln in a.line:
        steps.append(("line " + ln, ["line", ln]))
    if a.restore:
        steps.append(("restore kernel " + a.restore, ["kernel", a.restore]))
    if not steps:
        ap.error("nothing to do")
    os.makedirs(os.path.join(HERE, "..", "out"), exist_ok=True)
    log = open(os.path.join(HERE, "..", "out", "pi_test.txt"), "w", encoding="utf-8")
    ok = True
    for name, cmd in steps:
        if cmd is None:
            time.sleep(a.wait)
            out = "waited"
        else:
            out = pi(*cmd)
        # the progress lines of --kernel are long: keep only the last ones
        short = "\n".join(out.splitlines()[-6:])
        bad = any(w in out for w in FAIL_WORDS)
        ok = ok and not bad
        line = "%s  %s\n%s" % ("FAIL" if bad else "ok  ", name, short)
        print(line)
        log.write(line + "\n")
    print("RESULT:", "PASS" if ok else "FAIL")
    log.write("RESULT: %s\n" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
