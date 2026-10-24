"""Tiny mesh builders for tests and samples: axis-aligned boxes.

The corner order and the 12 triangles are copied from the villager of
tests/fixtures/village/models.bm (MESH model "villager", its first box: vertices 0..7,
faces 0..11, read with bm_core.mesh.decode), so the winding is the real
tool's: clockwise seen from outside (src/bm/bm.h:46-47, the r3d.c invariant).
"""
import json

from . import mesh as meshmod
from .errors import need

# corners: (x0,y0,z0) (x0,y1,z0) (x1,y1,z0) (x1,y0,z0) (x1,y0,z1) (x1,y1,z1) (x0,y1,z1) (x0,y0,z1)
_CORNERS = ((0, 0, 0), (0, 1, 0), (1, 1, 0), (1, 0, 0), (1, 0, 1), (1, 1, 1), (0, 1, 1), (0, 0, 1))
_TRIS = ((0, 1, 2), (0, 2, 3), (4, 5, 6), (4, 6, 7), (7, 6, 1), (7, 1, 0),
         (3, 2, 5), (3, 5, 4), (1, 6, 5), (1, 5, 2), (7, 0, 3), (7, 3, 4))


def box(lo, hi, colour=0xFFFFFF, base=0):
    """(8 vertices, 12 Faces) of the box lo..hi; face indices start at `base`"""
    need(all(hi[k] > lo[k] for k in range(3)), f"box {lo}..{hi}: hi must be above lo on every axis", "box")
    vs = [[(lo, hi)[c[k]][k] for k in range(3)] for c in _CORNERS]
    fs = [meshmod.Face(base + a, base + b, base + c, colour) for a, b, c in _TRIS]
    return vs, fs


def boxes_model(name, boxes):
    """a Model from boxes [{"lo": [x,y,z], "hi": [x,y,z], "colour": 0xRRGGBB or "#RRGGBB",
    "bone": name (optional, ignored here)}] -> (Model, [bone name or None per vertex])"""
    verts, faces, owner = [], [], []
    for b in boxes:
        col = b.get("colour", 0xFFFFFF)
        if isinstance(col, str):
            col = int(col.lstrip("#"), 16)
        vs, fs = box(b["lo"], b["hi"], col, len(verts))
        verts += vs
        faces += fs
        owner += [b.get("bone")] * len(vs)
    m = meshmod.Model(name, verts, faces, 0)
    meshmod.validate_model(m)
    return m, owner


def load_boxes(path_or_dict):
    """{"model": name, "boxes": [...]} (dict or JSON file) -> (Model, owners)"""
    d = path_or_dict
    if not isinstance(d, dict):
        with open(d, encoding="utf-8") as f:
            d = json.load(f)
    return boxes_model(d["model"], d["boxes"])
