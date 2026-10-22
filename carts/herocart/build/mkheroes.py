#!/usr/bin/env python3
"""
mkheroes.py: makes carts/herocart/assets/models.bm, the hero in four levels of
detail and four outline hulls (docs/design/hero-stress-cart-and-cel-shader.md,
sections 3.2 and 3.3).

  1. the bm-forge charkit hero (preset rango) with the bm-forge walk clip;
  2. the console's polygon reducer (scripts/bmdecimate.py, quadric edge
     collapse) makes hero1, hero2, hero3 from hero0 (the source);
  3. each level gets a hull, hero0_o ... hero3_o: the same mesh, vertices moved
     out along the vertex normal, winding reversed, one dark emissive colour.
     Every model has the same skeleton and clip (the walk).

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
    for m, v in levels:
        out_models.append(m)
        rigs.append((m["name"], bones, v, clips))
    for m, v in levels:
        h = hull(m)
        h["name"] = m["name"] + "_o"
        out_models.append(h)
        rigs.append((h["name"], bones, v, clips))
    secs[bmmesh.SEC_MESH] = bmmesh.encode(out_models, inset)
    secs[bmmesh.SEC_ANIM] = bmmesh.encode_anim(rigs)
    with open(OUT, "wb") as f:
        f.write(bmmesh.rewrite_cart(data, secs))
    return check(full)


def check(full=None):
    secs = dict(bmmesh.cart_sections(open(OUT, "rb").read()))
    models, _ = bmmesh.decode(secs[bmmesh.SEC_MESH])
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
            ok = abs(got - want) <= 0.15 * want and n in rigs and rigs[n][3][0]["name"] == "walk"
            print(f"{n:9s} {got:5d} triangles (target {want:6.0f}) {'ok' if ok else 'OFF'}")
            if not ok:
                bad.append(n)
    print(f"8 models, {total} triangles, {os.path.getsize(OUT)} bytes")
    return 1 if bad else 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true")
    sys.exit(check() if ap.parse_args().check else build())
