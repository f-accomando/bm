#!/usr/bin/env python3
"""make test-web-bmm: file_write of a .bmm in bmhost (docs/api/system.md).

  run_bmm_test.py BMHOST CART.bm WORKDIR

Puts the committed models of web/models and some broken files in WORKDIR/sd/docs, runs the cart
(tests/web/bmm_cart.lua) and checks the folders of the card after the run: the models are in /bmm
byte for byte, the refused files are nowhere (not in /bmm, not in bm/web), no .bm/.bme/.b16 was made.
"""
import os
import re
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmres  # noqa: E402

MODELS = ["house", "tree", "crate", "walker"]
fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)
        print("FAIL", msg)


def main(host, cart, work):
    sd = os.path.join(work, "sd")
    shutil.rmtree(work, ignore_errors=True)
    docs = os.path.join(sd, "docs")
    os.makedirs(docs)
    good = {}
    for n in MODELS:
        good[n] = open(os.path.join(ROOT, "web", "models", n + ".bmm"), "rb").read()
        open(os.path.join(docs, n + ".bmm"), "wb").write(good[n])
    h = good["house"]
    flip = bytearray(h)
    flip[len(h) // 2] ^= 0x55                       # the CRC no longer fits
    img = bmres.File(2, "img")
    bmres.sheet_set(img, 32, 8, bytearray(b"\x10\x20\x30\xff" * (32 * 8)))
    bad = {
        "trunc": h[:len(h) - 40],                   # cut short
        "junk": b"<html>not a model</html>\n" * 20,
        "flip": bytes(flip),
        "empty": b"",
        "image": bmres.dump(img),                   # a good BMRES file, but not a model
        "cart": open(cart, "rb").read(),            # a good cartridge, but not a model
    }
    for n, b in bad.items():
        open(os.path.join(docs, n + ".dat"), "wb").write(b)
    out = subprocess.run([host, cart, "--sd", sd, "--seconds", "3"], capture_output=True, text=True,
                         timeout=120)
    log = out.stdout + out.stderr
    open(os.path.join(work, "run.log"), "w").write(log)
    for line in log.splitlines():
        if line.startswith("web_bmm"):
            print(line)
    m = re.search(r"^web_bmm: (\d+)/(\d+) checks passed", log, re.M)
    check(m is not None and m.group(1) == m.group(2), "the cart: all checks passed")
    bmm = os.path.join(sd, "bmm")
    names = sorted(os.listdir(bmm)) if os.path.isdir(bmm) else []
    check(names == sorted([n + ".bmm" for n in MODELS] + ["dir.bmm", "up.bmm"]), "files in /bmm: %r" % names)
    for n in MODELS:
        p = os.path.join(bmm, n + ".bmm")
        check(os.path.exists(p) and open(p, "rb").read() == good[n], n + ".bmm is the committed file, whole")
    for n in ("up", "dir"):
        p = os.path.join(bmm, n + ".bmm")
        check(os.path.exists(p) and open(p, "rb").read() == good["tree"], n + ".bmm is the tree")
    web = os.path.join(sd, "bm", "web")
    wnames = sorted(os.listdir(web)) if os.path.isdir(web) else []
    check(wnames == ["model.glb", "note.txt"], "files in bm/web: %r" % wnames)
    for dirpath, _, files in os.walk(sd):
        for f in files:
            if f.lower().endswith((".bm", ".bme", ".b16")):
                check(False, "a cartridge file was written: " + os.path.join(dirpath, f))
    print("test-web-bmm: %s" % ("FAIL (%d)" % len(fails) if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:4]))
