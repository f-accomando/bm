#!/usr/bin/env python3
"""
bmmesh.py - the MESH section of a .bm cartridge (3D models), read and
written, and the glTF binary files (.glb) that bm Studio (sdk/studio)
exports. Standard library only; used by mkbm.py --models and the tests.

A model is a dict:
  { "name": "house",
    "verts": [(x, y, z), ...],                       y up, as for mesh()
    "faces": [(a, b, c, colour, (u0, v0, u1, v1, u2, v2)), ...] }
a, b, c are 0-based and clockwise seen from the side that shows; colour is
0xRRGGBB, or TEXTURED (the face shows the sprite sheet at those corners,
in sheet pixels). Layout of the section: see src/bm/bm.h.

  bmmesh.py FILE.glb|FILE.bm       lists the models it holds
"""
import json
import math
import struct
import zlib
import sys

SEC_MESH, SEC_ANIM = 8, 9            # src/bm/bm.h; the first bm Studio files had 6 and 7
SEC_AUDIO, SEC_OLD_ANIM = 6, 7
TEXTURED = 0x80000000
NAME_LEN = 16
MAX_VERTS, MAX_FACES, MAX_MODELS = 4096, 16384, 256


def encode(models, inset=0.25):
    """models -> MESH section body; inset in sheet pixels (see bm.h)"""
    if not 1 <= len(models) <= MAX_MODELS:
        raise ValueError(f"1 to {MAX_MODELS} models, not {len(models)}")
    out = bytearray(struct.pack("<HHI", len(models), max(0, min(65535, round(inset * 256))), 0))
    seen = set()
    for m in models:
        name = m["name"].encode()[:NAME_LEN - 1]
        if not name or name in seen:
            raise ValueError(f"model names must be unique and not empty: {m['name']!r}")
        seen.add(name)
        v, f = m["verts"], m["faces"]
        if not 1 <= len(v) <= MAX_VERTS:
            raise ValueError(f"{m['name']}: 1 to {MAX_VERTS} vertices, not {len(v)}")
        if not 1 <= len(f) <= MAX_FACES:
            raise ValueError(f"{m['name']}: 1 to {MAX_FACES} faces, not {len(f)}")
        out += name.ljust(NAME_LEN, b"\0") + struct.pack("<HHI", len(v), len(f), 0)
        for p in v:
            out += struct.pack("<3f", *p)
        for a, b, c, colour, uv in f:
            if max(a, b, c) >= len(v) or min(a, b, c) < 0:
                raise ValueError(f"{m['name']}: vertex index out of range")
            q = [max(0, min(65535, round(t * 8))) for t in (uv or (0,) * 6)]
            out += struct.pack("<4HI6H", a, b, c, 0, colour & 0xFFFFFFFF, *q)
    return bytes(out)


def encode_faces(name, faces, bones=None):
    """a model as bm Studio keeps it (faces with corners p, colour c and the
    bone b of each corner, 0-based; from tools/img2mesh.py or the kernel's
    ai.mesh) -> the dict encode() takes, plus the bone of each vertex.
    Corners of different bones stay apart (bm3d.lua encode_mesh)."""
    verts, index, vb, tris = [], {}, [], []
    for f in faces:
        ids = []
        for k, p in enumerate(f["p"]):
            b = f.get("b", [0] * len(f["p"]))[k] if bones else 0
            key = (round(p[0], 5), round(p[1], 5), round(p[2], 5), b)
            i = index.get(key)
            if i is None:
                i = len(verts)
                index[key] = i
                verts.append((p[0], p[1], p[2]))
                vb.append(b)
            ids.append(i)
        colour = f["c"] & 0xFFFFFF
        for a, b_, c in ([(0, 1, 2), (0, 2, 3)] if len(ids) == 4 else [(0, 1, 2)]):
            if len({ids[a], ids[b_], ids[c]}) == 3:
                tris.append((ids[a], ids[b_], ids[c], colour, None))
    return {"name": name, "verts": verts, "faces": tris}, vb


