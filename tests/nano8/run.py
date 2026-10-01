#!/usr/bin/env python3
"""nano8 on the PC (make test-nano8).

1. the loader reads every cart (n8cartinfo);
2. the translator turns the code of every cart into Lua 5.4 that compiles
   (luahost tests/nano8/xlat.lua);
3. the API test cart (tests/nano8/carts/api.p8) passes all its checks,
   played by the real nano8 cartridge (n8host);
4. every cart given (the ones shipped in carts/nano8/roms) plays for 20
   seconds with buttons pressed, without an error.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, errors="replace", **kw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default="build")
    ap.add_argument("carts", nargs="*")
    a = ap.parse_args()
    host = os.path.join(a.build, "host")
    main_lua = os.path.join(a.build, "nano8", "main.lua")
    api = os.path.join(HERE, "carts", "api.p8")
    carts = [api] + a.carts
    fails = 0

    print("nano8: loading")
    r = run([os.path.join(host, "n8cartinfo")] + carts)
    print(r.stdout, end="")
    if r.returncode:
        print("FAILED: a cart does not load")
        fails += 1

    with tempfile.TemporaryDirectory() as tmp:
        print("nano8: translating")
        codes = []
        for i, c in enumerate(carts):
            out = os.path.join(tmp, "%02d_%s.lua" % (i, os.path.basename(c)))
            run([os.path.join(host, "n8cartinfo"), "--code", out, c])
            codes.append(out)
        r = run([os.path.join(host, "luahost"), os.path.join(HERE, "xlat.lua"), main_lua] + codes)
        print(r.stdout + r.stderr, end="")
        if r.returncode:
            print("FAILED: translation")
            fails += 1

        # an SD card with the carts in carts/nano8
        sd = os.path.join(tmp, "sd")
        os.makedirs(os.path.join(sd, "carts", "nano8"))
        for c in carts:
            shutil.copy(c, os.path.join(sd, "carts", "nano8"))
        names = sorted(os.path.basename(c) for c in carts)

        print("nano8: API test cart")
        n = names.index("api.p8") + 1
        r = run([os.path.join(host, "n8host"), main_lua, "--root", sd, "--frames", "20",
                 "--exec", "NANO8.Ui.play(%d)" % n])
        lines = [l for l in r.stderr.splitlines() if "api:" in l or "fail " in l or "error" in l.lower()]
        print("\n".join("  " + l for l in lines))
        if "api: all tests passed" not in r.stderr or r.returncode:
            print(r.stdout + r.stderr)
            print("FAILED: API test cart")
            fails += 1

        print("nano8: playing every cart for 20 s")
        # O, X and the directions in turn, then the pause menu: open, close
        presses = []
        for k in range(0, 1200, 40):
            bit = (16, 32, 2, 1, 4, 8)[(k // 40) % 6]
            presses += ["--at", "%d:pad=%d" % (k + 20, bit), "--at", "%d:pad=0" % (k + 32)]
        for c in sorted(a.carts):
            n = names.index(os.path.basename(c)) + 1
            r = run([os.path.join(host, "n8host"), main_lua, "--root", sd, "--frames", "1200", "--quiet",
                     "--exec", "NANO8.Ui.play(%d)" % n] + presses)
            status = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else "?"
            print("  %-28s %s" % (os.path.basename(c), status))
            if r.returncode or "state=error" in r.stdout:
                print(r.stdout + r.stderr)
                print("FAILED: %s" % c)
                fails += 1

    if fails:
        print("nano8: %d FAILED" % fails)
        sys.exit(1)
    print("nano8: all passed")


if __name__ == "__main__":
    main()
