"""Detail test: how much of a high-detail model survives polygon reduction, and how it compares with a model built
low-poly from the start. Optional dependencies: numpy and Pillow (not needed by bm-forge itself).

  python detail_test.py OUT_DIR [--reuse]

Builds the reference (the highest `detail` of mannequin.py that fits the tools' 4096 vertices) and an "ultra" one
(over the tools' limit, kernel limits only), reduces them with the repo's own reducer (tools/bmreduce.py, the same
code as bm Studio's "reduce": quadric edge collapse, a vertex keeps its bone), builds direct low-poly versions, and
measures each against the reference:
  - error in centimetres: distance from the reference surface to the candidate surface (what is lost) and back
    (what material was added), by body part (head, torso, arms, hands, legs, feet), from 20000 points per surface;
  - silhouette IoU from the front, the side and the top (512 px).
Writes OUT_DIR/*.bm, OUT_DIR/results.json and prints a Markdown table. --reuse measures the files already there.
"""
import json
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

from bm_core import anim as A, container, export, mesh as Mesh  # noqa: E402
import mannequin  # noqa: E402

GROUPS = {"head": ("head",), "torso": ("hips", "spine"), "arms": ("arm.", "forearm."), "hands": ("hand.",),
          "legs": ("leg.", "shin."), "feet": ("foot.",)}


def load(path):
    cart = container.read_file(path, kernel_limits=True)
    model = cart.mesh(kernel_limits=True).models[0]
    rig = cart.anim(kernel_limits=True).rigs[0]
    verts = np.array(model.verts, dtype=np.float64)
    tris = np.array([(f.a, f.b, f.c) for f in model.faces], dtype=np.int64)
    bone_of_vertex = np.array(rig.vbones)
    names = [b.name for b in rig.bones]
    group_of_bone = []
    for n in names:
        gi = next(i for i, (g, keys) in enumerate(GROUPS.items()) if any(n.startswith(k) for k in keys))
        group_of_bone.append(gi)
    face_group = np.array(group_of_bone)[bone_of_vertex[tris[:, 0]]]
    return verts, tris, face_group


def sample(verts, tris, face_group, n, rng):
    a, b, c = verts[tris[:, 0]], verts[tris[:, 1]], verts[tris[:, 2]]
    area = 0.5 * np.linalg.norm(np.cross(b - a, c - a), axis=1)
    idx = rng.choice(len(tris), size=n, p=area / area.sum())
    u, v = rng.random(n), rng.random(n)
    flip = u + v > 1
    u[flip], v[flip] = 1 - u[flip], 1 - v[flip]
    pts = a[idx] + (b[idx] - a[idx]) * u[:, None] + (c[idx] - a[idx]) * v[:, None]
    return pts, face_group[idx]


def nearest(p, q, chunk=400):
    """distance from each point of p to the nearest point of q"""
    q2 = (q * q).sum(1)
    out = np.empty(len(p))
    for i in range(0, len(p), chunk):
        a = p[i:i + chunk]
        d = (a * a).sum(1)[:, None] + q2[None, :] - 2.0 * a @ q.T
        out[i:i + chunk] = np.sqrt(np.maximum(d.min(1), 0.0))
    return out


def silhouette(verts, tris, view, box, res=512):
    ax = {"front": (0, 1), "side": (2, 1), "top": (0, 2)}[view]
    lo, hi = box[0][list(ax)], box[1][list(ax)]
    scale = (res - 8) / max(hi - lo)
    img = Image.new("L", (res, res), 0)
    dr = ImageDraw.Draw(img)
    p = (verts[:, list(ax)] - lo) * scale + 4
    p[:, 1] = res - p[:, 1]
    for t in tris:
        dr.polygon([tuple(p[t[0]]), tuple(p[t[1]]), tuple(p[t[2]])], fill=255)
    return np.asarray(img) > 0


def iou(a, b):
    return float((a & b).sum() / max((a | b).sum(), 1))


REUSE = "--reuse" in sys.argv                # measure again the files already in OUT_DIR


