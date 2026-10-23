#!/usr/bin/env python3
"""
bm_res_build (src/bm/format.c, through build/host/test_resbuild), the file the console's res_write
makes in /bmm: the bytes are the ones scripts/bmres.py dump makes (header, CRC-32, section table, bodies
padded to 4), with MESH only, with MESH + ANIM + a sheet + INFO, with a name over 47 bytes (cut on a
character), and a section the kernel refuses (a LUA section in a model file).

  run_resbuild_test.py build/host/test_resbuild build/resbuild
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import bmmesh  # noqa: E402
import bmres  # noqa: E402

tool, out_dir = sys.argv[1], sys.argv[2]
os.makedirs(out_dir, exist_ok=True)


def sections():
    model = {"name": "tri", "verts": [(0, 0, 0), (1, 0, 0), (0, 1, 0)], "faces": [(0, 1, 2, 0x336699, None)]}
    mesh = bmmesh.encode([model])
    bones = [{"name": "root", "parent": -1, "head": (0, 0, 0), "tail": (0, 1, 0)}]
    clip = {"name": "idle", "length": 1.0, "keys": [{"t": 0.0, "pose": [{"q": (0, 0, 0, 1), "t": (0, 0, 0)}]}]}
    anim = bmmesh.encode_anim([("tri", bones, [0, 0, 0], [clip])])
    sheet = (8).to_bytes(2, "little") * 2 + bytes([200, 100, 50, 255]) * 64
    info = bmres.info_dump({"file": [("name", "tri")], "items": []})
    return mesh, anim, sheet, info


def build(name, author, secs, tag):
    files = []
    for typ, body in secs:
        p = os.path.join(out_dir, f"{tag}.{typ}")
        open(p, "wb").write(body)
        files.append(f"{typ}:{p}")
    out = os.path.join(out_dir, tag + ".bmm")
    r = subprocess.run([tool, out, "1", name, author] + files, capture_output=True, text=True)
    return r, out


mesh, anim, sheet, info = sections()
cases = [
    ("mesh", "tri", "", [(8, mesh)]),
    ("full", "tri", "me", [(8, mesh), (9, anim), (2, sheet), (10, info)]),
    ("long", "é" * 40, "x" * 40, [(8, mesh), (2, sheet)]),
]
for tag, name, author, secs in cases:
    r, out = build(name, author, secs, tag)
    assert r.returncode == 0, (tag, r.stdout, r.stderr)
    want = bmres.dump(bmres.File(bmres.MODEL, name, author, [[t, b] for t, b in secs]))
    got = open(out, "rb").read()
    assert got == want, (tag, "bytes differ from bmres.dump", len(got), len(want))
    back = bmres.load(got, tag)
    assert back.kind == bmres.MODEL and [t for t, _ in back.sections] == [t for t, _ in secs], tag
long_name = bmres.load(open(os.path.join(out_dir, "long.bmm"), "rb").read()).name
assert len(long_name.encode()) <= 47 and long_name == "é" * 23, long_name      # 46 bytes, no half character

# the kernel refuses what a model file does not hold
r, _ = build("bad", "", [(8, mesh), (1, b"print(1)")], "bad")
assert r.returncode != 0 and "never holds code" in r.stderr, (r.stdout, r.stderr)
print("resbuild: ok (%d files, same bytes as bmres.py)" % len(cases))
