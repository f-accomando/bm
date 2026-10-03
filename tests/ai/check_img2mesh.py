#!/usr/bin/env python3
"""The .bm written by tools/img2mesh.py offline (make test-img2mesh): two
models from the mech script, each with its skeleton and three animations,
the viewer's Lua, a sound header and CRC the kernel accepts."""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import bmmesh  # noqa: E402
import mkbm  # noqa: E402

path = sys.argv[1]
data = open(path, "rb").read()
assert data[:8] == b"BMCART\0\0", "not a cartridge"
n = data[17]
after = data[128:]
assert struct.unpack_from("<I", data, 20)[0] == mkbm.crc32(after), "CRC"
secs = dict(bmmesh.cart_sections(data))
assert 1 in secs and b"_draw" in secs[1], "the viewer's Lua"
models, inset = bmmesh.decode(secs[bmmesh.SEC_MESH])
names = [m["name"] for m in models]
assert names == ["mech", "mech2"], names
for m in models:
    assert len(m["verts"]) == 702 and len(m["faces"]) == 1160, (m["name"], len(m["verts"]), len(m["faces"]))
rigs = bmmesh.decode_anim(secs[bmmesh.SEC_ANIM])
assert [(r[0], len(r[1]), len(r[2]), len(r[3])) for r in rigs] == [("mech", 9, 702, 3), ("mech2", 9, 702, 3)], rigs
clips = [c["name"] for c in rigs[0][3]]
assert clips == ["idle", "walk", "fire"], clips
assert rigs[0][1][1]["name"] == "thigh.L" and rigs[0][1][1]["parent"] == 0, rigs[0][1][1]
assert max(rigs[0][2]) == 8, "every bone has vertices"
print(f"img2mesh: {path}: {n} sections, {names}, rigs and animations as expected")
