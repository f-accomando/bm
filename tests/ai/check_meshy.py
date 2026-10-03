#!/usr/bin/env python3
"""tools/meshy2mesh.py offline (make test-img2mesh): a .glb made here (a
textured box and a coloured pyramid, uint16 and uint32 indices, a node
transform, a PNG texture) becomes a .bm with the model textured on its
sheet, then flat in an existing cartridge, then snapped to a grid.

  check_meshy.py BUILD_DIR
"""
import json
import os
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmmesh  # noqa: E402

build = sys.argv[1]
out_dir = os.path.join(build, "meshy")
os.makedirs(out_dir, exist_ok=True)


import glbfix  # noqa: E402

data = glbfix.build("png")
glb_path = os.path.join(out_dir, "test.glb")
open(glb_path, "wb").write(data)

tool = os.path.join(ROOT, "tools", "meshy2mesh.py")
cart = os.path.join(out_dir, "meshy.bm")
if os.path.exists(cart):
    os.remove(cart)
r = subprocess.run([sys.executable, tool, "--glb", glb_path, "-o", cart, "--name", "thing", "--height", "3"],
                   capture_output=True, text=True)
assert r.returncode == 0, r.stdout + r.stderr
d = open(cart, "rb").read()
secs = dict(bmmesh.cart_sections(d))
assert 2 in secs or 5 in secs, "a sheet with the texture"
models, _ = bmmesh.decode(secs[8])
assert [m["name"] for m in models] == ["thing"], models
m = models[0]
tex_faces = [f for f in m["faces"] if f[3] == bmmesh.TEXTURED]
flat_faces = [f for f in m["faces"] if f[3] != bmmesh.TEXTURED]
assert len(tex_faces) == 12 and len(flat_faces) == 6, (len(tex_faces), len(flat_faces))
ys = [v[1] for v in m["verts"]]
assert abs(min(ys)) < 1e-4 and abs(max(ys) - 3) < 1e-3, (min(ys), max(ys))     # on the ground, 3 tall
xs = [v[0] for v in m["verts"]]
assert abs(max(xs) + min(xs)) < 1e-3, "centred"
us = [u for f in tex_faces for u in f[4][0::2]]
assert 0 <= min(us) and max(us) <= 256, (min(us), max(us))
# the pyramid's colours come from its vertices (the apex white, the base coloured)
cols = {f[3] for f in flat_faces}
assert len(cols) >= 3, cols
# the winding: every face shows from outside its solid, i.e. the cross
# product of its corners points away from a point inside (the box's
# middle, the pyramid's; the model is 3 tall: the box is y 0..1.5, the
# pyramid 1.5..3)


def _unused_outward(model, faces, inside):
    v = model["verts"]
    bad = 0
    for a, b, c, *_ in faces:
        A, B, C = v[a], v[b], v[c]
        e1 = [B[k] - A[k] for k in range(3)]
        e2 = [C[k] - A[k] for k in range(3)]
        n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        mid = [(A[k] + B[k] + C[k]) / 3 for k in range(3)]
        if sum(n[k] * (mid[k] - inside[k]) for k in range(3)) <= 0:
            bad += 1
    return bad


assert glbfix.outward(m, tex_faces, (0, 0.75, 0)) == 0, "the box's faces show from outside"
assert glbfix.outward(m, flat_faces, (0, 2.0, 0)) == 0, "the pyramid's faces show from outside"
print("meshy2mesh: textured cartridge ok:", len(m["verts"]), "vertices", len(m["faces"]), "triangles")

