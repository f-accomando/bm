"""Skeletons from a simple description, and rigid vertex -> bone binding.

The format has no bind matrices and no weights: a bone is a name, a parent
(-1 or an EARLIER index), and a rest segment head -> tail (44 bytes,
src/bm/bm.h:61-76, src/bm/format.c:255-263). Skinning is rigid, one bone
index (u8) per vertex (src/bm/format.c:266-269, src/bm/runtime.c:2319-2320).
So "bind pose" here = the head/tail points, and "skin weights" = one bone per
vertex (assign_vertices). Output: anim.Rig with no clips (animgen adds them).

Description (dict or JSON file), every key checked (unknown keys refused):

    {"model": "hero",                       # the MESH model the rig moves (1..15 bytes)
     "bones": [
       {"name": "hips", "parent": null, "head": [0, 0.9, 0], "tail": [0, 1, 0]},
       {"name": "spine", "parent": "hips", "head": [0, 0.95, 0],
        "length": 0.55, "direction": [0, 1, 0]},          # tail = head + length * unit(direction)
       {"name": "head", "parent": "spine", "connect": true, # head = the parent's tail
        "tail": [0, 2, 0]}]}

Rules (strict bm-forge choices, see references/OPEN_QUESTIONS.md 37-40):
- names 1..15 bytes UTF-8, unique (C does not check, anim.validate_rig does);
- 1..64 bones (bm.h:179; bm Animator "at most 64 bones", carts/animator/main.lua:416);
- exactly one root; every other parent must name a bone of the description
  (an unknown parent = orphan bone, refused); no cycles;
- bones are reordered parents-first (stable: description order otherwise),
  because the format needs parent < index (src/bm/format.c:257-259);
- head/tail finite and in (-1e6, 1e6), the vertex range of MESH
  (src/bm/format.c:157; the ANIM check only asks |v| < 1e9, format.c:241);
- bone length >= 1e-3: bm Animator never makes a zero-length bone (it moves
  the tail when dist2 < 1e-6, carts/animator/main.lua:423).
"""
import json
import math

from . import anim as animmod
from .binary import name_bytes
from .errors import ValidationError, need

COORD_LIMIT = 1e6        # src/bm/format.c:157 (MESH vertices); strict for bone points too
MIN_LENGTH = 1e-3        # carts/animator/main.lua:423 (dist2 < 1e-6 -> tail moved)
BONE_KEYS = {"name", "parent", "head", "tail", "length", "direction", "connect"}
TOP_KEYS = {"model", "bones", "comment"}
STRATEGIES = ("nearest_bone", "nearest_bone_part")


def _vec3(v, what, where):
    need(isinstance(v, (list, tuple)) and len(v) == 3, f"{what} must be 3 numbers, not {v!r}", where)
    out = []
    for x in v:
        need(isinstance(x, (int, float)) and not isinstance(x, bool), f"{what} {v!r}: not a number", where)
        need(math.isfinite(x) and -COORD_LIMIT < x < COORD_LIMIT,
             f"{what} {v!r} not finite in (-1e6, 1e6) (src/bm/format.c:157)", where)
        out.append(float(x))
    return out


def load(path_or_dict):
    """a dict, or the path of a JSON file -> dict"""
    if isinstance(path_or_dict, dict):
        return path_or_dict
    with open(path_or_dict, encoding="utf-8") as f:
        return json.load(f)