def encode_anim(rigs):
    """[(name, bones, vb, clips)] -> ANIM section body (bm3d.lua encode_anim):
    bones {name, parent (0-based, -1 none), head, tail}, vb the bone of
    each vertex of the model's MESH entry, clips {name, loop, length, keys:
    [{t, pose: [{q, t}] }]}"""
    out = bytearray(struct.pack("<HHI", len(rigs), 0, 0))
    for name, bones, vb, clips in rigs:
        nb = len(bones)
        out += name.encode()[:NAME_LEN - 1].ljust(NAME_LEN, b"\0") + struct.pack("<HHHH", nb, len(clips), len(vb), 0)
        for b in bones:
            out += b["name"].encode()[:NAME_LEN - 1].ljust(NAME_LEN, b"\0")
            out += struct.pack("<hH6f", b["parent"], 0, *b["head"], *b["tail"])
        out += bytes(vb) + b"\0" * ((-len(vb)) % 4)
        for c in clips:
            out += c["name"].encode()[:NAME_LEN - 1].ljust(NAME_LEN, b"\0")
            out += struct.pack("<HBBf", len(c["keys"]), c.get("mode", 1), 1 if c.get("loop", True) else 0, c["length"])
            for key in c["keys"]:
                out += struct.pack("<f", key["t"])
                for i in range(nb):
                    p = key["pose"][i] if i < len(key["pose"]) else {"q": (0, 0, 0, 1), "t": (0, 0, 0)}
                    out += struct.pack("<7f", *p["q"], *p["t"])
    return bytes(out)


def decode_anim(body):
    """ANIM section body -> [(name, bones, vb, clips)] as encode_anim takes them"""
    n = struct.unpack_from("<H", body, 0)[0]
    off, out = 8, []
    for _ in range(n):
        name = body[off:off + NAME_LEN].split(b"\0")[0].decode(errors="replace")
        nb, nc, nv, _ = struct.unpack_from("<HHHH", body, off + NAME_LEN)
        off += NAME_LEN + 8
        bones = []
        for _ in range(nb):
            bname = body[off:off + NAME_LEN].split(b"\0")[0].decode(errors="replace")
            parent, _, hx, hy, hz, tx, ty, tz = struct.unpack_from("<hH6f", body, off + NAME_LEN)
            bones.append({"name": bname, "parent": parent, "head": (hx, hy, hz), "tail": (tx, ty, tz)})
            off += NAME_LEN + 28
        vb = list(body[off:off + nv])
        off += (nv + 3) & ~3
        clips = []
        for _ in range(nc):
            cname = body[off:off + NAME_LEN].split(b"\0")[0].decode(errors="replace")
            nk, mode, flags, length = struct.unpack_from("<HBBf", body, off + NAME_LEN)
            off += NAME_LEN + 8
            keys = []
            for _ in range(nk):
                t = struct.unpack_from("<f", body, off)[0]
                off += 4
                pose = []
                for _ in range(nb):
                    q = struct.unpack_from("<7f", body, off)
                    pose.append({"q": q[:4], "t": q[4:]})
                    off += 28
                keys.append({"t": t, "pose": pose})
            clips.append({"name": cname, "mode": mode, "loop": bool(flags & 1), "length": length, "keys": keys})
        out.append((name, bones, vb, clips))
    return out


def decode(body):
    """MESH section body -> (models, inset)"""
    count, inset, _ = struct.unpack_from("<HHI", body, 0)
    off, models = 8, []
    for _ in range(count):
        name = body[off:off + NAME_LEN].split(b"\0")[0].decode(errors="replace")
        nv, nf, _ = struct.unpack_from("<HHI", body, off + NAME_LEN)
        off += NAME_LEN + 8
        verts = [struct.unpack_from("<3f", body, off + i * 12) for i in range(nv)]
        off += nv * 12
        faces = []
        for i in range(nf):
            a, b, c, _, colour, *uv = struct.unpack_from("<4HI6H", body, off + i * 24)
            faces.append((a, b, c, colour, tuple(t / 8 for t in uv)))
        off += nf * 24
        models.append({"name": name, "verts": verts, "faces": faces})
    return models, inset / 256


def cart_sections(data):
    """a .bm file -> [(type, body)] in file order; the MESH and ANIM of the
    first bm Studio files (6 that is not a sound bank, 7) come as 8 and 9"""
    if data[:8] not in (b"BMCART\0\0", b"BM33CART"):
        raise ValueError("not a .bm cartridge")
    out = []
    for i in range(data[17]):
        typ, off, size, _ = struct.unpack_from("<IIII", data, 128 + i * 16)
        body = data[off:off + size]
        if typ == SEC_AUDIO and body[:4] != b"BMAU":
            typ = SEC_MESH
        elif typ == SEC_OLD_ANIM:
            typ = SEC_ANIM
        out.append((typ, body))
    return out


