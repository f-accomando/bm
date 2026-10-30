"""hunter_build: the Hunter (Bloodborne fan study), high poly, rigged, with separate weapons.

Four models in one BMRES file (`art/models/hunter.bmm`):
  hunter   one continuous flat-shaded skin in the T pose, 24 bones (one bone per vertex)
  saw_c    Saw Cleaver, closed form (short curved handle, blade folded forward)
  saw_o    Saw Cleaver, open form (long handle, long blade)
  pistol   Hunter pistol (barrel, lock, wooden grip, hammer bone)

The weapons are NOT part of the body skin. Their local frame: origin at the grip, tip toward -z, up +y.
To hold one: draw it with the matrix of the hand bone times GRIP (see `GRIP` below).

Run:  python3 hunter_build.py OUT.bm      (then build.py adds the clips and exports the .bmm)
Built from bm-forge's charkit Skin (shared vertices, faces turned away from a point inside the part).
Faces -z, .L on +x, like bm Studio's recipes (built facing +z and mirrored at the end).
"""
import math
import os
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SKILL = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".claude", "skills", "bm-forge"))
sys.path.insert(0, SKILL)
sys.path.insert(0, os.path.join(SKILL, "examples", "charkit"))

from charkit import Skin, align, frame_ring, unit, sub, add, mul, cross, dot, FLAT  # noqa: E402
from bm_core import anim as A, export, mesh as M, skelgen  # noqa: E402

LO = os.environ.get("HUNTER_LOD", "hi") == "lo"     # "lo": about 1000 triangles for the body, same structure
DT = 0 if LO else 1                                     # smoothing passes (a ring between every ring pair)
N = 12 if LO else 32          # sectors of the body rings: 0 = +x (.L side), 8 = front (+z while building), 16 = -x, 24 = back
LM = N // 2     # sides of a limb ring (arm, leg): half of N, so a leg ring fits the pelvis half
FRONT, BACK = N // 4, 3 * N // 4
SC = N / 32.0

PAL = {k: int(v.lstrip("#"), 16) for k, v in {
    "coat": "#3f3b36", "coat2": "#524c44", "coat_dk": "#2b2825", "lining": "#211b1d", "hem": "#5a1c1c",
    "vest": "#5b3529", "vest_lt": "#7b4c37", "strap": "#8d5f43", "belt": "#2f1f1a",
    "gold": "#cfa94e", "gold_dk": "#8f6f2c", "pants": "#3a362e", "boot": "#50291f", "boot_lt": "#744532",
    "boot_dk": "#2c1713", "glove": "#2b211d", "skin": "#b38d76", "skin_sh": "#8a6554", "hair": "#2a2220",
    "mask": "#2c2f31", "mask_lt": "#3e4346", "cloth": "#b9aa94", "eye": "#1a1718", "eye_w": "#cfc6b8",
    "hat": "#4b4642", "hat_lt": "#6d655c", "hat_dk": "#2f2c2a", "blood": "#4a1e1e",
    "steel": "#80878c", "steel_dk": "#4d5256", "iron": "#2d2f33", "wood": "#6c4524", "wood_dk": "#3e2713",
    "brass": "#b8923c", "band": "#8f7b5a", "band_dk": "#3b3128", "cord": "#a38458",
}.items()}


def hsh(*a):
    return (zlib.crc32(repr(a).encode()) & 0xFFFFFF) / float(0xFFFFFF)


def shade(col, k):
    r, g, b = (col >> 16) & 255, (col >> 8) & 255, col & 255
    return (max(0, min(255, int(r * k))) << 16) | (max(0, min(255, int(g * k))) << 8) | max(0, min(255, int(b * k)))


# ------------------------------------------------------------------ profiles

def cr(p0, p1, p2, p3, t=0.5):
    return 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t ** 3)


def densify(rows, keys, times=1):
    """insert a smooth (Catmull-Rom) ring between every pair of rows; a midpoint keeps the proximal ring's bone and colours"""
    for _ in range(times):
        out = []
        for i, r in enumerate(rows):
            out.append(r)
            if i + 1 < len(rows):
                a, b = rows[max(0, i - 1)], rows[min(len(rows) - 1, i + 2)]
                n = dict(r)
                n.pop("tag", None)
                for k in keys:
                    n[k] = cr(a.get(k, 0.0), r.get(k, 0.0), rows[i + 1].get(k, 0.0), b.get(k, 0.0))
                out.append(n)
        rows = out
    return rows


def pt(row, k, n=N):
    a = 2 * math.pi * k / n
    z = row.get("cz", 0.0) + row["rz"] * math.sin(a) + row.get("fz", 0.0) * max(0.0, math.sin(a)) ** 4
    return (row.get("cx", 0.0) + row["rx"] * math.cos(a), row["y"], z)


def ring_pts(row, n=N, skip=()):
    return [pt(row, k, n) for k in range(n)]


def sector_cols(seg, n=N):
    """colours of the n sectors of a segment: base, then (colour, centre sector, half width) overrides"""
    col = [PAL[seg["base"]]] * n
    for key, centre in (("front", 8), ("back", 24)):
        if key in seg:
            c, hw = seg[key]
            for k in range(n):
                d = min((k - centre * SC) % n, (centre * SC - k) % n)
                if d < max(0.75, hw * SC):
                    col[k] = PAL[c]
    for c, centre, hw in seg.get("over", []):
        for k in range(n):
            d = min((k - centre * SC) % n, (centre * SC - k) % n)
            if d < max(0.75, hw * SC):
                col[k] = PAL[c]
    return col


def lod(rows):
    """at low detail drop the rows marked hi=True"""
    return [r for r in rows if not (LO and r.get("hi"))]


def S(base, **kw):
    d = {"base": base}
    d.update(kw)
    return d


# ------------------------------------------------------------------ small decals (flush, raised a few mm)

