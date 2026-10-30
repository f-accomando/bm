"""enemy_build: the enemies of Dark Night (a fan study), rigged, one continuous skin each, weapons as separate models.

  villager   a shambling man: wide brimmed hat, shaggy hair and beard, green waistcoat, one arm wrapped in rags
  cleaver    his weapon (a flat blade and a handle)
  huntsman   a tall man in a top hat and a long frock coat, torn scarf, wrapped hands
  torch      the huntsman's torch (left hand)
  haxe       the huntsman's axe (right hand)
  dog        a rabid dog: a quadruped with a long snout, a jaw bone and a tail

The two men use the SAME skeleton as the Hunter (hunter_build.BONES: 24 bones, T pose), so the clips of hunter_clips work on them
(enemy_clips.py). The dog has its own (DOG_BONES). Built facing +z and mirrored to -z at the end, .L on +x, like the Hunter.
Low poly (the body of a man is about 700 triangles): N = 12 sectors, limbs of 6 sides.

  python3 enemy_build.py OUT.bm       (then build.py adds the clips and exports the .bmm)
"""
import math
import os
import sys

os.environ["HUNTER_LOD"] = "lo"
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "hunter"))
import hunter_build as HB  # noqa: E402
from hunter_build import Skin, align, frame_ring, unit, sub, add, mul, cross, dot, S, hsh, N, LM, FRONT, BACK  # noqa: E402
from bm_core import anim as A, export, mesh as M  # noqa: E402

HB.PAL.update({k: int(v.lstrip("#"), 16) for k, v in {
    "skin": "#a89880", "skin_sh": "#7d6e5b", "hair": "#2b2724", "hair_lt": "#4a443d", "beard": "#3a342e",
    "hat_v": "#34312e", "band_v": "#1e1c1b", "hat_h": "#26262c", "band_h": "#3c3c44",
    "shirt": "#b6b1a3", "shirt_dk": "#8b867a", "vest_g": "#4e5b50", "vest_dk": "#39443b",
    "coat_h": "#303136", "coat_h2": "#46484f", "vest_h": "#3b3c3f", "lining_h": "#1d1e21",
    "pants": "#5b5750", "pants_dk": "#403d38", "boot": "#22201f", "boot_lt": "#3a3633",
    "wrap": "#6b6359", "wrap_lt": "#8e8472", "rag": "#c9c5b9", "blood": "#6a2020",
    "metal": "#555a61", "metal_dk": "#34383d", "wood": "#5d452e", "wood_dk": "#3b2b1b",
    "flame": "#f0a030", "flame2": "#d8661e", "flame3": "#ffd870", "cloth": "#8a7d66",
    "fur": "#6e5d46", "fur_dk": "#3e3427", "fur_lt": "#8c7857", "claw": "#1f1c19", "tongue": "#8a4a52", "teeth": "#d9d1c0",
    "eye_r": "#c8402a",
}.items()})
PAL = HB.PAL
FLAT = HB.FLAT


def hat_brim(ctx, base_tag, scale, y_up, droop, col_top, col_under, col_edge, n_front_up=0.0):
    """a brim: the head ring `base_tag` gives the middle; rings go out, down at the edge, back under"""
    s, R, rows, tags = ctx["s"], ctx["R"], ctx["rows"], ctx["tags"]
    base, row = R[tags[base_tag]], rows[tags[base_tag]]
    under = R[tags[ctx.get("under_tag", "eyes")]]
    def ring(sc, dy, jit=0.0, tag="b"):
        pts = []
        for k in range(N):
            a = 2 * math.pi * k / N
            side = abs(math.cos(a))
            dd = dy + droop * (1 - side) * (0.0 if math.sin(a) > 0 else 1.0) + (n_front_up if math.sin(a) > 0.9 else 0.0)
            j = (hsh(tag, k) - 0.5) * jit
            pts.append((row["rx"] * sc * (1 + j) * math.cos(a), row["y"] + dd + j * 0.5, row["rz"] * sc * (1 + j) * math.sin(a)))
        return s.ring(pts, "head")
    inner = ring(1.18, y_up, 0.0, "bi")
    outer = ring(scale, y_up - 0.012, 0.10, "bo")
    edge = ring(scale, y_up - 0.03, 0.10, "bo")
    below = ring(1.25, y_up - 0.035, 0.0, "bu")
    s.bridge(base, inner, PAL[col_top])
    s.bridge(inner, outer, PAL[col_top])
    s.bridge(outer, edge, PAL[col_edge])
    s.bridge(edge, below, PAL[col_under])
    s.bridge(below, under, PAL[col_under])


