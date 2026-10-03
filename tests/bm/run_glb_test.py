#!/usr/bin/env python3
"""
The .glb reader of the console (src/bm/glb.c, through build/host/test_glb)
on the test .glb of tests/ai/glbfix.py: the box textured (red left, blue
right on the sheet, the texture corners on the faces), the pyramid's
vertex colours, every face showing from outside, a mirrored node, the
JPEG texture (with Pillow), the reducer above --faces, the flat twin with
the colours of the texture under each face.

  run_glb_test.py build/host/test_glb build/glb
"""
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
sys.path.insert(0, os.path.join(HERE, "..", "ai"))
import bmmesh  # noqa: E402
import glbfix  # noqa: E402

tool, out_dir = sys.argv[1], sys.argv[2]
os.makedirs(out_dir, exist_ok=True)


def convert(name, data, *args):
    path = os.path.join(out_dir, name + ".glb")
    open(path, "wb").write(data)
    prefix = os.path.join(out_dir, name)
    r = subprocess.run([tool, path, "--out", prefix] + list(args), capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    rec = open(prefix + ".rec", "rb").read()
    model = bmmesh.decode(struct.pack("<HHI", 1, 0, 0) + rec)[0][0]
    flat = bmmesh.decode(struct.pack("<HHI", 1, 0, 0) + open(prefix + ".flat", "rb").read())[0][0] \
        if os.path.exists(prefix + ".flat") else None
    rgba = open(prefix + ".rgba", "rb").read() if os.path.exists(prefix + ".rgba") else None
    return model, flat, rgba, r.stdout.strip()


for texture in ("png", "jpeg"):
    if texture == "jpeg":
        try:
            import PIL  # noqa: F401
        except ImportError:
            print("glb: no Pillow, the JPEG texture is not tried")
            continue
    m, flat, rgba, info = convert("box_" + texture, glbfix.build(texture))
    tex_faces = [f for f in m["faces"] if f[3] == bmmesh.TEXTURED]
    flat_faces = [f for f in m["faces"] if f[3] != bmmesh.TEXTURED]
    assert len(tex_faces) == 12 and len(flat_faces) == 6, (info, len(tex_faces), len(flat_faces))
    assert len(m["verts"]) == 13, len(m["verts"])                   # 8 of the box + 5 of the pyramid
    ys = [v[1] for v in m["verts"]]
    assert abs(min(ys)) < 1e-4 and abs(max(ys) - 2) < 1e-4, (min(ys), max(ys))   # framed: feet at 0, 2 tall
    assert rgba is not None and len(rgba) == 256 * 256 * 4
    left = rgba[(128 * 256 + 10) * 4:(128 * 256 + 10) * 4 + 3]
    right = rgba[(128 * 256 + 250) * 4:(128 * 256 + 250) * 4 + 3]
    assert left[0] > 240 and left[2] < 16, ("the sheet: red on the left", left)        # JPEG: near enough
    assert right[2] > 240 and right[0] < 16, ("the sheet: blue on the right", right)
    # the texture corners: sheet pixels, on the box's sides u goes 0..256
    us = sorted({round(f[4][k]) for f in tex_faces for k in (0, 2, 4)})
    assert us == [0, 256], us
    # the pyramid's vertex colours (sRGB of 1,0,0 ...): the faces carry a colour
    cols = {f[3] for f in flat_faces}
    assert len(cols) >= 3 and all(c <= 0xFFFFFF for c in cols), cols
    # every face shows from outside its solid
    box_faces = [f for f in m["faces"] if f[3] == bmmesh.TEXTURED]
    assert glbfix.outward(m, box_faces, (0, 0.5, 0)) == 0, "the box's faces show from outside"
    assert glbfix.outward(m, flat_faces, (0, 1.6, 0)) == 0, "the pyramid's faces show from outside"
    # the flat twin: the colour under each textured face is red or blue
    assert flat is not None and len(flat["faces"]) == len(m["faces"])
    reds = sum(1 for f in flat["faces"] if f[3] >> 16 > 200 and f[3] & 0xFF < 50)
    blues = sum(1 for f in flat["faces"] if f[3] & 0xFF > 200 and f[3] >> 16 < 50)
    assert reds >= 4 and blues >= 4, (reds, blues)
    print(f"glb ({texture} texture): {info}, the sheet, the corners, the colours and the winding ok")

# a mirrored node: its faces still show from outside
m, _, _, info = convert("mirrored", glbfix.build("png", mirrored=True))
flat_faces = [f for f in m["faces"] if f[3] != bmmesh.TEXTURED]
assert glbfix.outward(m, flat_faces, (0, 1.6, 0)) == 0, "a mirrored node's faces show from outside"
print("glb: a mirrored node ok")

# the reducer above --faces
m, flat, _, info = convert("fewer", glbfix.build("png"), "--faces", "8")
assert 1 <= len(m["faces"]) <= 8, info
assert any(f[3] == bmmesh.TEXTURED for f in m["faces"]), "the texture stays through the reducer"
print(f"glb: --faces 8: {info}")

# not a glb
bad = os.path.join(out_dir, "bad.glb")
open(bad, "wb").write(b"not a glb at all")
r = subprocess.run([tool, bad], capture_output=True, text=True)
assert r.returncode != 0 and "not a .glb" in r.stdout, r.stdout
print("glb: a broken file is refused")