def plate(s, c, n, up, w, h, rise, col, col_in, bone, inside, frame=0.5):
    """a raised rectangular frame (buckle) on a surface with normal n: outer w x h, hole `frame` times smaller"""
    n = unit(n)
    u = unit(cross(up, n))
    v = cross(n, u)
    def rect(sw, sh, off):
        return [add(add(c, mul(n, off)), add(mul(u, a * sw), mul(v, b * sh))) for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    ob = s.ring(rect(w, h, 0.002), bone)
    ot = s.ring(rect(w, h, rise), bone)
    it = s.ring(rect(w * frame, h * frame, rise), bone)
    ib = s.ring(rect(w * frame, h * frame, 0.002), bone)
    s.bridge(ob, ot, col)
    s.bridge(ot, it, col)
    s.bridge(it, ib, col_in)
    s.cap(ib, col_in, inside=inside)


def knob(s, c, n, r, h, col, bone, sides=8):
    n = unit(n)
    ring = s.ring(frame_ring(add(c, mul(n, 0.002)), n, r, sides, ref=(0, 1, 0)), bone)
    s.cap(ring, col, apex=add(c, mul(n, h)), bone=bone, inside=sub(c, mul(n, 0.05)))


def octa(s, c, r, col, bone, sx=1.0, sy=1.0, sz=1.0):
    ids = [s.vert(add(c, (a * r * sx, b * r * sy, d * r * sz)), bone) for a, b, d in
           ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1))]
    for a, b, d in ((0, 2, 4), (2, 1, 4), (1, 3, 4), (3, 0, 4), (2, 0, 5), (1, 2, 5), (3, 1, 5), (0, 3, 5)):
        s.tri(ids[a], ids[b], ids[d], col, c)


# ------------------------------------------------------------------ rig (build space: facing +z, mirrored at the end)

BONES = [
    {"name": "hips", "parent": None, "head": [0, 0.93, 0], "tail": [0, 1.05, 0]},
    {"name": "spine", "parent": "hips", "head": [0, 1.05, 0], "tail": [0, 1.30, 0]},
    {"name": "chest", "parent": "spine", "head": [0, 1.30, 0], "tail": [0, 1.55, 0]},
    {"name": "neck", "parent": "chest", "head": [0, 1.55, 0], "tail": [0, 1.68, 0]},
    {"name": "head", "parent": "neck", "head": [0, 1.68, 0], "tail": [0, 1.95, 0]},
    {"name": "upper.{S}", "parent": "chest", "mirror": True, "head": [0.20, 1.48, 0], "tail": [0.48, 1.48, 0]},
    {"name": "fore.{S}", "parent": "upper.{S}", "mirror": True, "head": [0.48, 1.48, 0], "tail": [0.74, 1.48, 0]},
    {"name": "hand.{S}", "parent": "fore.{S}", "mirror": True, "head": [0.74, 1.48, 0], "tail": [0.88, 1.48, 0]},
    {"name": "fing.{S}", "parent": "hand.{S}", "mirror": True, "head": [0.88, 1.48, 0], "tail": [1.02, 1.48, 0]},
    {"name": "thumb.{S}", "parent": "hand.{S}", "mirror": True, "head": [0.80, 1.48, 0.03], "tail": [0.90, 1.48, 0.10]},
    {"name": "thigh.{S}", "parent": "hips", "mirror": True, "head": [0.095, 0.92, 0], "tail": [0.095, 0.52, 0]},
    {"name": "shin.{S}", "parent": "thigh.{S}", "mirror": True, "head": [0.095, 0.52, 0], "tail": [0.095, 0.09, 0]},
    {"name": "foot.{S}", "parent": "shin.{S}", "mirror": True, "head": [0.095, 0.09, 0], "tail": [0.095, 0.03, 0.17]},
    {"name": "skirt.{S}", "parent": "hips", "mirror": True, "head": [0.10, 1.04, 0], "tail": [0.20, 0.50, 0]},
    {"name": "skirt_b", "parent": "hips", "head": [0, 1.04, -0.10], "tail": [0, 0.46, -0.27]},
]


def expand_bones(bones, model):
    """mirror the {S} bones (.L +x / .R -x) and flip z (build facing +z -> final facing -z); -> skelgen Rig"""
    out = []
    for b in bones:
        for side, sx in (("L", 1), ("R", -1)) if b.get("mirror") else ((None, 1),):
            nm = lambda t: t.replace("{S}", side) if side else t   # noqa: E731
            d = {"name": nm(b["name"]), "parent": nm(b["parent"]) if b["parent"] else None}
            for key in ("head", "tail"):
                if key in b:
                    x, y, z = b[key]
                    d[key] = [x * sx, y, -z]
            out.append(d)
    return skelgen.skeleton({"model": model, "bones": out})


def finish(s, name, bones):
    """mirror z, turn faces back, shade each face a little (hand-made look), bind one bone per vertex"""
    s.v = [(x, y, -z) for x, y, z in s.v]
    f = []
    for i, (a, b, c, col) in enumerate(s.f):
        f.append((a, c, b, shade(col, 0.93 + 0.14 * hsh(name, i))))
    rig = expand_bones(bones, name)
    bidx = {b.name: i for i, b in enumerate(rig.bones)}
    model = M.Model(name, [list(v) for v in s.v], [M.Face(a, b, c, col | FLAT) for a, b, c, col in f], 0)
    rig = skelgen.bind(model, rig, vbones=[bidx[b] for b in s.bone])
    return model, rig


def yz_ring(x, cy, ry, rz, n=LM, cz=0.0):
    return [(x, cy + ry * math.cos(2 * math.pi * k / n), cz + rz * math.sin(2 * math.pi * k / n)) for k in range(n)]


def xz_ring(y, cx, rx, rz, n=LM, cz=0.0):
    return [(cx + rx * math.cos(2 * math.pi * k / n), y, cz + rz * math.sin(2 * math.pi * k / n)) for k in range(n)]


# ------------------------------------------------------------------ the hunter