def skeleton(desc):
    """description (dict or JSON path) -> anim.Rig with bones, no vbones, no clips.
    Raises ValidationError with the bone and the rule broken."""
    d = load(desc)
    need(isinstance(d, dict), "a skeleton description is a JSON object", "skeleton")
    extra = set(d) - TOP_KEYS
    need(not extra, f"unknown keys {sorted(extra)} (allowed: {sorted(TOP_KEYS)})", "skeleton")
    need("model" in d, "no 'model' name (the MESH model the rig moves, src/bm/runtime.c:2161)", "skeleton")
    model = d["model"]
    name_bytes(model, "skeleton", "model name")
    bones = d.get("bones")
    need(isinstance(bones, list) and bones, "'bones' must be a non-empty list", "skeleton")
    need(len(bones) <= animmod.BONES_MAX,
         f"{len(bones)} bones, at most {animmod.BONES_MAX} (src/bm/bm.h:179)", "skeleton")
    names, byname = [], {}
    for i, b in enumerate(bones):
        w = f"skeleton bone {i}"
        need(isinstance(b, dict), "a bone is a JSON object", w)
        need("name" in b, "no 'name'", w)
        name_bytes(b["name"], w, "bone name")
        w = f"skeleton bone {b['name']!r}"
        extra = set(b) - BONE_KEYS
        need(not extra, f"unknown keys {sorted(extra)} (allowed: {sorted(BONE_KEYS)})", w)
        need(b["name"] not in byname, "bone name twice", w)
        byname[b["name"]] = b
        names.append(b["name"])
    roots = [n for n in names if byname[n].get("parent") is None]
    need(len(roots) == 1, f"{len(roots)} root bones {roots}: exactly one bone has parent null "
         "(strict; bm Animator starts every skeleton from one root, carts/animator/main.lua:389)", "skeleton")
    for n in names:
        p = byname[n].get("parent")
        if p is not None:
            need(isinstance(p, str), f"parent {p!r} must be a bone name or null", f"skeleton bone {n!r}")
            need(p != n, "a bone cannot be its own parent", f"skeleton bone {n!r}")
            need(p in byname, f"orphan bone: parent {p!r} is not a bone of this skeleton", f"skeleton bone {n!r}")
    # parents first, stable (src/bm/format.c:257-259 needs parent < index)
    order, placed = [], set()
    while len(order) < len(names):
        moved = False
        for n in names:
            p = byname[n].get("parent")
            if n not in placed and (p is None or p in placed):
                order.append(n)
                placed.add(n)
                moved = True
        if not moved:
            left = [n for n in names if n not in placed]
            raise ValidationError(f"cycle in the parents of {left}: no root reaches them", "skeleton")
    index = {n: i for i, n in enumerate(order)}
    out, tails = [], {}
    for n in order:
        b = byname[n]
        w = f"skeleton bone {n!r}"
        p = b.get("parent")
        if b.get("connect"):
            need(p is not None, "'connect' needs a parent (head = the parent's tail)", w)
            need("head" not in b, "'connect' and 'head' together", w)
            head = list(tails[p])
        else:
            need("head" in b, "no 'head' (or 'connect': true)", w)
            head = _vec3(b["head"], "head", w)
        has_tail, has_dir = "tail" in b, ("length" in b or "direction" in b)
        need(has_tail != has_dir, "give either 'tail' or 'length' + 'direction'", w)
        if has_tail:
            tail = _vec3(b["tail"], "tail", w)
        else:
            need("length" in b and "direction" in b, "'length' and 'direction' go together", w)
            ln = b["length"]
            need(isinstance(ln, (int, float)) and not isinstance(ln, bool) and math.isfinite(ln) and ln > 0,
                 f"length {ln!r} must be a positive number", w)
            dv = _vec3(b["direction"], "direction", w)
            dl = math.sqrt(sum(x * x for x in dv))
            need(dl > 1e-9, "direction is zero", w)
            tail = [head[k] + dv[k] / dl * ln for k in range(3)]
            _vec3(tail, "tail", w)
        length = math.sqrt(sum((tail[k] - head[k]) ** 2 for k in range(3)))
        need(length >= MIN_LENGTH, f"bone length {length:.6g} < {MIN_LENGTH} (head == tail; "
             "bm Animator never makes one, carts/animator/main.lua:423)", w)
        tails[n] = tail
        out.append(animmod.Bone(n, -1 if p is None else index[p], head, tail))
    rig = animmod.Rig(model, out, [], [])
    return rig


def _seg_dist2(p, a, b):
    ab = [b[k] - a[k] for k in range(3)]
    ap = [p[k] - a[k] for k in range(3)]
    l2 = sum(x * x for x in ab)
    u = 0.0 if l2 <= 0 else max(0.0, min(1.0, sum(ap[k] * ab[k] for k in range(3)) / l2))
    return sum((ap[k] - ab[k] * u) ** 2 for k in range(3))


