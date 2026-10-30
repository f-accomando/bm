#!/usr/bin/env python3
"""
mkheroes.py: makes carts/hero/assets/models.bm, the hero in four levels of
detail and four outline hulls (docs/design/hero-stress-cart-and-cel-shader.md,
sections 3.2 and 3.3).

  1. the bm-forge charkit hero (preset rango) with the bm-forge walk clip;
  2. the console's polygon reducer (scripts/bmdecimate.py, quadric edge
     collapse) makes hero1, hero2, hero3 from hero0 (the source);
  3. each level gets a hull, hero0_o ... hero3_o: the same mesh, vertices moved
     out along the vertex normal, winding reversed, one dark emissive colour.
     Every model has the same skeleton and clip (the walk);
  4. every face has its three corners on one bone (vertices split at the joints): the console's
     vertex shader takes a mesh of plain lit faces only then (one_bone_faces).

  mkheroes.py            make assets/models.bm (needs build/host/libbmdecimate.so:
                         `make build/host/libbmdecimate.so`, in WSL on Windows)
  mkheroes.py --check    print the triangles of the 8 models of assets/models.bm
                         and fail if a level is off its target by more than 15%

The result is committed, so a normal build needs none of this.
"""
import argparse
import math
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
OUT = os.path.join(HERE, "..", "assets", "models.bm")
FORGE = os.path.join(ROOT, ".claude", "skills", "bm-forge")
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmmesh  # noqa: E402

PRESET = "rango"
RATIOS = (1.0, 0.5, 0.25, 0.1)         # triangles of hero0..hero3, a fraction of the source
HULL_PUSH = 0.02                       # of the hero's height
HULL_COLOUR = 0x40000000 | 0x181820    # emissive (material bit 30), dark


def source():
    """charkit hero + walk clip -> the bytes of a .bm"""
    with tempfile.TemporaryDirectory() as tmp:
        raw, rigged = os.path.join(tmp, "hero.bm"), os.path.join(tmp, "hero_walk.bm")
        subprocess.run([sys.executable, os.path.join(FORGE, "examples", "charkit", "charkit.py"),
                        os.path.join(FORGE, "examples", "charkit", "presets", PRESET + ".json"), raw],
                       check=True, stdout=subprocess.DEVNULL)
        subprocess.run([sys.executable, os.path.join(FORGE, "scripts", "bm_forge.py"), "anim",
                        os.path.join(FORGE, "examples", "walk.json"), "--in", raw, "-o", rigged],
                       check=True, stdout=subprocess.DEVNULL)
        return open(rigged, "rb").read()


def one_bone_faces(model, vb):
    """splits the vertices so that every face has all its corners on one bone.

    The console's vertex shader (gpu3d.c, mesh_build) draws a face on two bones only when it is
    textured and lit; a hero of plain coloured faces on two bones sends the whole mesh back to the
    ARM, one triangle at a time. A face takes the bone of two of its corners (of its first corner
    when the three differ, the lowest bone number for a tie that has no majority). A corner on
    another bone becomes a copy of the vertex on the face's bone. The copies lie at the same rest
    position, so the pose of a joint opens a thin gap, as the rigid parts of the bench hero do."""
    verts, faces = model["verts"], model["faces"]
    out_v, out_b, seen, out_f = list(verts), list(vb), {}, []
    for a, b, c, *rest in faces:
        bs = (vb[a], vb[b], vb[c])
        if bs[0] == bs[1] or bs[0] == bs[2]:
            bone = bs[0]
        elif bs[1] == bs[2]:
            bone = bs[1]
        else:
            bone = min(bs)
        corner = []
        for v in (a, b, c):
            if vb[v] != bone:
                key = (v, bone)
                if key not in seen:
                    seen[key] = len(out_v)
                    out_v.append(verts[v])
                    out_b.append(bone)
                v = seen[key]
            corner.append(v)
        out_f.append((*corner, *rest))
    m = dict(model)
    m["verts"], m["faces"] = out_v, out_f
    return m, out_b