# into an existing cartridge: flat colours, the sheet stays, the old model stays
r = subprocess.run([sys.executable, tool, "--glb", glb_path, "-o", cart, "--name", "flat1"], capture_output=True, text=True)
assert r.returncode == 0, r.stdout + r.stderr
d2 = open(cart, "rb").read()
secs2 = dict(bmmesh.cart_sections(d2))
models2, _ = bmmesh.decode(secs2[8])
assert [x["name"] for x in models2] == ["thing", "flat1"], models2
assert all(f[3] != bmmesh.TEXTURED for f in models2[1]["faces"]), "flat in an existing cartridge"
assert secs2.get(2) == secs.get(2) and secs2.get(5) == secs.get(5), "the sheet stays"
reds = sum(1 for f in models2[1]["faces"] if f[3] >> 16 > 200 and f[3] & 0xFF < 50)
blues = sum(1 for f in models2[1]["faces"] if f[3] & 0xFF > 200 and f[3] >> 16 < 50)
assert reds >= 4 and blues >= 4, (reds, blues)              # the texture sampled: red left, blue right
# above --max-tris: the reducer (src/bm/decimate.c), the texture kept
cart3 = os.path.join(out_dir, "reduced.bm")
if os.path.exists(cart3):
    os.remove(cart3)
r = subprocess.run([sys.executable, tool, "--glb", glb_path, "-o", cart3, "--name", "g", "--max-tris", "10"],
                   capture_output=True, text=True)
assert r.returncode == 0, r.stdout + r.stderr
models3, _ = bmmesh.decode(dict(bmmesh.cart_sections(open(cart3, "rb").read()))[8])
assert 1 <= len(models3[0]["faces"]) <= 10, len(models3[0]["faces"])
assert any(f[3] == bmmesh.TEXTURED for f in models3[0]["faces"]), "the texture stays through the reducer"
assert "reduced to" in r.stderr, r.stderr
# the grid (past the hard limits): tried on its own
sys.path.insert(0, os.path.dirname(tool))
import meshy2mesh  # noqa: E402
pts3 = [(x * 0.1, y * 0.1, 0.0) for y in range(5) for x in range(5)]
tris3 = [(y * 5 + x, y * 5 + x + 1, y * 5 + x + 5, (1, 2, 3), None, None) for y in range(4) for x in range(4)]
gp, gf = meshy2mesh.cluster(pts3, tris3, 2)
assert len(gp) <= 9 and 1 <= len(gf) <= len(tris3), (len(gp), len(gf))
print("meshy2mesh: flat into an existing cartridge, the reducer and the grid ok")

# a node that mirrors (scale -1 on x): glTF says its faces are already
# clockwise in front, so the conversion must not reverse them again
mirror_glb = glbfix.build("png", mirrored=True)
mirror_path = os.path.join(out_dir, "mirror.glb")
open(mirror_path, "wb").write(mirror_glb)
cart4 = os.path.join(out_dir, "mirror.bm")
if os.path.exists(cart4):
    os.remove(cart4)
r = subprocess.run([sys.executable, tool, "--glb", mirror_path, "-o", cart4, "--name", "mir", "--height", "1"],
                   capture_output=True, text=True)
assert r.returncode == 0, r.stdout + r.stderr
models4, _ = bmmesh.decode(dict(bmmesh.cart_sections(open(cart4, "rb").read()))[8])
pyr4 = [f for f in models4[0]["faces"] if f[3] != bmmesh.TEXTURED]
pv4 = {i for f in pyr4 for i in f[:3]}
inside4 = tuple(sum(models4[0]["verts"][i][k] for i in pv4) / len(pv4) for k in range(3))
assert glbfix.outward(models4[0], pyr4, inside4) == 0, "a mirrored node's faces show from outside"
print("meshy2mesh: a mirrored node ok")

# bmrender draws it: the texture's red and blue on the picture
sys.path.insert(0, os.path.join(ROOT, "tools"))
import bmrender  # noqa: E402
png_path = os.path.join(out_dir, "meshy.png")
names = bmrender.render(cart, png_path, size=120)
assert [n for n, _, _ in names] == ["thing", "flat1"], names
raw = zlib.decompress(b"".join(
    open(png_path, "rb").read()[i + 8:i + 8 + struct.unpack(">I", open(png_path, "rb").read()[i:i + 4])[0]]
    for i in [open(png_path, "rb").read().find(b"IDAT") - 4]))
reds = blues = 0
for i in range(0, len(raw), 3 * 5):
    r, g, b = raw[i], raw[i + 1], raw[i + 2]
    reds += r > 120 and g < 80 and b < 80
    blues += b > 120 and r < 80 and g < 80
assert reds > 50 and blues > 50, (reds, blues)
print("bmrender: the textured box drawn:", png_path)
