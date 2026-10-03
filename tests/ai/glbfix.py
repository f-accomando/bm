#!/usr/bin/env python3
"""
glbfix: a small .glb made by hand for the tests of the converters
(tests/ai/check_meshy.py for tools/meshy2mesh.py, tests/bm/run_glb_test.py
for src/bm/glb.c): a textured box (red left, blue right; 16-bit indices)
and a pyramid with vertex colours (32-bit indices) moved and scaled by its
node; a mirrored-node variant; the texture as PNG, or JPEG through Pillow.

  build(texture="png" | "jpeg", mirrored=False) -> bytes
"""
import json
import struct
import zlib


def png(w, h, rgb):
    raw = b"".join(b"\0" + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))

    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


def jpeg(w, h, rgb):
    from PIL import Image
    import io
    im = Image.frombytes("RGB", (w, h), bytes(rgb))
    buf = io.BytesIO()
    im.save(buf, format="JPEG", quality=95, subsampling=0)
    return buf.getvalue()


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
TEX_RGB = [c for y in range(16) for x in range(16) for c in ((255, 0, 0) if x < 8 else (0, 0, 255))]


def build(texture="png", mirrored=False):
    blob = b""
    views, accessors = [], []

    def add(data, count, ctype, atype):
        nonlocal blob
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
    tex = (jpeg if texture == "jpeg" else png)(16, 16, TEX_RGB)
    img_off = len(blob)
    blob += tex
    views.append({"buffer": 0, "byteOffset": img_off, "byteLength": len(tex)})
    pyr_node = {"mesh": 1, "translation": [0, 0.5, 0], "scale": [-2, 2, 2] if mirrored else [2, 2, 2]}
    return glb(
        nodes=[{"mesh": 0}, pyr_node],
        meshes=[{"primitives": [{"attributes": {"POSITION": a_bpos, "TEXCOORD_0": a_buv}, "indices": a_bidx,
                                 "material": 0}]},
                {"primitives": [{"attributes": {"POSITION": a_ppos, "COLOR_0": a_pcol}, "indices": a_pidx}]}],
        accessors=accessors, views=views, blob=blob,
        images=[{"bufferView": len(views) - 1, "mimeType": "image/" + texture}],
        materials=[{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
        textures=[{"source": 0}])


def outward(model, faces, inside):
    """how many of the faces show from `inside` (clockwise there): 0 is right"""
    n = 0
    for a, b, c, *_ in faces:
        p, q, r = (model["verts"][i] for i in (a, b, c))
        e1 = [q[k] - p[k] for k in range(3)]
        e2 = [r[k] - p[k] for k in range(3)]
        nx = e1[1] * e2[2] - e1[2] * e2[1]
        ny = e1[2] * e2[0] - e1[0] * e2[2]
        nz = e1[0] * e2[1] - e1[1] * e2[0]
        d = [p[k] - inside[k] for k in range(3)]
        # seen from inside the solid, a face shows if its normal points inwards
        if nx * d[0] + ny * d[1] + nz * d[2] < 0:
            n += 1
    return n


if __name__ == "__main__":
    import sys
    open(sys.argv[1], "wb").write(build(sys.argv[2] if len(sys.argv) > 2 else "png"))