def hull(model):
    """the model pushed out along its vertex normals, winding reversed, one colour"""
    verts, faces = model["verts"], model["faces"]
    nrm = [[0.0, 0.0, 0.0] for _ in verts]
    for a, b, c, *_ in faces:
        p, q, r = verts[a], verts[b], verts[c]
        u = (q[0] - p[0], q[1] - p[1], q[2] - p[2])
        v = (r[0] - p[0], r[1] - p[1], r[2] - p[2])
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        for i in (a, b, c):                  # area weighted: the cross product is not normalised
            for k in range(3):
                nrm[i][k] += n[k]
    ys = [p[1] for p in verts]
    push = HULL_PUSH * (max(ys) - min(ys))
    out = []
    for p, n in zip(verts, nrm):
        ln = math.sqrt(sum(x * x for x in n)) or 1.0
        # (b-a)x(c-a) of a visible face points out of the mesh (checked on the source hero)
        out.append(tuple(p[k] + push * n[k] / ln for k in range(3)))
    return {"name": "", "verts": out,
            "faces": [(a, c, b, HULL_COLOUR, (0,) * 6) for a, b, c, *_ in faces]}


TRIS_LUA = os.path.join(HERE, "..", "src", "05_tris.lua")


def tris_lua(count):
    """the text of src/05_tris.lua: the triangles of hero0..hero3, read by the options panel"""
    t = ", ".join(str(count[f"hero{i}"]) for i in range(len(RATIOS)))
    return ("-- Made by build/mkheroes.py from models.bm: do not edit. The triangles of one hero, level 0..3."
            "\n"
            f"H.lod_tris = {{ {t} }}"
            "\n")


def build():
    import bmdecimate
    data = source()
    secs = dict(bmmesh.cart_sections(data))
    models, inset = bmmesh.decode(secs[bmmesh.SEC_MESH])
    (name, bones, vb, clips), = bmmesh.decode_anim(secs[bmmesh.SEC_ANIM])
    base = models[0]
    full = len(base["faces"])
    levels = []
    for i, ratio in enumerate(RATIOS):
        if ratio == 1.0:
            m, v = dict(base), list(vb)
        else:
            m, v = bmdecimate.reduce_model(base, max(1, round(full * ratio)), vb)
        m["name"] = f"hero{i}"
        levels.append((m, v))
    out_models, rigs = [], []
    split = []
    for m, v in levels:
        # the hull is made from the model whole (its normals are smooth across the joints), then
        # both are split in the same way: the hull's vertex k is the model's vertex k
        h = hull(m)
        h["name"] = m["name"] + "_o"
        split.append((one_bone_faces(m, v), one_bone_faces(h, v)))
    for (m, v), _ in split:
        out_models.append(m)
        rigs.append((m["name"], bones, v, clips))
    for _, (h, v) in split:
        out_models.append(h)
        rigs.append((h["name"], bones, v, clips))
    secs[bmmesh.SEC_MESH] = bmmesh.encode(out_models, inset)
    secs[bmmesh.SEC_ANIM] = bmmesh.encode_anim(rigs)
    with open(OUT, "wb") as f:
        f.write(bmmesh.rewrite_cart(data, secs))
    models2, _ = bmmesh.decode(dict(bmmesh.cart_sections(open(OUT, "rb").read()))[bmmesh.SEC_MESH])
    with open(TRIS_LUA, "w", encoding="utf-8", newline="\n") as f:
        f.write(tris_lua({m["name"]: len(m["faces"]) for m in models2}))
    return check(full)


def check(full=None):
    secs = dict(bmmesh.cart_sections(open(OUT, "rb").read()))
    models, _ = bmmesh.decode(secs[bmmesh.SEC_MESH])
    by_name = {m["name"]: m for m in models}
    count = {m["name"]: len(m["faces"]) for m in models}
    rigs = {r[0]: r for r in bmmesh.decode_anim(secs[bmmesh.SEC_ANIM])}
    full = full or count["hero0"]
    bad = []
    total = 0
    for i, ratio in enumerate(RATIOS):
        want = full * ratio
        for n in (f"hero{i}", f"hero{i}_o"):
            got = count[n]
            total += got
            vb = rigs[n][2]
            mixed = sum(1 for f in by_name[n]["faces"] if len({vb[f[0]], vb[f[1]], vb[f[2]]}) > 1)
            ok = abs(got - want) <= 0.15 * want and n in rigs and rigs[n][3][0]["name"] == "walk" and mixed == 0
            print(f"{n:9s} {got:5d} triangles (target {want:6.0f}) {mixed} faces on two bones {'ok' if ok else 'OFF'}")
            if not ok:
                bad.append(n)
    try:
        same = open(TRIS_LUA, encoding="utf-8").read() == tris_lua(count)
    except OSError:
        same = False
    if not same:
        print("src/05_tris.lua does not match models.bm: run mkheroes.py")
        bad.append("05_tris.lua")
    print(f"8 models, {total} triangles, {os.path.getsize(OUT)} bytes")
    return 1 if bad else 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true")
    sys.exit(check() if ap.parse_args().check else build())