def rewrite_cart(data, secs):
    """the .bm file `data` with its sections replaced by `secs` ({type:
    body}; a section not in it goes away, a new one goes last): the header
    stays (title, author, resolution), the CRC is new"""
    order = [t for t, _ in cart_sections(data)]
    order += [t for t in secs if t not in order]
    sections = [(t, secs[t]) for t in order if t in secs]
    table, bodies = b"", b""
    offset = 128 + 16 * len(sections)
    for typ, body in sections:
        table += struct.pack("<IIII", typ, offset + len(bodies), len(body), 0)
        bodies += body + b"\0" * ((-len(body)) % 4)
    after = table + bodies
    header = bytearray(data[:128])
    header[17] = len(sections)
    struct.pack_into("<I", header, 20, zlib.crc32(after) & 0xFFFFFFFF)
    return bytes(header) + after


# ------------------------------------------------------------------ glTF

def srgb_to_linear(c):
    c /= 255
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def linear_to_srgb(v):
    v = max(0.0, min(1.0, v))
    s = v * 12.92 if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055
    return max(0, min(255, round(s * 255)))


def _mat_mul(a, b):
    return [sum(a[r + k * 4] * b[k + c * 4] for k in range(4)) for c in range(4) for r in range(4)]


def _node_matrix(n):
    if "matrix" in n:
        return list(n["matrix"])                    # column major
    tx, ty, tz = n.get("translation", (0, 0, 0))
    qx, qy, qz, qw = n.get("rotation", (0, 0, 0, 1))
    sx, sy, sz = n.get("scale", (1, 1, 1))
    r = [1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy + qz * qw), 2 * (qx * qz - qy * qw),
         2 * (qx * qy - qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz + qx * qw),
         2 * (qx * qz + qy * qw), 2 * (qy * qz - qx * qw), 1 - 2 * (qx * qx + qy * qy)]
    return [r[0] * sx, r[1] * sx, r[2] * sx, 0, r[3] * sy, r[4] * sy, r[5] * sy, 0,
            r[6] * sz, r[7] * sz, r[8] * sz, 0, tx, ty, tz, 1]


def _png_size(data):
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        return struct.unpack(">II", data[16:24])
    return None


def read_glb(data):
    """-> (json, bin chunk)"""
    magic, version, length = struct.unpack_from("<4sII", data, 0)
    if magic != b"glTF" or version != 2:
        raise ValueError("not a glTF 2 binary file (.glb)")
    pos, js, bin_ = 12, None, b""
    while pos < min(length, len(data)):
        n, typ = struct.unpack_from("<I4s", data, pos)
        body = data[pos + 8:pos + 8 + n]
        if typ == b"JSON":
            js = json.loads(body.decode())
        elif typ == b"BIN\0":
            bin_ = body
        pos += 8 + n
    if js is None:
        raise ValueError(".glb without JSON")
    return js, bin_


def glb_image(data):
    """the PNG of the first image of a .glb (bm Studio: the sprite sheet), or None"""
    js, bin_ = read_glb(data)
    for img in js.get("images", []):
        if "bufferView" in img:
            bv = js["bufferViews"][img["bufferView"]]
            png = bin_[bv.get("byteOffset", 0):bv.get("byteOffset", 0) + bv["byteLength"]]
            if png[:8] == b"\x89PNG\r\n\x1a\n":
                return png
    return None


def _accessor(js, bin_, i):
    a = js["accessors"][i]
    comps = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
    fmt, size = {5126: ("f", 4), 5125: ("I", 4), 5123: ("H", 2), 5121: ("B", 1),
                 5122: ("h", 2), 5120: ("b", 1)}[a["componentType"]]
    count = a["count"]
    if "bufferView" not in a:
        return [(0,) * comps] * count
    bv = js["bufferViews"][a["bufferView"]]
    base = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = bv.get("byteStride") or comps * size
    out = [struct.unpack_from("<" + fmt * comps, bin_, base + i * stride) for i in range(count)]
    if a.get("normalized"):
        scale = {5121: 255, 5123: 65535, 5120: 127, 5122: 32767}[a["componentType"]]
        out = [tuple(v / scale for v in t) for t in out]
    return out


