#!/usr/bin/env python3
"""
m7test_cart_test.py - the SNES cart test build SNES_M7TEST=1 in bmhost.

  python3 tests/snes/m7test_cart_test.py build/host/bmhost-bin build/m7test.sfc

Builds the cart twice (flag on, flag off) in a temporary folder, puts the ROM in a fake snes folder
(with a second ROM named "aaa.sfc" that sorts first) and checks:
  - the test build opens m7test.sfc by itself and the ROM runs (log lines "snes rom" and "snes t=");
  - the script reaches the variants 1 2 3 4 5 1 in order, 20 s of emulated time apart, then stays in 1;
  - the flag off: the normal cart has no M7TEST code active and is the same with SNES_M7TEST=0.
"""
import hashlib, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
BUILD = os.path.join(ROOT, "carts", "snes", "build", "build.py")


def build(out, flag):
    env = dict(os.environ)
    env.pop("SNES_M7TEST", None)
    if flag is not None:
        env["SNES_M7TEST"] = flag
    os.makedirs(out, exist_ok=True)
    subprocess.check_call([sys.executable, BUILD, out], env=env, stdout=subprocess.DEVNULL)
    return os.path.join(out, "snes.bm"), open(os.path.join(out, "main.lua"), "rb").read()


def main():
    host, rom = sys.argv[1:3]
    tmp = tempfile.mkdtemp(prefix="snes-m7-")
    fails = []

    def check(name, ok):
        if not ok:
            fails.append(name)
        print(("ok   " if ok else "FAIL ") + name)

    cart_on, lua_on = build(tmp + "/on", "1")
    cart_off, lua_off = build(tmp + "/off", None)
    cart_zero, lua_zero = build(tmp + "/zero", "0")
    check("flag off: the normal cart keeps M7TEST = false", b"local M7TEST = false" in lua_off and b"local M7TEST = true" not in lua_off)
    check("flag 0 builds the same cart as no flag", lua_zero == lua_off)
    check("flag on: only the M7TEST line differs", lua_on.replace(b"local M7TEST = true", b"local M7TEST = false") == lua_off)
    check("flag off: the .bm is the same as with flag 0", hashlib.sha256(open(cart_off, "rb").read()).digest() == hashlib.sha256(open(cart_zero, "rb").read()).digest())

    os.makedirs(tmp + "/sd/snes"); os.makedirs(tmp + "/sd/docs"); os.makedirs(tmp + "/shots")
    shutil.copy(rom, tmp + "/sd/snes/m7test.sfc")
    shutil.copy(rom, tmp + "/sd/snes/aaa.sfc")             # sorts first: the test build must still pick m7test
    open(tmp + "/in.txt", "w").write("".join("%d shot v%d\n" % (600 + 1200 * k, k + 1) for k in range(6)))
    out = subprocess.run([host, cart_on, "--sd", tmp + "/sd", "--seconds", "135", "--shots", tmp + "/shots", "--input", tmp + "/in.txt"],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=True).stdout
    lines = out.splitlines()
    check("the ROM m7test.sfc opens by itself", any("snes rom m7test.sfc" in l for l in lines) and not any("snes rom aaa.sfc" in l for l in lines))
    secs = [l for l in lines if "snes t=" in l and "rom=m7test.sfc" in l]
    check("the ROM runs (snes t= lines of m7test.sfc)", len(secs) > 100)
    var = [re.search(r"m7test variant (\d) step=(\d) t=([\d.]+)", l) for l in lines if "m7test variant" in l]
    var = [m for m in var if m]
    got = [int(m.group(1)) for m in var]
    check("variants in order 1 2 3 4 5 1 (got %s)" % got, got == [1, 2, 3, 4, 5, 1])
    check("20 s of emulated time per variant", [float(m.group(3)) for m in var] == [0.0, 20.0, 40.0, 60.0, 80.0, 100.0])
    check("after the script the ROM stays in variant 1 (no exit, no restart)",
          len([l for l in lines if "snes rom " in l]) == 1 and len(var) == 6 and any("t=" in l for l in secs[-3:]))
    shots = sorted(os.listdir(tmp + "/shots"))
    digests = {hashlib.sha256(open(tmp + "/shots/" + s, "rb").read()).hexdigest() for s in shots}
    check("six pictures, the ROM draws something that changes with the variants", len(shots) == 6 and len(digests) >= 2)
    print("m7test cart test:", "ok" if not fails else "FAILED " + ", ".join(fails))
    if fails:
        print("\n".join(lines[-30:]))
    sys.exit(1 if fails else 0)


main()
