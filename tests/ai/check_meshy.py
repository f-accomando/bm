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


def png(w, h, rgb):
    raw = b"".join(b"\0" + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))

    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


def glb(nodes, meshes, accessors, views, blob, images=(), materials=(), textures=()):
    js = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": list(range(len(nodes)))}],
          "nodes": nodes, "meshes": meshes, "accessors": accessors, "bufferViews": views,
          "buffers": [{"byteLength": len(blob)}], "images": list(images), "materials": list(materials),
          "textures": list(textures)}
    j = json.dumps(js).encode()
    j += b" " * ((-len(j)) % 4)
    blob += b"\0" * ((-len(blob)) % 4)
    body = struct.pack("<II", len(j), 0x4E4F534A) + j + struct.pack("<II", len(blob), 0x004E4942) + blob
    return struct.pack("<4sII", b"glTF", 2, 12 + len(body)) + body


# a box 2 wide, 1 tall, 1 deep at y -0.5..0.5, textured: 24 vertices (4 per side)
box_pos, box_uv, box_idx = [], [], []
sides = [((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0)), ((1, 0, 0), (0, 0, -1), (0, 1, 0)),
         ((-1, 0, 0), (0, 0, 1), (0, 1, 0)), ((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1))]
half = (1.0, 0.5, 0.5)
for n, u, v in sides:
    base = len(box_pos)
    for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        box_pos.append(tuple(half[k] * (n[k] + su * u[k] + sv * v[k]) for k in range(3)))
        box_uv.append(((su + 1) / 2, (1 - sv) / 2))
    box_idx += [base, base + 1, base + 2, base, base + 2, base + 3]
# a pyramid with vertex colours, uint32 indices, moved by a node
pyr_pos = [(-0.5, 0, -0.5), (0.5, 0, -0.5), (0.5, 0, 0.5), (-0.5, 0, 0.5), (0, 1, 0)]
pyr_col = [(1, 0, 0), (0, 1, 0), (0, 0, 1), (1, 1, 0), (1, 1, 1)]
pyr_idx = [0, 4, 1, 1, 4, 2, 2, 4, 3, 3, 4, 0, 0, 1, 2, 0, 2, 3]       # counter-clockwise from outside
tex = png(16, 16, [c for y in range(16) for x in range(16) for c in ((255, 0, 0) if x < 8 else (0, 0, 255))])

blob = b""
views, accessors = [], []


def add(data, count, ctype, atype, target=None):
    global blob
    off = len(blob)
    blob += data + b"\0" * ((-len(data)) % 4)
    views.append({"buffer": 0, "byteOffset": off, "byteLength": len(data)})
    accessors.append({"bufferView": len(views) - 1, "componentType": ctype, "count": count, "type": atype})
    return len(accessors) - 1


a_bpos = add(b"".join(struct.pack("<3f", *p) for p in box_pos), 24, 5126, "VEC3")
a_buv = add(b"".join(struct.pack("<2f", *p) for p in box_uv), 24, 5126, "VEC2")
a_bidx = add(b"".join(struct.pack("<H", i) for i in box_idx), 36, 5123, "SCALAR")
a_ppos = add(b"".join(struct.pack("<3f", *p) for p in pyr_pos), 5, 5126, "VEC3")
a_pcol = add(b"".join(struct.pack("<3f", *p) for p in pyr_col), 5, 5126, "VEC3")
a_pidx = add(b"".join(struct.pack("<I", i) for i in pyr_idx), 18, 5125, "SCALAR")
accessors[a_bpos]["min"] = [-1, -0.5, -0.5]
accessors[a_bpos]["max"] = [1, 0.5, 0.5]
accessors[a_ppos]["min"] = [-0.5, 0, -0.5]
accessors[a_ppos]["max"] = [0.5, 1, 0.5]
img_off = len(blob)
blob += tex
views.append({"buffer": 0, "byteOffset": img_off, "byteLength": len(tex)})
data = glb(
    nodes=[{"mesh": 0}, {"mesh": 1, "translation": [0, 0.5, 0], "scale": [2, 2, 2]}],
    meshes=[{"primitives": [{"attributes": {"POSITION": a_bpos, "TEXCOORD_0": a_buv}, "indices": a_bidx, "material": 0}]},
            {"primitives": [{"attributes": {"POSITION": a_ppos, "COLOR_0": a_pcol}, "indices": a_pidx}]}],
    accessors=accessors, views=views, blob=blob,
    images=[{"bufferView": len(views) - 1, "mimeType": "image/png"}],
    materials=[{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
    textures=[{"source": 0}])
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


def outward(model, faces, inside):
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


assert outward(m, tex_faces, (0, 0.75, 0)) == 0, "the box's faces show from outside"
assert outward(m, flat_faces, (0, 2.0, 0)) == 0, "the pyramid's faces show from outside"
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
mirror_glb = glb(
    nodes=[{"mesh": 0, "scale": [-1, 1, 1]}],
    meshes=[{"primitives": [{"attributes": {"POSITION": a_ppos, "COLOR_0": a_pcol}, "indices": a_pidx}]}],
    accessors=accessors, views=views, blob=blob)
mirror_path = os.path.join(out_dir, "mirror.glb")
open(mirror_path, "wb").write(mirror_glb)
cart4 = os.path.join(out_dir, "mirror.bm")
if os.path.exists(cart4):
    os.remove(cart4)
r = subprocess.run([sys.executable, tool, "--glb", mirror_path, "-o", cart4, "--name", "mir", "--height", "1"],
                   capture_output=True, text=True)
assert r.returncode == 0, r.stdout + r.stderr
models4, _ = bmmesh.decode(dict(bmmesh.cart_sections(open(cart4, "rb").read()))[8])
assert outward(models4[0], models4[0]["faces"], (0, 0.3, 0)) == 0, "a mirrored node's faces show from outside"
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