def glb_models(data):
    """a .glb exported by bm Studio (or any glTF with plain colours) ->
    (models, sheet size or None, inset or None). Textured faces must come
    from bm Studio: their texture is the cartridge's sprite sheet."""
    js, bin_ = read_glb(data)
    bm = (js.get("asset", {}).get("extras") or {}).get("bm") or {}
    sheet = None
    if bm.get("sheet_w"):
        sheet = (bm["sheet_w"], bm["sheet_h"])
    elif js.get("images"):
        img = js["images"][0]
        if "bufferView" in img:
            bv = js["bufferViews"][img["bufferView"]]
            sheet = _png_size(bin_[bv.get("byteOffset", 0):bv.get("byteOffset", 0) + bv["byteLength"]])
    models, names = [], set()

    def visit(ni, parent):
        node = js["nodes"][ni]
        m = _mat_mul(parent, _node_matrix(node))
        if "mesh" in node:
            mesh = js["meshes"][node["mesh"]]
            name = (node.get("name") or mesh.get("name") or f"model{len(models) + 1}")[:NAME_LEN - 1]
            base, k = name, 2
            while name in names:
                name = f"{base[:NAME_LEN - 3]}{k}"
                k += 1
            names.add(name)
            models.append(_mesh_model(js, bin_, mesh, m, name, sheet))
        for c in node.get("children", []):
            visit(c, m)

    ident = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    scenes = js.get("scenes") or [{"nodes": list(range(len(js.get("nodes", []))))}]
    for ni in scenes[js.get("scene", 0)].get("nodes", []):
        visit(ni, ident)
    return [m for m in models if m["faces"]], sheet, bm.get("uv_inset")


def _mesh_model(js, bin_, mesh, mat, name, sheet):
    verts, index, faces = [], {}, []
    det = (mat[0] * (mat[5] * mat[10] - mat[9] * mat[6]) - mat[4] * (mat[1] * mat[10] - mat[9] * mat[2]) +
           mat[8] * (mat[1] * mat[6] - mat[5] * mat[2]))

    def vid(p):
        x, y, z = p
        w = (mat[0] * x + mat[4] * y + mat[8] * z + mat[12], mat[1] * x + mat[5] * y + mat[9] * z + mat[13],
             mat[2] * x + mat[6] * y + mat[10] * z + mat[14])
        key = (round(w[0], 5), round(w[1], 5), round(-w[2], 5))   # glTF is right handed: z flips
        if key not in index:
            index[key] = len(verts)
            verts.append(key)
        return index[key]

    for prim in mesh.get("primitives", []):
        if prim.get("mode", 4) != 4:
            continue
        attr = prim["attributes"]
        pos = _accessor(js, bin_, attr["POSITION"])
        idx = [t[0] for t in _accessor(js, bin_, prim["indices"])] if "indices" in prim else list(range(len(pos)))
        mat_ = js["materials"][prim["material"]] if "material" in prim and js.get("materials") else {}
        pbr = mat_.get("pbrMetallicRoughness", {})
        factor = pbr.get("baseColorFactor", [1, 1, 1, 1])
        textured = "baseColorTexture" in pbr and "TEXCOORD_0" in attr
        if textured and not sheet:
            raise ValueError(f"{name}: textured faces need a .glb from bm Studio (texture = sprite sheet)")
        uvs = _accessor(js, bin_, attr["TEXCOORD_0"]) if textured else None
        cols = _accessor(js, bin_, attr["COLOR_0"]) if "COLOR_0" in attr else None
        for t in range(0, len(idx) - 2, 3):
            tri = idx[t:t + 3]
            if det > 0:                 # z flips: the winding turns (unless the node mirrors too)
                tri = [tri[0], tri[2], tri[1]]
            a, b, c = (vid(pos[i]) for i in tri)
            if a == b or b == c or a == c:
                continue
            if textured:
                uv = []
                for i in tri:
                    uv += [uvs[i][0] * sheet[0], uvs[i][1] * sheet[1]]
                faces.append((a, b, c, TEXTURED, tuple(uv)))
            else:
                rgb = [factor[k] for k in range(3)]
                if cols:
                    rgb = [rgb[k] * sum(cols[i][k] for i in tri) / 3 for k in range(3)]
                colour = linear_to_srgb(rgb[0]) << 16 | linear_to_srgb(rgb[1]) << 8 | linear_to_srgb(rgb[2])
                faces.append((a, b, c, colour, (0,) * 6))
    return {"name": name, "verts": verts, "faces": faces}


def models_from_file(path):
    """(models, inset or None) from a .glb or from the MESH section of a .bm"""
    data = open(path, "rb").read()
    if data[:4] == b"glTF":
        models, _, inset = glb_models(data)
        return models, inset
    for typ, body in cart_sections(data):
        if typ == SEC_MESH:
            return decode(body)
    raise ValueError(f"{path}: no 3D models in it")


def main():
    for path in sys.argv[1:]:
        models, inset = models_from_file(path)
        print(f"{path}: {len(models)} models" + (f", texture inset {inset:g} px" if inset is not None else ""))
        for m in models:
            tex = sum(1 for f in m["faces"] if f[3] & TEXTURED)
            print(f"  {m['name']:16} {len(m['verts']):5} vertices {len(m['faces']):5} triangles ({tex} textured)")


if __name__ == "__main__":
    sys.exit(main())