def tents_between(ctx, lo_tag, hi_tag, sectors, down, out, col, bone="head", jit=0.0, tag="t"):
    """a pyramid on each chosen quad between two rings (shaggy hair, a beard, rags): the tip hangs `down` and goes `out`"""
    s, R, rows, tags = ctx["s"], ctx["R"], ctx["rows"], ctx["tags"]
    lo, hi = R[tags[lo_tag]], R[tags[hi_tag]]
    for k in sectors:
        k %= N
        base = [lo[k], lo[(k + 1) % N], hi[(k + 1) % N], hi[k]]
        c = s.centre(base)
        a = 2 * math.pi * (k + 0.5) / N
        d = down * (1 + (hsh(tag, k) - 0.5) * jit)
        s.tent(base, (c[0] + math.cos(a) * out, c[1] - d, c[2] + math.sin(a) * out), PAL[col], bone)


def biped(spec):
    """one continuous skin: column (torso, neck, head), hat, hair, arms, legs, an optional long coat. -> Skin"""
    s = Skin()
    rows = spec["rows"]
    R, tags = [], {}
    for i, r in enumerate(rows):
        R.append(s.ring(HB.ring_pts(r), r["bone"]))
        if "tag" in r:
            tags[r["tag"]] = i
    for i in range(len(rows) - 1):
        s.bridge(R[i], R[i + 1], HB.sector_cols(rows[i]["seg"]))
    s.cap(R[-1], PAL[spec["top"]], apex=(0.0, rows[-1]["y"] + spec.get("apex", 0.04), 0.0), bone="head")
    ctx = {"s": s, "R": R, "rows": rows, "tags": tags}
    for fn in spec.get("head_parts", []):
        fn(ctx)

    # ---- arms: the loop of torso vertices (3 columns x rows a1 and a3), then rings
    for sx, side in ((1, "L"), (-1, "R")):
        kk = lambda c: (c if sx > 0 else N // 2 - c) % N                      # noqa: E731
        cols = (-1, 0, 1)
        prev = [R[tags["a1"]][kk(c)] for c in cols] + [R[tags["a3"]][kk(c)] for c in reversed(cols)]
        rings = []
        for x, ry, rz, bone, col in spec["arms"][side]:
            ids = s.ring(align(s, prev, HB.yz_ring(sx * x, 1.48, ry, rz)), bone.replace("{S}", side))
            s.bridge(prev, ids, PAL[col])
            prev = ids
            rings.append(ids)
        x_tip = spec["arms"][side][-1][0] + 0.03
        s.cap(rings[-1], PAL[spec["arms"][side][-1][4]], apex=(sx * x_tip, 1.48, 0.0), bone="fing." + side)
        palm, knuck = rings[-3], rings[-2]
        base = [palm[3], palm[4], knuck[4], knuck[3]]
        c = s.centre(base)
        s.tent(base, (c[0] + sx * 0.05, c[1] - 0.006, c[2] + 0.085), PAL[spec["arms"][side][-1][4]], "thumb." + side)
        for fn in spec.get("arm_parts", []):
            fn(ctx, sx, side, rings)

    # ---- legs: the first ring is the half of the pelvis ring on that side
    arcs = {1: list(range(BACK, N)) + list(range(0, FRONT)), -1: list(range(FRONT, BACK))}
    for sx, side in ((1, "L"), (-1, "R")):
        prev = [R[tags["pelvis"]][k] for k in arcs[sx]]
        legs = spec["legs"]
        for i, (y, rx, rz, bone, col, cz) in enumerate(legs):
            ids = s.ring(align(s, prev, HB.xz_ring(y, sx * 0.095, rx, rz, LM, cz + 0.01)), bone.replace("{S}", side))
            s.bridge(prev, ids, PAL[legs[i - 1][4]] if i else PAL["pants"])
            prev = ids
            if i == len(legs) - 1:
                s.cap(ids, PAL["boot"], inside=(sx * 0.095, 0.2, 0.08))
    p0 = R[tags["pelvis"]]
    s.quad(p0[BACK], p0[FRONT - 1], p0[FRONT], p0[BACK - 1], PAL["pants"], (0.0, 1.0, 0.0))

    # ---- a long coat: an open arc below the belt, three bones, a ragged hem
    coat = spec.get("coat")
    if coat:
        def skirt_bone(k):
            if min((k - BACK) % N, (BACK - k) % N) <= max(1, N // 8):
                return "skirt_b"
            return "skirt.L" if math.cos(2 * math.pi * k / N) > 0 else "skirt.R"
        prev = {k: R[tags[coat["from"]]][k] for k in range(N) if k != FRONT}
        for i, r in enumerate(coat["rows"]):
            last = i == len(coat["rows"]) - 1
            ids = {}
            for k in range(N):
                if k == FRONT:
                    continue
                x, y, z = HB.pt(r, k)
                jy = (hsh("skh", k) - 0.5) * 0.07 if last else 0.0
                drop = -0.07 * max(0.0, 1 - min((k - BACK) % N, (BACK - k) % N) / 3.0) if last else 0.0
                ids[k] = s.vert((x, y + jy + drop, z), skirt_bone(k) if i else "hips")
            cols = HB.sector_cols(r["seg"])
            for k in range(N):
                k2 = (k + 1) % N
                if k in prev and k2 in prev and k in ids and k2 in ids:
                    s.quad(prev[k], prev[k2], ids[k2], ids[k], cols[k], (0.0, r["y"] - 0.05, 0.0))
            prev = ids
        hem = prev
        for k in range(N):                                     # rags: a tent under each hem edge
            k2 = (k + 1) % N
            if k in hem and k2 in hem:
                a, b = s.v[hem[k]], s.v[hem[k2]]
                tip = ((a[0] + b[0]) / 2, min(a[1], b[1]) - 0.10 - 0.08 * hsh("rag", k), (a[2] + b[2]) / 2)
                inner = s.vert(((a[0] + b[0]) / 2 * 0.95, min(a[1], b[1]) + 0.04, (a[2] + b[2]) / 2 * 0.95), skirt_bone(k))
                s.tri(hem[k], hem[k2], tip_vert(s, tip, skirt_bone(k)), PAL["coat_h2"] if hsh("rc", k) > 0.4 else PAL["lining_h"], (0, tip[1] + 0.3, 0))
    for fn in spec.get("late_parts", []):
        fn(ctx)
    return s


def tip_vert(s, p, bone):
    return s.vert(p, bone)


# ---------------------------------------------------------------- the villager

def villager():
    rows = [
        dict(y=0.93, rx=0.165, rz=0.105, bone="hips", seg=HB.S("pants"), tag="pelvis"),
        dict(y=1.04, rx=0.158, rz=0.100, bone="hips", seg=HB.S("shirt", front=("vest_g", 5), back=("shirt_dk", 5)), tag="belt"),
        dict(y=1.28, rx=0.182, rz=0.114, bone="chest", seg=HB.S("shirt", front=("vest_g", 6), back=("shirt_dk", 5)), tag="a1"),
        dict(y=1.50, rx=0.198, rz=0.114, bone="chest", seg=HB.S("shirt", front=("vest_g", 4)), tag="a3"),
        dict(y=1.57, rx=0.130, rz=0.100, bone="chest", seg=HB.S("shirt")),
        dict(y=1.65, rx=0.064, rz=0.068, bone="neck", seg=HB.S("skin")),
        dict(y=1.70, rx=0.058, rz=0.066, bone="head", seg=HB.S("beard"), tag="chin"),
        dict(y=1.76, rx=0.082, rz=0.098, bone="head", seg=HB.S("beard", over=[("skin", 8, 2)]), fz=0.016, tag="mouth"),
        dict(y=1.82, rx=0.094, rz=0.112, bone="head", seg=HB.S("skin", back=("hair", 12), over=[("hair", 0, 9), ("hair", 16, 9)]), fz=0.014, tag="eyes"),
        dict(y=1.88, rx=0.100, rz=0.118, bone="head", seg=HB.S("hat_v"), tag="hatbase"),
        dict(y=1.95, rx=0.090, rz=0.106, bone="head", seg=HB.S("hat_v"), tag="top"),
    ]

    def head(ctx):
        hat_brim(ctx, "hatbase", 2.35, 0.012, 0.05, "hat_v", "band_v", "hat_v")
        tents_between(ctx, "chin", "mouth", (1, 2, 3, 4), 0.14, 0.03, "beard", jit=0.6, tag="bd")          # the beard
        tents_between(ctx, "mouth", "eyes", (0, 1, 4, 5), 0.10, 0.05, "beard", jit=0.6, tag="bd2")
        tents_between(ctx, "eyes", "hatbase", (5, 6, 7, 8, 9, 10, 11, 0), 0.20, 0.05, "hair", jit=0.7, tag="hr")  # shaggy hair

    def rags(ctx):
        tents_between(ctx, "pelvis", "belt", tuple(range(N)), 0.12, 0.02, "shirt_dk", bone="hips", jit=0.8, tag="rg")

    wr = [(0.268, 0.078, 0.074, "upper.{S}", "shirt"), (0.400, 0.066, 0.062, "upper.{S}", "shirt_dk"),
          (0.480, 0.060, 0.057, "fore.{S}", "skin"), (0.545, 0.064, 0.060, "fore.{S}", "skin"),
          (0.665, 0.058, 0.055, "fore.{S}", "skin"), (0.738, 0.052, 0.048, "hand.{S}", "skin"),
          (0.800, 0.060, 0.036, "hand.{S}", "skin"), (0.860, 0.058, 0.033, "hand.{S}", "skin"),
          (0.930, 0.052, 0.029, "fing.{S}", "skin")]
    # the left arm is wrapped in rags and fur: thick, in bands
    wl = [(0.268, 0.090, 0.086, "upper.{S}", "shirt"), (0.400, 0.088, 0.084, "upper.{S}", "wrap"),
          (0.480, 0.086, 0.082, "fore.{S}", "wrap_lt"), (0.545, 0.098, 0.094, "fore.{S}", "wrap"),
          (0.665, 0.096, 0.092, "fore.{S}", "wrap_lt"), (0.738, 0.080, 0.076, "hand.{S}", "wrap"),
          (0.800, 0.082, 0.062, "hand.{S}", "wrap_lt"), (0.860, 0.080, 0.058, "hand.{S}", "wrap"),
          (0.930, 0.066, 0.046, "fing.{S}", "wrap_lt")]

    def frays(ctx, sx, side, rings):
        if side == "L":                                       # a fringe of rags hanging from the wrapped forearm
            lo, hi = rings[3], rings[4]
            for k in range(LM):
                base = [lo[k], lo[(k + 1) % LM], hi[(k + 1) % LM], hi[k]]
                c = ctx["s"].centre(base)
                ctx["s"].tent(base, (c[0], c[1] - 0.07 - 0.05 * hsh("fr", k), c[2]), PAL["wrap_lt"], "fore.L")

    legs = [(0.800, 0.092, 0.096, "thigh.{S}", "pants", 0.0), (0.520, 0.068, 0.072, "shin.{S}", "pants", 0.0),
            (0.472, 0.088, 0.092, "shin.{S}", "boot_lt", 0.0), (0.285, 0.062, 0.066, "shin.{S}", "boot", 0.0),
            (0.088, 0.060, 0.070, "foot.{S}", "boot", 0.005), (0.052, 0.068, 0.104, "foot.{S}", "boot", 0.040),
            (0.022, 0.070, 0.138, "foot.{S}", "boot", 0.074), (0.004, 0.069, 0.140, "foot.{S}", "boot", 0.075)]
    return dict(rows=rows, top="hat_v", head_parts=[head], arm_parts=[frays], late_parts=[rags],
                arms={"L": wl, "R": wr}, legs=legs, apex=0.03)


# ---------------------------------------------------------------- the huntsman

def huntsman():
    rows = [
        dict(y=0.93, rx=0.165, rz=0.105, bone="hips", seg=HB.S("pants"), tag="pelvis"),
        dict(y=1.06, rx=0.160, rz=0.102, bone="hips", seg=HB.S("coat_h", front=("vest_h", 4)), tag="belt"),
        dict(y=1.30, rx=0.178, rz=0.112, bone="chest", seg=HB.S("coat_h", front=("vest_h", 5)), tag="a1"),
        dict(y=1.52, rx=0.205, rz=0.118, bone="chest", seg=HB.S("coat_h2", front=("shirt", 3)), tag="a3"),
        dict(y=1.585, rx=0.160, rz=0.118, bone="chest", seg=HB.S("coat_h2")),
        dict(y=1.67, rx=0.100, rz=0.094, bone="neck", seg=HB.S("rag")),
        dict(y=1.72, rx=0.062, rz=0.068, bone="head", seg=HB.S("skin"), tag="chin"),
        dict(y=1.78, rx=0.082, rz=0.098, bone="head", seg=HB.S("skin", over=[("beard", 8, 3)]), fz=0.016, tag="mouth"),
        dict(y=1.84, rx=0.092, rz=0.110, bone="head", seg=HB.S("skin", back=("hair", 12), over=[("hair", 0, 9), ("hair", 16, 9)]), fz=0.012, tag="eyes"),
        dict(y=1.90, rx=0.098, rz=0.115, bone="head", seg=HB.S("band_h"), tag="hatbase"),
        dict(y=1.94, rx=0.108, rz=0.124, bone="head", seg=HB.S("hat_h")),
        dict(y=2.12, rx=0.104, rz=0.120, bone="head", seg=HB.S("hat_h"), tag="top"),
    ]

    def head(ctx):
        hat_brim(ctx, "hatbase", 1.55, 0.015, 0.0, "hat_h", "band_h", "hat_h")
        tents_between(ctx, "eyes", "hatbase", (5, 6, 7, 8, 9, 10, 11, 0), 0.12, 0.03, "hair", jit=0.7, tag="hh")
        tents_between(ctx, "chin", "mouth", (2, 3), 0.06, 0.02, "beard", jit=0.5, tag="hb")

    def scarf(ctx):                                           # torn strips of cloth hanging from the neck
        s, R, tags = ctx["s"], ctx["R"], ctx["tags"]
        lo, hi = R[tags["a3"] + 1], R[tags["a3"] + 2]
        for k in (1, 2, 3, 4, 5, 8, 10):
            base = [lo[k], lo[(k + 1) % N], hi[(k + 1) % N], hi[k]]
            c = s.centre(base)
            s.tent(base, (c[0], c[1] - 0.26 - 0.1 * hsh("sc", k), c[2] + (0.03 if 1 <= k <= 5 else -0.02)), PAL["rag"], "chest")

    sleeve = [(0.268, 0.082, 0.078, "upper.{S}", "coat_h"), (0.400, 0.070, 0.066, "upper.{S}", "coat_h2"),
              (0.480, 0.064, 0.060, "fore.{S}", "coat_h"), (0.545, 0.068, 0.064, "fore.{S}", "coat_h2"),
              (0.665, 0.064, 0.060, "fore.{S}", "coat_h"), (0.738, 0.060, 0.056, "hand.{S}", "wrap"),
              (0.800, 0.070, 0.044, "hand.{S}", "wrap_lt"), (0.860, 0.068, 0.040, "hand.{S}", "wrap"),
              (0.930, 0.058, 0.034, "fing.{S}", "wrap_lt")]

    def frays(ctx, sx, side, rings):                          # frayed wrappings at the wrists
        lo, hi = rings[4], rings[5]
        for k in range(0, LM, 2):
            base = [lo[k], lo[(k + 1) % LM], hi[(k + 1) % LM], hi[k]]
            c = ctx["s"].centre(base)
            ctx["s"].tent(base, (c[0], c[1] - 0.09 - 0.05 * hsh("fw", k + sx), c[2]), PAL["wrap_lt"], "hand." + side)

    legs = [(0.800, 0.090, 0.094, "thigh.{S}", "pants_dk", 0.0), (0.520, 0.066, 0.070, "shin.{S}", "pants_dk", 0.0),
            (0.472, 0.086, 0.090, "shin.{S}", "boot_lt", 0.0), (0.285, 0.060, 0.064, "shin.{S}", "boot", 0.0),
            (0.088, 0.058, 0.068, "foot.{S}", "boot", 0.005), (0.052, 0.066, 0.102, "foot.{S}", "boot", 0.040),
            (0.022, 0.068, 0.136, "foot.{S}", "boot", 0.074), (0.004, 0.067, 0.138, "foot.{S}", "boot", 0.075)]
    coat = dict(**{"from": "belt"}, rows=[
        dict(y=1.04, rx=0.172, rz=0.120, seg=HB.S("coat_h")),
        dict(y=0.85, rx=0.205, rz=0.150, seg=HB.S("coat_h2")),
        dict(y=0.62, rx=0.235, rz=0.178, seg=HB.S("coat_h")),
        dict(y=0.40, rx=0.255, rz=0.195, seg=HB.S("lining_h")),
    ])
    return dict(rows=rows, top="hat_h", head_parts=[head, scarf], arm_parts=[frays], arms={"L": sleeve, "R": sleeve},
                legs=legs, coat=coat, apex=0.005)


# ---------------------------------------------------------------- the weapons (origin at the grip, tip toward +z while building)

def plate_xz(s, pts, th, col, edge_col, bone, inside=(0.0, 0.0, 0.0)):
    """a flat blade from a polygon of (z, y) points, th thick (x +-th/2)"""
    sp = [s.vert((th / 2, y, z), bone) for z, y in pts]
    sm = [s.vert((-th / 2, y, z), bone) for z, y in pts]
    c = (0.0, sum(p[1] for p in pts) / len(pts), sum(p[0] for p in pts) / len(pts))
    for i in range(1, len(pts) - 1):
        s.tri(sp[0], sp[i], sp[i + 1], col, c)
        s.tri(sm[0], sm[i], sm[i + 1], col, c)
    for i in range(len(pts)):
        j = (i + 1) % len(pts)
        s.quad(sp[i], sp[j], sm[j], sm[i], edge_col, c)


def build_cleaver():
    s = Skin()
    P = PAL.__getitem__
    hr = HB.tube(s, [(0, 0, -0.14), (0, 0, -0.02), (0, 0, 0.10), (0, 0, 0.16)], [0.02, 0.019, 0.019, 0.024], "cleaver",
                 lambda i: P("wood") if i != 1 else P("wood_dk"), n=5)
    s.cap(hr[0], P("wood_dk"), bone="cleaver")
    plate_xz(s, [(0.14, 0.05), (0.56, 0.10), (0.60, -0.14), (0.14, -0.08)], 0.014, P("metal"), P("metal_dk"), "cleaver")
    for z, y in ((0.30, -0.02), (0.44, 0.02), (0.50, -0.07)):       # old blood on the blade
        s.tri(s.vert((0.0075, y, z), "cleaver"), s.vert((0.0075, y - 0.05, z + 0.02), "cleaver"),
              s.vert((0.0075, y - 0.03, z - 0.04), "cleaver"), P("blood"), (-1, y, z))
    return HB.finish(s, "cleaver", [{"name": "cleaver", "parent": None, "head": [0, 0, -0.14], "tail": [0, 0, 0.60]}])


def build_torch():
    s = Skin()
    P = PAL.__getitem__
    sr = HB.tube(s, [(0, 0, -0.22), (0, 0, 0.0), (0, 0, 0.20), (0, 0, 0.34)], [0.024, 0.026, 0.028, 0.045], "torch",
                 lambda i: P("wood") if i != 2 else P("wood_dk"), n=5)
    s.cap(sr[0], P("wood_dk"), bone="torch")
    top = [(0.0, 0.0, 0.34), (0.0, 0.0, 0.40)]
    cloth = HB.tube(s, [(0, 0, 0.34), (0, 0, 0.40)], [0.045, 0.055], "torch", lambda i: P("cloth"), n=5)
    for k, (dx, dy, h, col) in enumerate(((0, 0, 0.30, "flame3"), (0.03, 0.02, 0.22, "flame"), (-0.03, -0.02, 0.24, "flame2"),
                                         (0.0, 0.04, 0.18, "flame"), (0.02, -0.03, 0.16, "flame2"))):
        base = [s.vert((dx + 0.03, dy, 0.40), "torch"), s.vert((dx, dy + 0.03, 0.40), "torch"), s.vert((dx - 0.03, dy, 0.40), "torch")]
        s.tent(base, (dx, dy, 0.40 + h), P(col), "torch")
    return HB.finish(s, "torch", [{"name": "torch", "parent": None, "head": [0, 0, -0.22], "tail": [0, 0, 0.60]}])


def build_haxe():
    s = Skin()
    P = PAL.__getitem__
    hr = HB.tube(s, [(0, 0, -0.24), (0, 0, 0.0), (0, 0, 0.40), (0, 0, 0.74)], [0.024, 0.022, 0.022, 0.026], "haxe",
                 lambda i: P("wood") if i != 1 else P("wood_dk"), n=5)
    s.cap(hr[0], P("wood_dk"), bone="haxe")
    plate_xz(s, [(0.58, 0.02), (0.84, 0.03), (0.92, -0.22), (0.70, -0.20), (0.60, -0.06)], 0.02, P("metal"), P("metal_dk"), "haxe")
    plate_xz(s, [(0.70, 0.02), (0.76, 0.05), (0.76, 0.0)], 0.02, P("metal_dk"), P("metal_dk"), "haxe")     # the back spike
    return HB.finish(s, "haxe", [{"name": "haxe", "parent": None, "head": [0, 0, -0.24], "tail": [0, 0, 0.90]}])


# ---------------------------------------------------------------- the dog (a quadruped, its own skeleton)

DOG_BONES = [
    {"name": "hips", "parent": None, "head": [0, 0.74, -0.40], "tail": [0, 0.75, -0.12]},
    {"name": "spine", "parent": "hips", "head": [0, 0.75, -0.12], "tail": [0, 0.77, 0.18]},
    {"name": "chest", "parent": "spine", "head": [0, 0.77, 0.18], "tail": [0, 0.80, 0.42]},
    {"name": "neck", "parent": "chest", "head": [0, 0.80, 0.42], "tail": [0, 0.90, 0.64]},
    {"name": "head", "parent": "neck", "head": [0, 0.90, 0.64], "tail": [0, 0.84, 1.00]},
    {"name": "jaw", "parent": "head", "head": [0, 0.82, 0.72], "tail": [0, 0.78, 1.00]},
    {"name": "tail1", "parent": "hips", "head": [0, 0.76, -0.52], "tail": [0, 0.70, -0.76]},
    {"name": "tail2", "parent": "tail1", "head": [0, 0.70, -0.76], "tail": [0, 0.55, -0.98]},
    {"name": "fupper.{S}", "parent": "chest", "mirror": True, "head": [0.12, 0.72, 0.30], "tail": [0.13, 0.42, 0.28]},
    {"name": "ffore.{S}", "parent": "fupper.{S}", "mirror": True, "head": [0.13, 0.42, 0.28], "tail": [0.13, 0.12, 0.30]},
    {"name": "fpaw.{S}", "parent": "ffore.{S}", "mirror": True, "head": [0.13, 0.12, 0.30], "tail": [0.13, 0.02, 0.40]},
    {"name": "hthigh.{S}", "parent": "hips", "mirror": True, "head": [0.12, 0.70, -0.30], "tail": [0.14, 0.44, -0.38]},
    {"name": "hshin.{S}", "parent": "hthigh.{S}", "mirror": True, "head": [0.14, 0.44, -0.38], "tail": [0.12, 0.14, -0.46]},
    {"name": "hfoot.{S}", "parent": "hshin.{S}", "mirror": True, "head": [0.12, 0.14, -0.46], "tail": [0.12, 0.02, -0.34]},
]


def tube_b(s, pts, radii, bones, colfn, n=8, ref=(0, 1, 0)):
    """a loft along pts with a bone for each ring; radii (rx, ry) or r"""
    prev = None
    out = []
    for i, p in enumerate(pts):
        t = unit(sub(pts[min(i + 1, len(pts) - 1)], pts[max(i - 1, 0)]))
        r = radii[i]
        rx, ry = (r, r) if not isinstance(r, tuple) else r
        u = unit(sub(ref, mul(t, dot(ref, t))))
        v = cross(t, u)
        ring = [add(p, add(mul(u, ry * math.cos(2 * math.pi * k / n)), mul(v, rx * math.sin(2 * math.pi * k / n)))) for k in range(n)]
        ids = s.ring(align(s, prev, ring) if prev else ring, bones[i])
        if prev:
            s.bridge(prev, ids, colfn(i - 1))
        prev = ids
        out.append(ids)
    return out


def build_dog():
    s = Skin()
    P = PAL.__getitem__
    fur = lambda i: (P("fur"), P("fur_dk"), P("fur_lt"), P("fur"))[i % 4]                   # noqa: E731
    # the body, the neck and the head in one loft along the back
    spine = [(0, 0.74, -0.54), (0, 0.745, -0.40), (0, 0.75, -0.24), (0, 0.76, -0.04), (0, 0.78, 0.18), (0, 0.80, 0.38),
             (0, 0.86, 0.55), (0, 0.92, 0.68), (0, 0.90, 0.80), (0, 0.86, 0.92), (0, 0.83, 1.04), (0, 0.82, 1.10)]
    rad = [(0.07, 0.10), (0.12, 0.17), (0.14, 0.19), (0.12, 0.17), (0.15, 0.22), (0.14, 0.21), (0.10, 0.14), (0.085, 0.11),
           (0.095, 0.11), (0.065, 0.075), (0.045, 0.05), (0.025, 0.03)]
    bones = ["hips", "hips", "hips", "spine", "spine", "chest", "chest", "neck", "head", "head", "head", "head"]
    body = tube_b(s, spine, rad, bones, fur, n=8)
    s.cap(body[0], P("fur_dk"), bone="hips")
    s.cap(body[-1], P("claw"), bone="head")                                                # the nose
    # the lower jaw: its own bone, a thin wedge under the snout
    jaw = tube_b(s, [(0, 0.80, 0.76), (0, 0.775, 0.90), (0, 0.775, 1.02)], [(0.05, 0.03), (0.04, 0.025), (0.025, 0.016)],
                 ["jaw", "jaw", "jaw"], lambda i: P("tongue") if i else P("fur_dk"), n=6)
    s.cap(jaw[-1], P("fur_dk"), bone="jaw")
    for k in range(5):                                                                     # teeth along the jaw
        z = 0.84 + 0.045 * k
        for sx in (-1, 1):
            b = [s.vert((sx * 0.032, 0.80, z), "jaw"), s.vert((sx * 0.032, 0.80, z + 0.025), "jaw"), s.vert((sx * 0.024, 0.80, z + 0.012), "jaw")]
            s.tent(b, (sx * 0.03, 0.835, z + 0.013), P("teeth"), "jaw")
    for sx in (-1, 1):                                                                     # ears and red eyes
        eb = [s.vert((sx * 0.07, 0.99, 0.66), "head"), s.vert((sx * 0.10, 0.99, 0.70), "head"), s.vert((sx * 0.08, 0.99, 0.74), "head")]
        s.tent(eb, (sx * 0.12, 1.08, 0.62), P("fur_dk"), "head")
        ec = (sx * 0.07, 0.945, 0.93)
        e = [s.vert((ec[0], ec[1] - 0.014, ec[2]), "head"), s.vert((ec[0] + sx * 0.012, ec[1], ec[2]), "head"),
             s.vert((ec[0], ec[1] + 0.014, ec[2]), "head")]
        s.tent(e, (ec[0] + sx * 0.03, ec[1], ec[2] + 0.01), P("eye_r"), "head")
    # shaggy fur on the back: tents on rings 1..4 of the body
    for i in range(1, 6):
        lo, hi = body[i], body[i + 1]
        for k in (0, 1, 2, 6, 7):
            base = [lo[k], lo[(k + 1) % 8], hi[(k + 1) % 8], hi[k]]
            c = s.centre(base)
            s.tent(base, (c[0] * 1.1, c[1] - 0.01 + 0.06, c[2] - 0.05), P("fur_lt") if (i + k) % 2 else P("fur_dk"), bones[i])
    # the tail
    tail = tube_b(s, [(0, 0.75, -0.52), (0, 0.72, -0.66), (0, 0.66, -0.80), (0, 0.55, -0.98)], [0.05, 0.04, 0.03, 0.012],
                  ["tail1", "tail1", "tail2", "tail2"], fur, n=5)
    # legs: front (shoulder, elbow, wrist, paw), hind (hip, knee, hock, paw)
    for sx, side in ((1, "L"), (-1, "R")):
        f = tube_b(s, [(sx * 0.12, 0.72, 0.30), (sx * 0.13, 0.50, 0.29), (sx * 0.13, 0.30, 0.29), (sx * 0.13, 0.12, 0.30), (sx * 0.13, 0.04, 0.36)],
                   [0.08, 0.06, 0.045, 0.04, 0.05], ["fupper." + side, "fupper." + side, "ffore." + side, "ffore." + side, "fpaw." + side],
                   lambda i: P("fur") if i < 2 else P("fur_dk"), n=6)
        h = tube_b(s, [(sx * 0.12, 0.72, -0.28), (sx * 0.14, 0.54, -0.36), (sx * 0.13, 0.34, -0.42), (sx * 0.12, 0.16, -0.46), (sx * 0.12, 0.04, -0.38)],
                   [0.095, 0.07, 0.05, 0.04, 0.05], ["hthigh." + side, "hthigh." + side, "hshin." + side, "hshin." + side, "hfoot." + side],
                   lambda i: P("fur") if i < 2 else P("fur_dk"), n=6)
        for leg, bone, z in ((f, "fpaw." + side, 0.40), (h, "hfoot." + side, -0.34)):
            s.cap(leg[-1], P("claw"), bone=bone)
            last = leg[-1]
            for k in (0, 2, 4):                                                            # claws
                a = s.v[last[k]]
                b = s.v[last[(k + 1) % 6]]
                c = ((a[0] + b[0]) / 2, 0.012, (a[2] + b[2]) / 2 + (0.05 if z > 0 else 0.05))
                s.tri(last[k], last[(k + 1) % 6], s.vert(c, bone), P("claw"), (a[0], 0.2, a[2]))
    return HB.finish(s, "dog", DOG_BONES)


# ---------------------------------------------------------------- all

def build_all():
    models, rigs = [], []
    for name, spec_fn in (("villager", villager), ("huntsman", huntsman)):
        s = biped(spec_fn())
        m, r = HB.finish(s, name, HB.BONES)
        models.append(m)
        rigs.append(r)
    for fn in (build_cleaver, build_torch, build_haxe):
        m, r = fn()
        models.append(m)
        rigs.append(r)
    m, r = build_dog()
    models.append(m)
    rigs.append(r)
    return models, rigs


def main(argv):
    models, rigs = build_all()
    for m in models:
        print(f"{m.name}: {len(m.verts)} verts, {len(m.faces)} tris", file=sys.stderr)
    if len(argv) > 1:
        export.export_models(argv[1], M.Mesh(models), anim=A.Anim(rigs), title="Enemies")
        print("wrote", argv[1])


if __name__ == "__main__":
    main(sys.argv)