def _out(cmd):
    """the file a command writes: the -o argument, or the third argument of mannequin.py"""
    return cmd[cmd.index("-o") + 1] if "-o" in cmd else cmd[3]


def run(cmd):
    if REUSE and os.path.exists(_out(cmd)):                              # measure again what is already there
        return
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(r.stderr or r.stdout)


def largest_detail():
    d = 4.5
    while d > 1.0:
        if len(mannequin.build(d)[0].verts) <= 4096:
            return round(d, 2)
        d -= 0.05
    return 1.0


def main(out):
    os.makedirs(out, exist_ok=True)
    py = sys.executable
    mk = os.path.join(HERE, "mannequin.py")
    ref_detail = largest_detail()                                       # the largest that fits the tools' 4096 vertices
    print(f"reference detail {ref_detail}", file=sys.stderr)
    files = {}
    run([py, mk, str(ref_detail), os.path.join(out, "reference.bm")])
    run([py, mk, "6.0", os.path.join(out, "ultra.bm")])
    for d in (0.5, 0.8, 1.0, 1.2, 1.5, 2.0):
        run([py, mk, str(d), os.path.join(out, f"direct_{d}.bm")])
        files[f"direct detail {d}"] = f"direct_{d}.bm"
    for n in (4000, 2000, 1200, 800, 600, 400):
        run([py, os.path.join(ROOT, "tools", "bmreduce.py"), os.path.join(out, "reference.bm"), "--faces", str(n),
             "-o", os.path.join(out, f"ref_r{n}.bm")])
        files[f"reference reduced to {n}"] = f"ref_r{n}.bm"
    run([py, os.path.join(ROOT, "tools", "bmreduce.py"), os.path.join(out, "ultra.bm"), "--faces", "1200",
         "-o", os.path.join(out, "ultra_r1200.bm")])
    files[f"ultra source ({len(mannequin.build(6.0)[0].faces)} tris) reduced to 1200"] = "ultra_r1200.bm"

    rv, rt, rg = load(os.path.join(out, "reference.bm"))
    rng = np.random.default_rng(1)
    rp, rpg = sample(rv, rt, rg, 20000, rng)
    box = (rv.min(0) - 0.02, rv.max(0) + 0.02)
    ref_sil = {v: silhouette(rv, rt, v, box) for v in ("front", "side", "top")}
    rows = []
    for label, f in files.items():
        v, t, g = load(os.path.join(out, f))
        cp, cpg = sample(v, t, g, 40000, rng)
        to_cand = nearest(rp, cp) * 100                                  # cm, reference -> candidate
        to_ref = nearest(cp[:20000], rp) * 100                           # cm, candidate -> reference
        row = {"label": label, "tris": int(len(t)), "verts": int(len(v)),
               "lost_mean_cm": float(to_cand.mean()), "lost_p95_cm": float(np.percentile(to_cand, 95)),
               "added_mean_cm": float(to_ref.mean())}
        for vw in ("front", "side", "top"):
            row["iou_" + vw] = iou(ref_sil[vw], silhouette(v, t, vw, box))
        for gi, gn in enumerate(GROUPS):
            sel = rpg == gi
            row["lost_" + gn] = float(to_cand[sel].mean()) if sel.any() else None
        rows.append(row)
    rows.sort(key=lambda r: r["tris"])
    with open(os.path.join(out, "results.json"), "w") as fh:
        json.dump(rows, fh, indent=1)
    print("| candidate | tris | lost mean cm | lost p95 cm | added mean cm | IoU front/side/top | head | torso | arms | hands | legs | feet |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|")
    for r in rows:
        print(f"| {r['label']} | {r['tris']} | {r['lost_mean_cm']:.2f} | {r['lost_p95_cm']:.2f} | {r['added_mean_cm']:.2f} | "
              f"{r['iou_front']:.3f}/{r['iou_side']:.3f}/{r['iou_top']:.3f} | {r['lost_head']:.2f} | {r['lost_torso']:.2f} | {r['lost_arms']:.2f} | "
              f"{r['lost_hands']:.2f} | {r['lost_legs']:.2f} | {r['lost_feet']:.2f} |")


if __name__ == "__main__":
    main(sys.argv[1])