def build_hunter():
    s = Skin()
    P = PAL.__getitem__

    # ---- column: torso, collar, neck, head
    torso = [
        dict(y=0.93, rx=0.178, rz=0.118, bone="hips", seg=S("pants", back=("coat", 9)), tag="pelvis"),
        dict(y=1.02, rx=0.168, rz=0.112, bone="hips", seg=S("belt", front=("belt", 3)), tag="belt0"),
        dict(y=1.10, rx=0.160, rz=0.106, bone="spine", seg=S("coat", front=("vest", 7), over=[("gold_dk", 8, 1)]), tag="belt1"),
        dict(y=1.20, rx=0.164, rz=0.108, bone="spine", seg=S("coat", front=("vest", 7)), hi=True),
        dict(y=1.30, rx=0.178, rz=0.118, bone="chest", seg=S("coat", front=("vest", 7)), hi=True),
        dict(y=1.40, rx=0.194, rz=0.126, bone="chest", seg=S("coat", front=("vest", 6)), tag="a1"),
        dict(y=1.48, rx=0.206, rz=0.130, bone="chest", seg=S("coat", front=("vest", 5)), tag="a2", hi=True),
        dict(y=1.54, rx=0.212, rz=0.125, bone="chest", seg=S("coat", front=("cloth", 3)), tag="a3"),
    ]
    torso = densify(lod(torso), ("y", "rx", "rz"), DT)
    collar = [
        dict(y=1.575, rx=0.168, rz=0.128, bone="chest", seg=S("coat"), tag="collar0"),
        dict(y=1.655, rx=0.180, rz=0.138, bone="chest", seg=S("coat2")),
        dict(y=1.645, rx=0.102, rz=0.094, bone="neck", seg=S("lining")),
        dict(y=1.600, rx=0.072, rz=0.074, bone="neck", seg=S("mask")),
    ]
    head = [
        dict(y=1.680, rx=0.066, rz=0.070, bone="head", seg=S("mask"), tag="chin"),
        dict(y=1.715, rx=0.082, rz=0.100, bone="head", seg=S("mask", over=[("mask_lt", 8, 5)]), fz=0.012, hi=True),
        dict(y=1.765, rx=0.094, rz=0.115, bone="head", seg=S("mask", over=[("mask_lt", 8, 4)]), fz=0.022, tag="mouth"),
        dict(y=1.815, rx=0.100, rz=0.120, bone="head", seg=S("skin", back=("hair", 12), over=[("hair", 0, 9), ("hair", 16, 9)]), fz=0.016, tag="eyes0"),
        dict(y=1.865, rx=0.103, rz=0.123, bone="head", seg=S("hat"), tag="hatbase"),
        dict(y=1.915, rx=0.094, rz=0.110, bone="head", seg=S("hat"), hi=True),
        dict(y=1.955, rx=0.066, rz=0.082, bone="head", seg=S("hat"), tag="top"),
    ]
    head = densify(lod(head), ("y", "rx", "rz", "fz"), DT)
    rows = torso + collar + head
    R, tags = [], {}
    for i, r in enumerate(rows):
        R.append(s.ring(ring_pts(r), r["bone"]))
        if "tag" in r:
            tags[r["tag"]] = i
    for i in range(len(rows) - 1):
        s.bridge(R[i], R[i + 1], sector_cols(rows[i]["seg"]))
    s.cap(R[-1], P("hat"), apex=(0.0, 1.985, 0.0), bone="head")
    ring = lambda t: R[tags[t]]                                  # noqa: E731
    row = lambda t: rows[tags[t]]                                # noqa: E731

    # ---- hat: tricorne, brim with raised points (sides tall, back and front small), ragged edge
    base = ring("hatbase")
    hb = row("hatbase")
    def hat_dy(k):
        d = lambda c: min((k - c) % N, (c - k) % N)             # noqa: E731
        side = max(0.0, 1 - min(d(0), d(N // 2)) / (2.6 * SC)) ** 1.4
        back = max(0.0, 1 - d(BACK) / (2.5 * SC)) ** 1.3
        front = max(0.0, 1 - d(FRONT) / (2.2 * SC)) ** 1.3
        return 0.215 * side + 0.085 * back + 0.05 * front - 0.012, side, back, front
    def brim(scale, y, lift, extra=0.0):
        pts = []
        for k in range(N):
            dy, sd, bk, fr = hat_dy(k)
            jy = (hsh("brim", k) - 0.5) * 0.012
            sc = scale * (1 + 0.40 * sd * extra + 0.22 * bk * extra + 0.10 * fr * extra + (hsh("brimr", k) - 0.5) * 0.05 * extra)
            x = hb["rx"] * sc * math.cos(2 * math.pi * k / N)
            z = hb["rz"] * sc * math.sin(2 * math.pi * k / N) + (0.03 * fr * extra)
            pts.append((x, y + dy * lift + jy * extra, z))
        return pts
    hbo = s.ring(brim(1.82, 1.880, 1.0, 1.0), "head")
    hd = s.ring(brim(1.32, 1.850, 0.5), "head")
    if LO:
        s.bridge(base, hbo, [P("hat_lt") if hat_dy(k)[1] > 0.3 else P("hat") for k in range(N)])
        s.bridge(hbo, hd, P("hat_dk"))
    else:
        ha = s.ring(brim(1.22, 1.884, 0.55), "head")
        hc = s.ring(brim(1.82, 1.866, 0.97, 1.0), "head")
        s.bridge(base, ha, P("hat"))
        s.bridge(ha, hbo, [P("hat_lt") if hat_dy(k)[1] > 0.3 else P("hat") for k in range(N)])
        s.bridge(hbo, hc, P("hat_lt"))
        s.bridge(hc, hd, P("hat_dk"))
    s.bridge(hd, ring("eyes0"), P("hat_dk"))

    # ---- eyes (dark discs) and brow shadow plates
    ey = row("eyes0")
    for sx in (1, -1):
        a = math.radians(90 - sx * 21)
        c = (sx * 0.0 + ey["rx"] * 1.0 * math.cos(a), 1.842, ey["rz"] * math.sin(a) + ey["fz"] * math.sin(a) ** 4)
        nrm = unit((math.cos(a) * 0.5, 0.0, math.sin(a) + 0.2))
        for dr, h, col in (((0.0075, 0.007, "eye"),) if LO else ((0.0105, 0.004, "eye_w"), (0.0065, 0.007, "eye"))):
            u = unit(cross(nrm, (0, 1, 0)))
            v = cross(nrm, u)
            rp = [add(add(c, mul(nrm, 0.003 + (0.002 if col == "eye" else 0))), add(mul(u, dr * 1.7 * math.cos(2 * math.pi * i / (6 if LO else 8))), mul(v, dr * math.sin(2 * math.pi * i / (6 if LO else 8))))) for i in range(6 if LO else 8)]
            ids = s.ring(rp, "head")
            s.cap(ids, P(col), apex=add(c, mul(nrm, h + (0.002 if col == "eye" else 0))), bone="head")

    # ---- gold clasp rosettes and chain over the chest (octahedra links), chain loop at the mask
    cl = row("collar0")
    for sx in () if LO else (1, -1):
        c = (sx * 0.095, 1.585, 0.118)
        knob(s, c, (sx * 0.25, 0.15, 1.0), 0.018, 0.012, P("gold"), "chest", 10)
    a0, a1 = (0.095, 1.575, 0.128), (-0.095, 1.575, 0.128)
    links = 1 if LO else 11
    for i in range(1, links):
        t = i / links
        p = (a0[0] + (a1[0] - a0[0]) * t, 1.575 - 0.045 * math.sin(math.pi * t), 0.130 + 0.012 * math.sin(math.pi * t))
        octa(s, p, 0.0075, P("gold_dk") if i % 2 else P("gold"), "chest", 1.6 if i % 2 else 1.0, 1.0 if i % 2 else 1.6, 1.0)
    for i in range(0 if LO else 10):
        t = i / 9.0
        ang = math.radians(60 + 60 * t)
        p = (0.075 * math.cos(ang) * 1.0, 1.645 - 0.03 * math.sin(math.pi * t), 0.082 * math.sin(ang) + 0.012)
        octa(s, p, 0.0058, P("gold_dk") if i % 2 else P("gold"), "neck", 1.5, 1.0, 1.0)

    # ---- bandolier strap (raised ribbon) from the left shoulder to the right hip across the chest, plus belt buckle
    def surf(y, k_ang):
        """a point on the torso surface at height y (interpolated rows a..), angle in degrees from +x toward +z"""
        for i in range(len(rows) - 1):
            if rows[i]["y"] <= y <= rows[i + 1]["y"]:
                t = (y - rows[i]["y"]) / (rows[i + 1]["y"] - rows[i]["y"])
                rx = rows[i]["rx"] + (rows[i + 1]["rx"] - rows[i]["rx"]) * t
                rz = rows[i]["rz"] + (rows[i + 1]["rz"] - rows[i]["rz"]) * t
                a = math.radians(k_ang)
                return (rx * math.cos(a), y, rz * math.sin(a)), unit((math.cos(a) / rx, 0, math.sin(a) / rz))
        raise ValueError(y)
    left, right = [], []
    steps = 4 if LO else 16
    for i in range(steps + 1):
        t = i / steps
        y = 1.51 - 0.40 * t
        ang = 72 + (t * 62) * 1.0 + 0.0     # 72 deg (left of the sternum) sweeping across to 134 deg
        ang = 62 + 78 * t
        c, nn = surf(y, ang)
        up = unit((-0.55, 0.60, 0.0))
        side = unit(cross(nn, up))
        for sgn, lst in ((-1, left), (1, right)):
            lst.append(s.vert(add(add(c, mul(nn, 0.007)), mul(unit(cross(nn, (0.3, 1, 0))), sgn * 0.017)), "chest" if y > 1.30 else "spine"))
    for i in range(steps):
        inside = (0.0, 1.30 - 0.3 * (i / steps), 0.0)
        s.quad(left[i], left[i + 1], right[i + 1], right[i], P("strap"), inside)
    for t_, y_ in (() if LO else ((0.18, 1.42), (0.46, 1.30), (0.78, 1.18))):
        ang = 62 + 78 * t_
        c, nn = surf(y_, ang)
        plate(s, add(c, mul(nn, 0.005)), nn, (0, 1, 0), 0.02, 0.016, 0.008, P("gold"), P("strap"), "chest" if y_ > 1.30 else "spine", (0, y_, 0))
    for k_off in () if LO else (0,):
        c, nn = surf(1.06, 90)
        plate(s, add(c, mul(nn, 0.004)), nn, (0, 1, 0), 0.032, 0.020, 0.010, P("gold"), P("belt"), "spine", (0, 1.06, 0))
    for y_ in () if LO else (1.43, 1.32, 1.22):                                     # vest front buttons / rivets
        c, nn = surf(y_, 90 - 11)
        knob(s, add(c, mul(nn, 0.004)), nn, 0.007, 0.005, P("gold_dk"), "chest" if y_ > 1.30 else "spine", 6)

    # ---- capelet: an open arc over the shoulders (front open), ragged hem with a fringe
    gw = N // 24                                                 # half width of the open front, in vertices
    gapc = set(range(FRONT - gw, FRONT + gw + 1))
    cape_rows = [
        dict(y=1.528, rx=0.275, rz=0.168, seg=S("coat2")),
        dict(y=1.470, rx=0.312, rz=0.188, seg=S("coat"), hi=True),
        dict(y=1.405, rx=0.336, rz=0.200, seg=S("coat")),
        dict(y=1.345, rx=0.344, rz=0.205, seg=S("coat_dk")),
    ]
    cape_rows = densify(lod(cape_rows), ("y", "rx", "rz"), DT)
    def arc_ring(r, scale=1.0, dy=0.0, jag=0.0, tag="c", gap=gapc, bone="chest"):
        ids = {}
        for k in range(N):
            if k in gap:
                continue
            x, y, z = pt(r, k)
            j = (hsh(tag, k) - 0.5) * jag
            sc = scale * (1 + (hsh(tag, "r", k) - 0.5) * jag * 1.3)
            ids[k] = s.vert((x * sc, y + dy + j, z * sc), bone if not callable(bone) else bone(k))
        return ids
    def arc_bridge(r0, r1, cols):
        for k in range(N):
            k2 = (k + 1) % N
            if k in r0 and k2 in r0 and k in r1 and k2 in r1:
                c = cols[k] if isinstance(cols, list) else cols
                inside = (0.0, (s.v[r0[k]][1] + s.v[r1[k]][1]) / 2 - 0.05, 0.0)
                s.quad(r0[k], r0[k2], r1[k2], r1[k], c, inside)
    prev = {k: ring("a3")[k] for k in range(N) if k not in gapc}
    cape_ids = []
    for i, r in enumerate(cape_rows):
        last = i == len(cape_rows) - 1
        ids = arc_ring(r, jag=0.03 if last else 0.0, tag="cape%d" % i, gap=gapc)
        arc_bridge(prev, ids, sector_cols(r["seg"]))
        prev = ids
        cape_ids.append(ids)
    hem = prev
    cin = arc_ring(dict(y=1.352, rx=0.344 * 0.955, rz=0.205 * 0.955), tag="capein")
    arc_bridge(hem, cin, P("hem"))
    if not LO:
        cin2 = arc_ring(dict(y=1.45, rx=0.344 * 0.82, rz=0.205 * 0.82), tag="capein2")
        arc_bridge(cin, cin2, P("lining"))
    for k in range(0 if LO else N):                              # fringe: a tent under every hem quad
        k2 = (k + 1) % N
        if k in hem and k2 in hem and k in cin and k2 in cin:
            base = [hem[k], hem[k2], cin[k2], cin[k]]
            c = s.centre(base)
            s.tent(base, (c[0], c[1] - 0.025 - 0.035 * hsh("fr", k), c[2]), P("coat_dk") if hsh("frc", k) > 0.3 else P("hem"), "chest")

    # ---- arms: first ring = a loop of 16 torso vertices (7 columns x rows a1, a2, a3), then limb rings
    ar_rows = [
        dict(x=0.268, ry=0.080, rz=0.076, bone="upper.{S}", seg="coat"),
        dict(x=0.330, ry=0.072, rz=0.068, bone="upper.{S}", seg="coat", hi=True),
        dict(x=0.400, ry=0.066, rz=0.062, bone="upper.{S}", seg="coat2", hi=True),
        dict(x=0.480, ry=0.060, rz=0.057, bone="fore.{S}", seg="coat"),       # elbow (the first ring of the forearm)
        dict(x=0.545, ry=0.066, rz=0.062, bone="fore.{S}", seg="vest_lt"),
        dict(x=0.610, ry=0.063, rz=0.060, bone="fore.{S}", seg="vest_lt", hi=True),
        dict(x=0.665, ry=0.060, rz=0.057, bone="fore.{S}", seg="gold_dk"),
        dict(x=0.700, ry=0.069, rz=0.065, bone="fore.{S}", seg="vest_lt"),    # bracer lip
        dict(x=0.738, ry=0.052, rz=0.048, bone="hand.{S}", seg="glove"),
        dict(x=0.800, ry=0.062, rz=0.036, bone="hand.{S}", seg="glove", tag="palm"),
        dict(x=0.860, ry=0.060, rz=0.033, bone="hand.{S}", seg="glove", tag="knuck"),
        dict(x=0.930, ry=0.054, rz=0.029, bone="fing.{S}", seg="glove"),
        dict(x=0.985, ry=0.042, rz=0.022, bone="fing.{S}", seg="glove", hi=True),
    ]
    ar_rows = lod(ar_rows)
    ai = {r["tag"]: i for i, r in enumerate(ar_rows) if "tag" in r}
    for sx, side in ((1, "L"), (-1, "R")):
        ncol = LM // 2 - 1 if not LO else LM // 2                              # columns of the shoulder patch
        cols = tuple(range(-(ncol // 2), ncol // 2 + 1))
        kk = lambda c: (c if sx > 0 else N // 2 - c) % N                      # noqa: E731
        if LO:                                                                # two rows (a1, a3): the loop is 2 x ncol
            loop = [R[tags["a1"]][kk(c)] for c in cols] + [R[tags["a3"]][kk(c)] for c in reversed(cols)]
        else:                                                                 # three rows (a1, a2, a3): 2 x ncol + 2
            loop = [R[tags["a1"]][kk(c)] for c in cols] + [R[tags["a2"]][kk(cols[-1])]] \
                + [R[tags["a3"]][kk(c)] for c in reversed(cols)] + [R[tags["a2"]][kk(cols[0])]]
        prev = loop
        rings = []
        for r in ar_rows:
            bone = r["bone"].replace("{S}", side)
            pts = yz_ring(sx * r["x"], 1.48, r["ry"], r["rz"])
            ids = s.ring(align(s, prev, pts), bone)
            col = P(r["seg"])
            s.bridge(prev, ids, col)
            prev = ids
            rings.append(ids)
        tipbone = "fing." + side
        s.cap(rings[-1], P("glove"), apex=(sx * 1.015, 1.48, 0.0), bone=tipbone)
        # thumb: a 4-sided loft grown from a quad of the palm, on the front
        palm, knuck = rings[ai["palm"]], rings[ai["knuck"]]
        k0 = 3 if sx > 0 else 3
        base = [palm[k0], palm[k0 + 1], knuck[k0 + 1], knuck[k0]]
        c = s.centre(base)
        tb = "thumb." + side
        if LO:                                                                # low detail: one pyramid on the palm quad
            s.tent(base, (c[0] + sx * 0.05, c[1] - 0.006, c[2] + 0.085), P("glove"), tb)
            continue
        t1 = s.ring(align(s, base, frame_ring((c[0] + sx * 0.02, c[1] - 0.004, c[2] + 0.034), (sx * 0.4, 0, 1.0), 0.016, 4, ref=(0, 1, 0))), tb)
        s.bridge(base, t1, P("glove"))
        t2 = s.ring(align(s, t1, frame_ring((c[0] + sx * 0.04, c[1] - 0.006, c[2] + 0.066), (sx * 0.5, 0, 1.0), 0.012, 4, ref=(0, 1, 0))), tb)
        s.bridge(t1, t2, P("glove"))
        s.cap(t2, P("glove"), apex=(c[0] + sx * 0.052, c[1] - 0.007, c[2] + 0.088), bone=tb)
        # bracer gold studs
        for xx in () if LO else (0.575, 0.640):
            for ang in (0, 120, 240):
                a_ = math.radians(ang + 20)
                cc = (sx * xx, 1.48 + 0.066 * math.cos(a_), 0.062 * math.sin(a_))
                nn = unit((0, math.cos(a_), math.sin(a_)))
                knob(s, cc, nn, 0.0065, 0.005, P("gold"), "fore." + side, 6)

    # ---- legs: first ring = the 16 pelvis vertices of that half (an open arc closed by a chord)
    arcs = {1: list(range(BACK, N)) + list(range(0, FRONT)), -1: list(range(FRONT, BACK))}
    leg_rows = [
        dict(y=0.800, rx=0.094, rz=0.098, bone="thigh.{S}", seg="pants"),
        dict(y=0.700, rx=0.087, rz=0.092, bone="thigh.{S}", seg="pants", hi=True),
        dict(y=0.610, rx=0.079, rz=0.083, bone="thigh.{S}", seg="pants", hi=True),
        dict(y=0.520, rx=0.070, rz=0.074, bone="shin.{S}", seg="pants"),
        dict(y=0.496, rx=0.082, rz=0.086, bone="shin.{S}", seg="pants", hi=True),
        dict(y=0.472, rx=0.090, rz=0.094, bone="shin.{S}", seg="boot_lt"),
        dict(y=0.440, rx=0.084, rz=0.088, bone="shin.{S}", seg="boot", hi=True),
        dict(y=0.360, rx=0.071, rz=0.075, bone="shin.{S}", seg="boot", hi=True),
        dict(y=0.285, rx=0.063, rz=0.068, bone="shin.{S}", seg="boot_lt", hi=True),
        dict(y=0.215, rx=0.057, rz=0.064, bone="shin.{S}", seg="boot", hi=True),
        dict(y=0.150, rx=0.056, rz=0.064, bone="shin.{S}", seg="boot_lt", hi=True),
        dict(y=0.105, rx=0.060, rz=0.068, bone="shin.{S}", seg="boot", hi=True),
        dict(y=0.088, rx=0.060, rz=0.070, bone="foot.{S}", seg="boot", cz=0.005),
        dict(y=0.052, rx=0.068, rz=0.104, bone="foot.{S}", seg="boot", cz=0.040),
        dict(y=0.022, rx=0.070, rz=0.138, bone="foot.{S}", seg="boot_dk", cz=0.074),
        dict(y=0.004, rx=0.069, rz=0.140, bone="foot.{S}", seg="iron", cz=0.075),
    ]
    leg_rows = lod(leg_rows)
    for sx, side in ((1, "L"), (-1, "R")):
        prev = [R[0][k] for k in arcs[sx]]
        for i, r in enumerate(leg_rows):
            bone = r["bone"].replace("{S}", side)
            ids = s.ring(align(s, prev, xz_ring(r["y"], sx * 0.095, r["rx"], r["rz"], LM, r.get("cz", 0.0) + 0.01)), bone)
            s.bridge(prev, ids, P(leg_rows[i - 1]["seg"]) if i else P("pants"))
            prev = ids
            if i == len(leg_rows) - 1:
                s.cap(ids, P("iron"), inside=(sx * 0.095, 0.2, 0.08))
        # boot cuff buckle straps
        for y_ in (0.41, 0.17):
            for a_ in (math.radians(8), math.radians(172)):
                pass
        cc = (sx * 0.095 + 0.0, 0.40, 0.09)
        if not LO:
            plate(s, (sx * 0.095, 0.385, 0.088 + 0.0), (0, 0.15, 1), (0, 1, 0), 0.026, 0.012, 0.007, P("gold"), P("boot_dk"), "shin." + side, (sx * 0.095, 0.385, 0))
        if not LO:
            plate(s, (sx * 0.095, 0.175, 0.070), (0, 0.15, 1), (0, 1, 0), 0.020, 0.010, 0.007, P("gold"), P("boot_dk"), "shin." + side, (sx * 0.095, 0.175, 0))
        # toe cap
    s.quad(R[0][BACK], R[0][FRONT - 1], R[0][FRONT], R[0][BACK - 1], P("pants"), (0.0, 1.0, 0.0))

    # ---- coat skirt: an open arc below the belt (front open), three bones, ragged hem with a fringe
    sk_gap = {FRONT}
    def skirt_bone(k):
        if min((k - BACK) % N, (BACK - k) % N) <= max(1, N // 8):
            return "skirt_b"
        return "skirt.L" if math.cos(2 * math.pi * k / N) > 0 else "skirt.R"
    sk_rows = [
        dict(y=1.080, rx=0.182, rz=0.128, seg=S("coat_dk")),
        dict(y=0.985, rx=0.207, rz=0.152, seg=S("coat"), hi=True),
        dict(y=0.870, rx=0.234, rz=0.178, seg=S("coat"), hi=True),
        dict(y=0.750, rx=0.258, rz=0.198, seg=S("coat2", back=("coat", 14)), hi=True),
        dict(y=0.630, rx=0.280, rz=0.212, seg=S("coat")),
        dict(y=0.515, rx=0.298, rz=0.224, seg=S("coat_dk")),
    ]
    sk_rows = densify(lod(sk_rows), ("y", "rx", "rz"), DT)
    prev = {k: R[tags["belt1"]][k] for k in range(N) if k not in sk_gap}
    for i, r in enumerate(sk_rows):
        last = i == len(sk_rows) - 1
        def back_drop(k):
            return -0.05 * max(0.0, 1 - min((k - BACK) % N, (BACK - k) % N) / (9.0 * SC)) if last else 0.0
        ids = {}
        for k in range(N):
            if k in sk_gap:
                continue
            x, y, z = pt(r, k)
            jy = (hsh("skh", k) - 0.5) * 0.045 if last else 0.0
            jr = 1 + (hsh("skr", k) - 0.5) * 0.06 if last else 1.0
            ids[k] = s.vert((x * jr, y + jy + back_drop(k), z * jr), skirt_bone(k) if i else "hips")
        arc_bridge(prev, ids, sector_cols(r["seg"]))
        prev = ids
    hem = prev
    yy = sk_rows[-1]["y"]
    sin_ = {}
    for k in hem:
        x, y, z = s.v[hem[k]]
        sin_[k] = s.vert((x * 0.955, y + 0.012, z * 0.955), skirt_bone(k))
    arc_bridge(hem, sin_, P("hem"))
    sin2 = {}
    for k in () if LO else hem:
        x, y, z = s.v[hem[k]]
        sin2[k] = s.vert((x * 0.86, y + 0.20, z * 0.86), skirt_bone(k))
    if sin2:
        arc_bridge(sin_, sin2, P("lining"))
    for k in range(0 if LO else N):
        k2 = (k + 1) % N
        if k in hem and k2 in hem:
            base = [hem[k], hem[k2], sin_[k2], sin_[k]]
            c = s.centre(base)
            s.tent(base, (c[0], c[1] - 0.03 - 0.04 * hsh("sfr", k), c[2]), P("coat_dk") if hsh("sfc", k) > 0.25 else P("hem"), skirt_bone(k))
    # blood stains on the hem band and the right sleeve: recolour some faces already built
    return s


# ------------------------------------------------------------------ weapons

def tube(s, pts, radii, bone, colfn, n=8, ref=(0, 1, 0), flat=None):
    """a loft along path pts; radii: list of r or (rx, ry) per point; colfn(i) -> colour of the ring pair i"""
    ids_prev = None
    rings = []
    for i, p in enumerate(pts):
        t = unit(sub(pts[min(i + 1, len(pts) - 1)], pts[max(i - 1, 0)]))
        r = radii[i]
        rx, ry = (r, r) if not isinstance(r, tuple) else r
        base = frame_ring(p, t, 1.0, n, ref=ref)
        pts_i = [add(p, (sub(q, p)[0] * rx, sub(q, p)[1] * ry, sub(q, p)[2] * rx)) if False else add(p, mul(sub(q, p), rx)) for q in base]
        if ry != rx:
            u = unit(sub(ref, mul(t, dot(ref, t))))
            v = cross(t, u)
            pts_i = [add(p, add(mul(u, ry * math.cos(2 * math.pi * k / n)), mul(v, rx * math.sin(2 * math.pi * k / n)))) for k in range(n)]
        ids = s.ring(align(s, ids_prev, pts_i) if ids_prev else pts_i, bone)
        if ids_prev:
            s.bridge(ids_prev, ids, colfn(i - 1))
        ids_prev = ids
        rings.append(ids)
    return rings


def build_saw(open_form):
    """Saw Cleaver. closed: short curved handle, blade pointing forward-down; open: long straight handle and blade.
    Local frame while building: origin = grip centre, tip toward +z (mirrored to -z at the end), blade teeth down."""
    s = Skin()
    P = PAL.__getitem__
    H0, H1 = (-0.26, 0.32) if open_form else (-0.20, 0.22)         # handle from the pommel to the hinge
    nh = (8 if LO else 14) if open_form else (7 if LO else 12)   # enough rings for the S
    hp, hr = [], []
    for i in range(nh):
        t = i / (nh - 1)
        z = H0 + (H1 - H0) * t
        y = (0.085 if open_form else 0.060) * math.sin(2 * math.pi * t)   # the handle is an S (reference picture 5)
        hp.append((0.0, y, z))
        hr.append(0.017 + (0.010 if t < 0.05 else 0.0) + (0.007 if i in (nh // 2,) else 0.0))
    hr[0] = 0.012
    hr[1] = 0.027
    hr[2] = 0.019
    rings = tube(s, hp, hr, "saw", lambda i: P("band") if i % 2 else P("band_dk"), n=5 if LO else 6)
    s.cap(rings[0], P("steel_dk"), bone="saw")
    # hinge block (a cylinder across x) at the end of the handle
    hz = H1 + 0.012
    hy = hp[-1][1]
    hing = tube(s, [(-0.040, hy, hz), (-0.026, hy, hz), (0.026, hy, hz), (0.040, hy, hz)], [0.020, 0.034, 0.034, 0.020], "saw",
                lambda i: P("iron") if i != 1 else P("steel_dk"), n=5 if LO else 8, ref=(0, 1, 0))
    s.cap(hing[0], P("iron"), bone="saw")
    s.cap(hing[-1], P("iron"), bone="saw")
    # blade: the profile of the reference picture. A wide wedge, narrow at the hinge, widest about 40% along,
    # blunt at the far end, with a row of pointed teeth on one edge (down, -y) that lean toward the tip.
    # Built as flat sections (6 points: back, two faces, tooth edge) along the blade; a tooth = a section whose edge point
    # is pulled out and forward.
    BL = 0.74 if open_form else 0.58
    NT = (7 if LO else 13) if open_form else (6 if LO else 11)
    TH = 0.032 if open_form else 0.028                              # tooth height
    t0, t1 = 0.15, 0.97
    pitch = (t1 - t0) / NT
    stations = [(0.0, 0.0, 0.0), (0.07, 0.0, 0.0), (t0, 0.0, 0.0)]
    for i in range(NT):
        stations.append((t0 + (i + 0.42) * pitch + 0.012, TH, 1.0))      # tooth tip: out and leaning toward the tip
        stations.append((t0 + (i + 1) * pitch, 0.0, 0.0))
    stations.append((1.0, 0.0, 0.0))
    z0 = hz + 0.03
    WMAX = 0.19 if open_form else 0.17
    def width(t):
        """cleaver: narrow at the hinge, then a constant width to a square end (reference picture 5)"""
        u = min(1.0, t / 0.15)
        return 0.07 + (WMAX - 0.07) * u * u * (3 - 2 * u)
    # a flat plate: two planar faces (+x, -x) each a strip of a few big triangles between a straight back edge and the
    # zigzag tooth edge, plus the thin edges. One vertex per tooth tip and base on the tooth edge, one per tooth on the back.
    st = []
    for t, tooth, is_tip in stations:
        W = width(t) * (0.84 if t >= 1.0 else 1.0)          # the far end is cut square, the toothed corner a little chamfered
        vb = 0.075                                          # the back edge is a straight line
        vf = vb - W - tooth
        th = 0.0105 + 0.004 * math.sin(math.pi * min(1.0, t))
        if is_tip:
            th *= 0.4
        yc = (hy - 0.05 - 0.24 * t) if not open_form else (hy - 0.02 + 0.09 * math.sin(1.5 * t))
        st.append((z0 + BL * t, yc + vb, yc + vf, th, is_tip))
    bidx = [0] + [i for i in range(2, len(st)) if not st[i][4]]
    Fp = [s.vert((q[3], q[2], q[0]), "saw") for q in st]
    Fm = [s.vert((-q[3], q[2], q[0]), "saw") for q in st]
    Bp = [s.vert((st[i][3], st[i][1], st[i][0]), "saw") for i in bidx]
    Bm = [s.vert((-st[i][3], st[i][1], st[i][0]), "saw") for i in bidx]
    def face_col(a, b, c):
        zc = (s.v[a][2] + s.v[b][2] + s.v[c][2]) / 3
        yc = (s.v[a][1] + s.v[b][1] + s.v[c][1]) / 3
        return P("band")                                    # one flat bandage colour: thin triangles must not make stripes
    def strip(Bv, Fv):
        zb = [st[i][0] for i in bidx]
        zf = [q[0] for q in st]
        i = j = 0
        while i < len(Bv) - 1 or j < len(Fv) - 1:
            if j == len(Fv) - 1 or (i < len(Bv) - 1 and zb[i + 1] <= zf[j + 1]):
                tri = (Bv[i], Fv[j], Bv[i + 1])
                i += 1
            else:
                tri = (Bv[i], Fv[j], Fv[j + 1])
                j += 1
            mid = [sum(s.v[q][k] for q in tri) / 3 for k in (1, 2)]
            s.tri(tri[0], tri[1], tri[2], face_col(*tri), (0.0, mid[0], mid[1]))
    strip(Bp, Fp)
    strip(Bm, Fm)
    for i in range(len(Bp) - 1):                            # the back edge
        s.quad(Bp[i], Bp[i + 1], Bm[i + 1], Bm[i], P("band_dk"), (0.0, st[bidx[i]][1] - 0.1, st[bidx[i]][0]))
    for i in range(len(Fp) - 1):                            # the tooth edge: steel, a little blood
        col = P("blood") if hsh("bl", i) > 0.8 else P("steel_dk")
        s.quad(Fp[i], Fp[i + 1], Fm[i + 1], Fm[i], col, (0.0, st[i][2] + 0.1, st[i][0]))
    s.quad(Bp[0], Bm[0], Fm[0], Fp[0], P("iron"), (0.0, st[0][1], st[0][0] + 0.1))       # the hinge end
    s.quad(Bp[-1], Fp[-1], Fm[-1], Bm[-1], P("steel_dk"), (0.0, st[-1][1], st[-1][0] - 0.1))   # the far end
    bones = [{"name": "saw", "parent": None, "head": [0, 0, -0.17], "tail": [0, 0, 0.30]}]
    return finish(s, "saw_o" if open_form else "saw_c", bones)


def build_pistol():
    s = Skin()
    P = PAL.__getitem__
    # grip centre at the origin; barrel runs +z (to -z after the mirror). Octagonal barrel, brass lock, wooden grip.
    nbar = 3 if LO else 8
    barrel_z = [0.030 + 0.375 * i / (nbar - 1) for i in range(nbar)]
    bp = [(0.0, 0.058, z) for z in barrel_z]
    br = [0.027 if i < 1 else (0.020 if i < nbar - 1 else 0.026) for i in range(nbar)]
    r1 = tube(s, bp, br, "pistol", lambda i: P("steel") if i % 2 else P("steel_dk"), n=5 if LO else 6, ref=(0, 1, 0))
    s.cap(r1[0], P("brass"), bone="pistol")
    s.cap(r1[-1], P("iron"), bone="pistol")
    # lock/breech block
    lp = [(0.0, 0.052, 0.040), (0.0, 0.052, 0.000), (0.0, 0.050, -0.040), (0.0, 0.048, -0.075)]
    lr = [0.030, 0.034, 0.032, 0.026]
    if LO:
        lp, lr = [lp[0], lp[2], lp[3]], [lr[0], lr[2], lr[3]]
    lk = tube(s, lp, lr, "pistol", lambda i: P("brass") if i != 1 else P("gold_dk"), n=5 if LO else 6, ref=(0, 1, 0))
    s.cap(lk[-1], P("brass"), bone="pistol")
    # wooden grip: curved down and back, thicker toward the pommel
    gp = [(0.0, 0.040, -0.040), (0.0, 0.015, -0.050), (0.0, -0.020, -0.062), (0.0, -0.058, -0.078), (0.0, -0.095, -0.098), (0.0, -0.125, -0.118)]
    gr = [(0.022, 0.026), (0.021, 0.028), (0.022, 0.030), (0.023, 0.031), (0.025, 0.032), (0.027, 0.030)]
    if LO:
        gp, gr = [gp[0], gp[2], gp[4], gp[5]], [gr[0], gr[2], gr[4], gr[5]]
    elif True:
        gp, gr = [gp[0], gp[1], gp[3], gp[4], gp[5]], [gr[0], gr[1], gr[3], gr[4], gr[5]]
    gi = tube(s, gp, gr, "pistol", lambda i: P("wood") if i % 2 else P("wood_dk"), n=5 if LO else 6, ref=(0, 0, 1))
    s.cap(gi[-1], P("brass"), bone="pistol")
    # trigger guard ring (octahedra) and a small trigger tent under the lock
    for i in range(0 if LO else 5):
        a = math.pi * i / 4
        octa(s, (0.0, 0.020 - 0.032 * math.sin(a), -0.050 + 0.040 * math.cos(a) + 0.005), 0.0065, P("brass"), "pistol", 1.0, 1.0, 1.4)
    # hammer: its own bone, a small wedge on top of the lock
    hm = s.ring([(-0.007, 0.082, -0.030), (0.007, 0.082, -0.030), (0.007, 0.082, -0.050), (-0.007, 0.082, -0.050)], "hammer")
    hb = s.ring([(-0.009, 0.060, -0.030), (0.009, 0.060, -0.030), (0.009, 0.060, -0.060), (-0.009, 0.060, -0.060)], "pistol")
    s.bridge(hb, hm, P("iron"))
    top = [s.vert((-0.007, 0.108, -0.068), "hammer"), s.vert((0.007, 0.108, -0.068), "hammer")]
    s.quad(hm[3], hm[2], top[1], top[0], P("steel_dk"), (0, 0.07, -0.05))
    s.quad(hm[0], hm[1], hm[2], hm[3], P("iron"), (0, 0.07, -0.05))
    s.tri(hm[0], hm[3], top[0], P("iron"), (0, 0.07, -0.05))
    s.tri(hm[1], top[1], hm[2], P("iron"), (0, 0.07, -0.05))
    # blade-less bayonet lug and a ring on the barrel underside
    bones = [{"name": "pistol", "parent": None, "head": [0, 0.04, -0.10], "tail": [0, 0.04, 0.40]},
             {"name": "hammer", "parent": "pistol", "head": [0, 0.062, -0.040], "tail": [0, 0.108, -0.068]}]
    return finish(s, "pistol", bones)


def build_all():
    s = build_hunter()
    hunter, rig = finish(s, "hunter", BONES)
    saw_c, rig_c = build_saw(False)
    saw_o, rig_o = build_saw(True)
    pistol, rig_p = build_pistol()
    return [hunter, saw_c, saw_o, pistol], [rig, rig_c, rig_o, rig_p]


def main(argv):
    models, rigs = build_all()
    for m in models:
        print(f"{m.name}: {len(m.verts)} verts, {len(m.faces)} tris", file=sys.stderr)
    if len(argv) > 1:
        export.export_models(argv[1], M.Mesh(models), anim=A.Anim(rigs), title="Hunter")
        print("wrote", argv[1])


if __name__ == "__main__":
    main(sys.argv)
