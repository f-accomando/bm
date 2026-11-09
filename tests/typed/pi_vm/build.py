#!/usr/bin/env python3
"""Builds main.lua of the Pi cartridge that times blocked of Yharnam three ways: the Lua of tests/typed/yblocked.bmt, the
general C of the kernel (collmap) and the bmvm machine (vm_run). Run:  python3 tests/typed/pi_vm/build.py OUT_DIR
then make the .bm:  python3 scripts/mkbm.py -o OUT_DIR/pi_vm.bm --lua OUT_DIR/main.lua --info tests/typed/pi_vm/bm.info"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "bmt.py"), os.path.join(ROOT, "tests", "typed", "yblocked.bmt"),
                "--lua", os.path.join(out, "yblocked.lua")], check=True)
subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "bmtvm.py"), os.path.join(ROOT, "tests", "typed", "yblocked.bmt"),
                "--out", os.path.join(out, "yblocked.bmv")], check=True)
subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "bmtvm.py"), os.path.join(ROOT, "tests", "typed", "vmnoop.bmt"),
                "--out", os.path.join(out, "vmnoop.bmv")], check=True)
lua = open(os.path.join(out, "yblocked.lua"), encoding="utf-8").read()
blob = open(os.path.join(out, "yblocked.bmv"), "rb").read()
lit = '"' + "".join("\\%03d" % b for b in blob) + '"'
noop = open(os.path.join(out, "vmnoop.bmv"), "rb").read()
lit2 = '"' + "".join("\\%03d" % b for b in noop) + '"'
body = open(os.path.join(HERE, "bench.lua"), encoding="utf-8").read()
open(os.path.join(out, "main.lua"), "w", encoding="utf-8").write(
    "local floor, min, max, sqrt, abs = math.floor, math.min, math.max, math.sqrt, math.abs\n" + lua + "\nlocal BLOB = " + lit + "\nlocal BLOB_NOOP = " + lit2 + "\n" + body)
print("main.lua: %d bytes of Lua, a blob of %d bytes" % (len(lua), len(blob)))
