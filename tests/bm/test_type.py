#!/usr/bin/env python3
"""mkbm.py writes the `type:` key of bm.info into bits 2-3 of the u16 at 18 (D09-1b)."""
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MKBM = os.path.join(ROOT, "scripts", "mkbm.py")

ok = fail = 0


def check(cond, text):
    global ok, fail
    if cond:
        ok += 1
    else:
        fail += 1
        print("FAIL:", text)


def build(tmp, out, type_line, res):
    info = os.path.join(tmp, "bm.info")
    with open(info, "w") as f:
        f.write(f"name: T\nauthor: t\nresolution: {res}\n{type_line}")
    return subprocess.run([sys.executable, MKBM, "-o", os.path.join(tmp, out), "--lua", os.path.join(tmp, "main.lua"),
                           "--info", info], capture_output=True, text=True)


with tempfile.TemporaryDirectory() as tmp:
    with open(os.path.join(tmp, "main.lua"), "w") as f:
        f.write("function _draw() cls(0) end\n")
    cases = [("type: app\n", 1), ("type: utility\n", 2), ("type: game\n", 0), ("", 0)]
    # extension: (resolution, bits kept from the kind)
    for ext, res, kind in ((".bm", "640x360", 0), (".bme", "640x360", 1)):
        for line, t in cases:
            out = "t" + ext
            r = build(tmp, out, line, res)
            check(r.returncode == 0, f"{ext} {line.strip() or 'no type'}: mkbm exits 0 ({r.stderr.strip()})")
            if r.returncode:
                continue
            with open(os.path.join(tmp, out), "rb") as f:
                data = f.read()
            flags = struct.unpack_from("<H", data, 18)[0]
            check(flags == kind | (t << 2), f"{ext} {line.strip() or 'no type'}: flags {flags}, want {kind | (t << 2)}")
    build(tmp, "t.bm", "type: app\n", "640x360")
    with open(os.path.join(tmp, "t.bm"), "rb") as f:
        check(b"type: app" in f.read(), ".bm of type app carries the INFO line")
    r = build(tmp, "bad.bm", "type: banana\n", "640x360")
    check(r.returncode != 0 and "must be game, app or utility" in r.stdout + r.stderr, "type: banana is an error")

print(f"type: {ok}/{ok + fail} checks passed")
sys.exit(1 if fail else 0)
