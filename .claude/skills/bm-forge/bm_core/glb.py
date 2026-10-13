"""glTF binary (.glb) -> MESH models. A straight port of the glTF half of
scripts/bmmesh.py (read_glb, _accessor, _node_matrix, _mat_mul, glb_image,
glb_models, _mesh_model, linear_to_srgb), returning bm_core Model / Face.

Same conventions as the original: glTF is right handed, so z flips sign and
each triangle's winding turns (scripts/bmmesh.py:347,369-370; CLAUDE.md
invariant), vertices merged by position rounded to 1e-5, degenerate
triangles dropped, textured faces only from bm Studio .glb files (texture =
the cartridge sheet). Only triangle lists (mode 4). No skins/animations: a
.glb skeleton is not imported (bmmesh does not either).
"""
import json
import struct

from .errors import ValidationError
from .mesh import TEXTURED, Face, Model, Mesh

NAME_MAX = 15


def linear_to_srgb(v):
    """port of scripts/bmmesh.py:linear_to_srgb"""
    v = max(0.0, min(1.0, v))
    s = v * 12.92 if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055
    return max(0, min(255, round(s * 255)))


def _mat_mul(a, b):
    """port of scripts/bmmesh.py:_mat_mul (column major 4x4)"""
    return [sum(a[r + k * 4] * b[k + c * 4] for k in range(4)) for c in range(4) for r in range(4)]


def _node_matrix(n):
    """port of scripts/bmmesh.py:_node_matrix"""
    if "matrix" in n:
        return list(n["matrix"])
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
    """-> (json, bin chunk); port of scripts/bmmesh.py:read_glb, errors as ValidationError"""
    try:
        magic, version, length = struct.unpack_from("<4sII", data, 0)
        if magic != b"glTF" or version != 2:
            raise ValidationError("not a glTF 2 binary file (.glb)", "glb")
        pos, js, bin_ = 12, None, b""
        while pos < min(length, len(data)):
            n, typ = struct.unpack_from("<I4s", data, pos)
            body = data[pos + 8:pos + 8 + n]
            if typ == b"JSON":
                js = json.loads(body.decode())
            elif typ == b"BIN\0":
                bin_ = body
            pos += 8 + n
    except (struct.error, ValueError) as e:
        if isinstance(e, ValidationError):
            raise
        raise ValidationError(f"broken .glb: {e}", "glb")
    if js is None:
        raise ValidationError(".glb without JSON", "glb")
    return js, bin_


def _accessor(js, bin_, i):
    """port of scripts/bmmesh.py:_accessor"""
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


def glb_image(data):
    """the PNG of the first image of a .glb, or None (port of scripts/bmmesh.py:glb_image)"""
    js, bin_ = read_glb(data)
    for img in js.get("images", []):
        if "bufferView" in img:
            bv = js["bufferViews"][img["bufferView"]]
            png = bin_[bv.get("byteOffset", 0):bv.get("byteOffset", 0) + bv["byteLength"]]
            if png[:8] == b"\x89PNG\r\n\x1a\n":
                return png
    return None


def _mesh_model(js, bin_, mesh, mat, name, sheet):
    """port of scripts/bmmesh.py:_mesh_model -> Model"""
    verts, index, faces = [], {}, []
    det = (mat[0] * (mat[5] * mat[10] - mat[9] * mat[6]) - mat[4] * (mat[1] * mat[10] - mat[9] * mat[2]) +
           mat[8] * (mat[1] * mat[6] - mat[5] * mat[2]))

    def vid(p):
        x, y, z = p
        w = (mat[0] * x + mat[4] * y + mat[8] * z + mat[12], mat[1] * x + mat[5] * y + mat[9] * z + mat[13],
             mat[2] * x + mat[6] * y + mat[10] * z + mat[14])
        key = (round(w[0], 5), round(w[1], 5), round(-w[2], 5))     # z flips
        if key not in index:
            index[key] = len(verts)
            verts.append(list(key))
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
            raise ValidationError("textured faces need a .glb from bm Studio (texture = sprite sheet)", name)
        uvs = _accessor(js, bin_, attr["TEXCOORD_0"]) if textured else None
        cols = _accessor(js, bin_, attr["COLOR_0"]) if "COLOR_0" in attr else None
        for t in range(0, len(idx) - 2, 3):
            tri = idx[t:t + 3]
            if det > 0:
                tri = [tri[0], tri[2], tri[1]]
            a, b, c = (vid(pos[i]) for i in tri)
            if a == b or b == c or a == c:
                continue
            if textured:
                uv = []
                for i in tri:
                    uv += [uvs[i][0] * sheet[0], uvs[i][1] * sheet[1]]
                faces.append(Face.from_px(a, b, c, TEXTURED, uv))
            else:
                rgb = [factor[k] for k in range(3)]
                if cols:
                    rgb = [rgb[k] * sum(cols[i][k] for i in tri) / 3 for k in range(3)]
                colour = linear_to_srgb(rgb[0]) << 16 | linear_to_srgb(rgb[1]) << 8 | linear_to_srgb(rgb[2])
                faces.append(Face(a, b, c, colour))
    return Model(name, verts, faces)


def glb_models(data):
    """a .glb -> (Mesh, sheet size or None). Port of scripts/bmmesh.py:glb_models;
    the inset comes from asset.extras.bm.uv_inset (sheet px) when bm Studio wrote
    it, else 64 (0.25 px, scripts/bmmesh.py:30). Names cut to 15 characters and
    made unique like the original (NON VERIFICATO for multi-byte names: the
    original cuts characters, the MESH field counts bytes; a name over 15 bytes
    is refused later by mesh.validate)."""
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
            name = (node.get("name") or mesh.get("name") or f"model{len(models) + 1}")[:NAME_MAX]
            base, k = name, 2
            while name in names:
                name = f"{base[:NAME_MAX - 2]}{k}"
                k += 1
            names.add(name)
            models.append(_mesh_model(js, bin_, mesh, m, name, sheet))
        for c in node.get("children", []):
            visit(c, m)

    ident = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    try:
        scenes = js.get("scenes") or [{"nodes": list(range(len(js.get("nodes", []))))}]
        for ni in scenes[js.get("scene", 0)].get("nodes", []):
            visit(ni, ident)
    except (KeyError, IndexError, TypeError, struct.error) as e:
        raise ValidationError(f"broken .glb structure: {e!r}", "glb")
    inset = bm.get("uv_inset")
    inset = 64 if inset is None else max(0, min(65535, round(inset * 256)))
    return Mesh([m for m in models if m.faces], inset), sheet