def _nearest(p, bones):
    best, bi = None, 0
    for i, b in enumerate(bones):
        d = _seg_dist2(p, b.head, b.tail)
        if best is None or d < best - 1e-12:        # ties -> the lower index (deterministic)
            best, bi = d, i
    return bi


def _parts(model):
    """connected parts of a model (vertices joined by faces) -> part id per vertex"""
    parent = list(range(len(model.verts)))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for f in model.faces:
        ra, rb, rc = find(f.a), find(f.b), find(f.c)
        parent[rb] = ra
        parent[find(rc)] = ra
    return [find(i) for i in range(len(model.verts))]


def assign_vertices(model, rig, strategy="nearest_bone"):
    """one bone index per vertex of `model` (rigid skinning, the only kind the
    format has: src/bm/format.c:266-269). Strategies:
    - "nearest_bone": each vertex to the bone whose rest segment head-tail is
      nearest (ties -> lower bone index);
    - "nearest_bone_part": each connected part (vertices joined by faces) to
      the bone nearest to the part's centroid, so a box limb never splits at a
      joint (the village villager is skinned that way: one box, one bone).
    -> list of ints, len == len(model.verts)"""
    need(strategy in STRATEGIES, f"strategy {strategy!r}: one of {STRATEGIES}", "assign_vertices")
    need(rig.bones, "a rig with no bones", "assign_vertices")
    if strategy == "nearest_bone":
        return [_nearest(v, rig.bones) for v in model.verts]
    part = _parts(model)
    groups = {}
    for i, p in enumerate(part):
        groups.setdefault(p, []).append(i)
    out = [0] * len(model.verts)
    for p in sorted(groups):
        vs = groups[p]
        c = [sum(model.verts[i][k] for i in vs) / len(vs) for k in range(3)]
        bi = _nearest(c, rig.bones)
        for i in vs:
            out[i] = bi
    return out


def bind(model, rig, vbones=None, strategy="nearest_bone_part"):
    """a copy of `rig` bound to `model`: same name (the link is by name,
    src/bm/runtime.c:2161), one bone per vertex (given, or assign_vertices),
    len(vbones) == len(model.verts). Clips are kept. -> anim.Rig, checked."""
    w = f"bind rig {rig.model!r}"
    need(rig.model == model.name, f"rig is for model {rig.model!r}, the model is {model.name!r} "
         "(they link by name, src/bm/runtime.c:2161)", w)
    vb = list(vbones) if vbones is not None else assign_vertices(model, rig, strategy)
    need(len(vb) == len(model.verts), f"{len(vb)} vertex bones, the model has {len(model.verts)} vertices "
         "(src/bm/runtime.c:2161)", w)
    out = animmod.Rig(rig.model, [animmod.Bone(b.name, b.parent, list(b.head), list(b.tail)) for b in rig.bones],
                      vb, list(rig.clips))
    animmod.validate_rig(out)
    return out


def describe(rig):
    """anim.Rig -> a skeleton description (parents by name), the inverse of skeleton()"""
    return {"model": rig.model,
            "bones": [{"name": b.name, "parent": None if b.parent < 0 else rig.bones[b.parent].name,
                       "head": list(b.head), "tail": list(b.tail)} for b in rig.bones]}


def mirror_name(n):
    """the other side's name, as bm Animator's mirror_name (carts/animator/main.lua:59-65):
    .L/.R, _L/_R suffixes, L_/R_ prefixes, Left/Right; None if no side"""
    import re
    for pat, rep in ((r"\.L$", ".R"), (r"\.R$", ".L"), (r"_L$", "_R"), (r"_R$", "_L"), (r"^L_", "R_"),
                     (r"^R_", "L_"), ("Left", "Right"), ("left", "right"), ("Right", "Left"), ("right", "left")):
        if re.search(pat, n):
            return re.sub(pat, rep, n, count=1)
    return None
