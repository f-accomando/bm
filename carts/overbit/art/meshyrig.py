"""
meshyrig.py - the Meshy models of Overbit's heroes (art/meshy: NAME.mesh and
NAME.png, made from our descriptions by tools/meshy_text.py) on the heroes'
skeletons, in place of the bodies made of primitives.

Meshy makes a figure standing in A-pose (arms down and out, or straight out
in T-pose); the hero's skeleton rests with the arms hanging. For a figure:
 1. its own joints are measured on the mesh, at the heights of the
    skeleton's (hips, knees, ankles and toes; waist, chest, neck and the
    top of the head; shoulders, elbows, wrists and finger tips) from
    slices of the mesh and shells around the shoulder;
 2. each vertex goes to the bone whose segment is nearest, in units of
    the bone's thickness (rigid skinning: one bone a vertex; the faces
    between two bones stretch);
 3. each limb turns from where Meshy put it to the skeleton's rest, bone by
    bone (shoulder to elbow, elbow to wrist...), stretched along the bone
    to its length; the trunk moves onto z = 0.
The faces keep their texture corners, moved to the model's place on the
sheet (slot()); 1200 triangles at the levels of detail 2 and 3, 450 at 0
and 1. The parts of the old model on bones that are not the body's (the
weapon, a drone, a tail...) stay.
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "scripts"))
import bmmesh  # noqa: E402
from geo import Mesh, TEXTURED, lod_bits  # noqa: E402
from rig import turn_between  # noqa: E402

MESHY = os.path.join(HERE, "meshy")
TEX = 256                               # a model's texture on the sheet
SHEET_W, SHEET_H = 1024, 1024           # the sheet: the map's atlas (512x256) at 0, 0, then the models
MODELS = ["sarge", "frost", "fuse", "rail", "orbit", "akari", "rally_pilot", "kaiju_pilot", "rally_mech",
          "kaiju_mech"]
BODY_BONES = {"root", "hips", "spine", "chest", "head"} | {f"{b}.{s}" for b in (
    "upperarm", "forearm", "hand", "thigh", "shin", "foot") for s in "LR"}


def slot(name):
    """the corner of the model's texture on the sheet: two right of the
    map's atlas, then rows of four"""
    i = MODELS.index(name)
    if i < 2:
        return 512 + TEX * i, 0
    i -= 2
    return (i % 4) * TEX, TEX * (1 + i // 4)


def available(name):
    return os.path.exists(os.path.join(MESHY, name + ".mesh"))


# textures made brighter on the sheet: Rally's mech came out of Meshy grey
# where it is meant white (its greys lifted towards white, the orange of
# its stripes stronger)
WHITEN = {"rally_mech": 0.55}


def texture(name):
    """(w, h, rgba) of the model's texture"""
    from PIL import Image
    im = Image.open(os.path.join(MESHY, name + ".png")).convert("RGBA")
    lift = WHITEN.get(name)
    if lift:
        import colorsys
        px = []
        for r, g, b, a in im.getdata():
            h, l, sat = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
            if sat < 0.25:
                l = lift + (1 - lift) * l
            else:
                sat = min(1.0, sat * 1.25)
                l = min(0.62, l * 1.1)
            r2, g2, b2 = colorsys.hls_to_rgb(h, l, sat)
            px.append((round(r2 * 255), round(g2 * 255), round(b2 * 255), a))
        im = Image.new("RGBA", im.size)
        im.putdata(px)
    return im.width, im.height, im.tobytes()


def sheet_with(base, names):
    """the sheet (w, h, rgba): `base` (the map's atlas, or None) at 0, 0 and
    the textures of the models `names` in their slots"""
    from PIL import Image
    img = Image.new("RGBA", (SHEET_W, SHEET_H), (0, 0, 0, 255))
    if base:
        w, h, rgba = base
        img.paste(Image.frombytes("RGBA", (w, h), bytes(rgba)), (0, 0))
    for n in names:
        w, h, rgba = texture(n)
        img.paste(Image.frombytes("RGBA", (w, h), bytes(rgba)), slot(n))
    return img.width, img.height, img.tobytes()


# ------------------------------------------------------------------ vectors

def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def mul(a, k): return (a[0] * k, a[1] * k, a[2] * k)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def length(a): return math.sqrt(dot(a, a))


def norm(a):
    n = length(a)
    return mul(a, 1 / n) if n > 1e-9 else (0.0, -1.0, 0.0)


def rot(m, v):
    return (m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2])


def seg_dist(p, a, b):
    ab = sub(b, a)
    t = dot(sub(p, a), ab) / max(dot(ab, ab), 1e-12)
    t = max(0.0, min(1.0, t))
    return length(sub(p, add(a, mul(ab, t))))


def centroid(ps, fallback):
    if not ps:
        return fallback
    n = len(ps)
    return (sum(p[0] for p in ps) / n, sum(p[1] for p in ps) / n, sum(p[2] for p in ps) / n)


# ------------------------------------------------------------------ Meshy's rig

RIG_PARTS = [("forearm", "upperarm_fore"), ("upleg", "thigh"), ("shoulder", "chest"), ("arm", "upperarm"),
             ("hand", "hand"), ("thumb", "hand"), ("index", "hand"), ("middle", "hand"), ("ring", "hand"),
             ("pinky", "hand"), ("toe", "foot"), ("foot", "foot"), ("leg", "shin"), ("hips", "hips"),
             ("neck", "head"), ("head", "head"), ("spine", "spine")]


def rig_part(name):
    """our bone for a joint of Meshy's rig (Mixamo's names: LeftForeArm,
    RightUpLeg, Spine02...): (part, side), side "L", "R" or empty"""
    n = name.lower().replace("_", "").replace(":", "")
    side = "L" if "left" in n else "R" if "right" in n else ""
    for key, part in RIG_PARTS:
        if key in n:
            return ("forearm" if part == "upperarm_fore" else part), side
    return "chest", ""


def read_rig(name):
    """Meshy's rig of the model (meshy/NAME.rig, packed by meshy/pack.py), or
    None: (positions, the joint of each vertex (the heaviest), the joints
    [(name, position)]) in glTF's axes"""
    import json
    path = os.path.join(MESHY, name + ".rig")
    if not os.path.exists(path):
        return None
    r = json.load(open(path))
    f = lambda p: tuple(c / 10000 for c in p[:3])         # noqa: E731
    return [f(v) for v in r["verts"]], [v[3] for v in r["verts"]], [(n, f(p)) for n, p in r["joints"]]


def read_rig_glb(path):
    """a rigged .glb (tools/meshy_rig.py) -> as read_rig"""
    js, bin_ = bmmesh.read_glb(open(path, "rb").read())
    skin = js["skins"][0]
    nodes = js["nodes"]
    world = {}

    def visit(i, parent):
        m = bmmesh._mat_mul(parent, bmmesh._node_matrix(nodes[i]))
        world[i] = m
        for c in nodes[i].get("children", []):
            visit(c, m)
    scene = js.get("scenes", [{}])[js.get("scene", 0)]
    for r in scene.get("nodes", range(len(nodes))):
        visit(r, [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])
    joints = [(nodes[j].get("name", f"joint{j}"), tuple(world.get(j, [0] * 16)[12:15])) for j in skin["joints"]]
    pos, owner = [], []
    for mesh in js["meshes"]:
        for prim in mesh["primitives"]:
            a = prim["attributes"]
            if "JOINTS_0" not in a or "WEIGHTS_0" not in a:
                continue
            P = bmmesh._accessor(js, bin_, a["POSITION"])
            Jn = bmmesh._accessor(js, bin_, a["JOINTS_0"])
            W = bmmesh._accessor(js, bin_, a["WEIGHTS_0"])
            for p, jj, ww in zip(P, Jn, W):
                k = max(range(4), key=lambda i: ww[i])
                pos.append(tuple(p))
                owner.append(int(jj[k]))
    if not pos:
        raise SystemExit(f"{path}: no skinned mesh")
    return pos, owner, joints


def rig_frame(rig, verts):
    """Meshy's rig in the frame of the figure `verts` (ours: facing +z,
    standing on y = 0): glTF's x turns (-x), the size and the middle of the
    bounding boxes match -> (positions, joints)"""
    pos, _, joints = rig
    lo = [min(p[k] for p in pos) for k in range(3)]
    hi = [max(p[k] for p in pos) for k in range(3)]
    flo = [min(p[k] for p in verts) for k in range(3)]
    fhi = [max(p[k] for p in verts) for k in range(3)]
    s = (fhi[1] - flo[1]) / max(hi[1] - lo[1], 1e-6)
    mid = [(lo[k] + hi[k]) / 2 for k in range(3)]
    fmid = [(flo[k] + fhi[k]) / 2 for k in range(3)]
    sign = (-1, 1, 1)
    go = lambda p: tuple(fmid[k] + sign[k] * s * (p[k] - mid[k]) for k in range(3))     # noqa: E731
    return [go(p) for p in pos], [(n, go(p)) for n, p in joints]


def rig_labels(rig, verts):
    """the bone ("upperarm.L", "chest"...) of each vertex of the figure
    `verts` from Meshy's rig: the joint of its nearest rig vertex"""
    rpos, rjoints = rig_frame(rig, verts)
    owner = rig[1]
    cell = 0.05
    grid = {}
    for i, p in enumerate(rpos):
        grid.setdefault(tuple(int(math.floor(c / cell)) for c in p), []).append(i)
    names = [rig_part(n) for n, _ in rjoints]
    # the trunk has no sides; the lowest spine joint is the spine, the
    # others the chest
    names = [(p, "" if p in ("hips", "spine", "chest", "head") else sd) for p, sd in names]
    spine = sorted((rjoints[i][1][1], i) for i, (p, _) in enumerate(names) if p == "spine")
    for _, i in spine[1:]:
        names[i] = ("chest", "")
    out = []
    for p in verts:
        c = tuple(int(math.floor(v / cell)) for v in p)
        best, bi = None, 0
        for r in range(0, 4):
            for dx in range(-r, r + 1):
                for dy in range(-r, r + 1):
                    for dz in range(-r, r + 1):
                        if max(abs(dx), abs(dy), abs(dz)) != r:
                            continue
                        for i in grid.get((c[0] + dx, c[1] + dy, c[2] + dz), ()):
                            d = sum((p[k] - rpos[i][k]) ** 2 for k in range(3))
                            if best is None or d < best:
                                best, bi = d, i
            if best is not None and best <= (r * cell) ** 2:
                break
        part, side = names[owner[bi]]
        out.append(part + ("." + side if side else ""))
    return out, rjoints


def rig_joints(rjoints):
    """the limbs' joints of Meshy's rig by our names (shoulder.L, elbow.L,
    wrist.L, hip.L, knee.L, ankle.L, toe.L...), in the figure's frame"""
    at = {"upperarm": "shoulder", "forearm": "elbow", "hand": "wrist", "thigh": "hip", "shin": "knee",
          "foot": "ankle"}
    out = {}
    for name, p in rjoints:
        n = name.lower()
        part, side = rig_part(name)
        if not side or part not in at:
            continue
        if part == "hand" and not n.endswith("hand"):
            continue                    # the fingers' joints
        key = ("toe" if "toe" in n else at[part]) + "." + side
        if "end" in n or key in out:
            continue
        out[key] = p
    return out


# ------------------------------------------------------------------ the figure

def spec_height(name):
    """the height of NAME.txt (the figure's, in metres), or None"""
    path = os.path.join(MESHY, name + ".txt")
    for line in open(path) if os.path.exists(path) else ():
        k, _, v = line.partition("=")
        if k.strip() == "height":
            return float(v)
    return None


def load(name):
    """the two models of NAME.mesh: (1200 triangles, 450), as tall as the
    spec says (a figure made smaller can grow: the mechs)"""
    models, _ = bmmesh.decode(open(os.path.join(MESHY, name + ".mesh"), "rb").read())
    by = {m["name"]: m for m in models}
    hi, lo = by[name], by[name + "_lo"]
    h = spec_height(name)
    ys = [p[1] for p in hi["verts"] + lo["verts"]]
    top = max(ys) - min(ys)
    if h and abs(h - top) > 0.01 * h:
        s = h / top
        hi = dict(hi, verts=[(p[0] * s, p[1] * s, p[2] * s) for p in hi["verts"]])
        lo = dict(lo, verts=[(p[0] * s, p[1] * s, p[2] * s) for p in lo["verts"]])
    return hi, lo


def joints_of(sk):
    """the skeleton's joints at rest by name (head and tail of each bone)"""
    return {n: (h, t) for n, _, h, t in sk.bones}


def fit(verts, sk):
    """the A-pose joints of the figure `verts` (already at the skeleton's
    height, standing on y = 0): a dict like joints_of() plus the trunk's z"""
    J = joints_of(sk)
    hip_y = J["hips"][0][1]
    top = J["head"][1][1]
    k = top / 1.75
    trunk = lambda p: abs(p[0]) < 0.22 * k      # noqa: E731

    def sl(y, h, pred=None):
        return [p for p in verts if abs(p[1] - y) <= h and (pred is None or pred(p))]

    neck_y = J["head"][0][1]
    zc = centroid([p for p in verts if hip_y <= p[1] <= neck_y and trunk(p)], (0, 0, 0))[2]
    out = {"zc": zc}
    # the trunk: the middle of its slices
    for name in ("spine", "chest", "head"):
        y = J[name][0][1]
        c = centroid(sl(y, 0.03 * k, trunk), (0, y, zc))
        out[name] = (0.0, y, c[2])
    out["hips"] = (0.0, hip_y, zc)
    out["pelvis"] = (0.0, hip_y - 0.10 * k, zc)
    out["top"] = (0.0, top, centroid(sl(top - 0.06 * k, 0.04 * k, trunk), (0, 0, zc))[2])
    # the shoulders: the trunk's width at the chest (no gap wider than 2 cm
    # from the middle), both sides; near the skeleton's own
    chest_sl = sl(J["chest"][0][1], 0.025 * k)
    widths = []
    for x in (-1, 1):
        width = last = 0.0
        for v in sorted(x * p[0] for p in chest_sl if x * p[0] > 0):
            if v - last > 0.02 * k:
                break
            width = last = v
        widths.append(width)
    prior = abs(J["upperarm.R"][0][0])
    shx = max(0.8 * prior, min(1.25 * prior, 0.9 * sum(widths) / 2 if all(widths) else prior))
    for s, x in (("L", -1), ("R", 1)):
        # the legs: slices of the side, below the hips
        leg = lambda p, x=x: x * p[0] > 0.005 and p[1] < hip_y - 0.04 * k        # noqa: E731
        knee_y, ankle_y = J[f"thigh.{s}"][1][1], J[f"shin.{s}"][1][1]
        hipj = centroid(sl(hip_y - 0.12 * k, 0.03 * k, leg), J[f"thigh.{s}"][0])
        out[f"hip.{s}"] = (hipj[0], hip_y, zc)
        out[f"knee.{s}"] = centroid(sl(knee_y, 0.03 * k, leg), J[f"thigh.{s}"][1])
        out[f"ankle.{s}"] = centroid(sl(ankle_y + 0.02 * k, 0.02 * k, leg), J[f"shin.{s}"][1])
        feet = [p for p in verts if leg(p) and p[1] < ankle_y + 0.03 * k]
        tip = max(feet, key=lambda p: p[2]) if feet else J[f"foot.{s}"][1]
        a = out[f"ankle.{s}"]
        out[f"toe.{s}"] = (a[0], 0.03 * k, tip[2] - 0.02 * k)
        # the arms: the shoulder at the trunk's side, the finger tip the
        # farthest point out; the elbow the middle of the arm's points at the
        # upper arm's length from the shoulder, the wrist at the forearm's
        # from the elbow (bent arms too)
        sy = J[f"upperarm.{s}"][0][1]
        sh = (x * shx, sy, centroid(sl(sy, 0.03 * k, trunk), (0, 0, zc))[2])
        arm = [p for p in verts if x * p[0] > shx * 0.85 and p[1] > J[f"thigh.{s}"][1][1]]
        # (the farthest from the trunk's axis: arms out, or forearms forward)
        tip = max(arm, key=lambda p: p[0] * p[0] + (p[2] - zc) ** 2) if arm else J[f"hand.{s}"][1]
        up = length(sub(J[f"upperarm.{s}"][1], J[f"upperarm.{s}"][0]))
        lo = length(sub(J[f"forearm.{s}"][1], J[f"forearm.{s}"][0]))
        reach = length(sub(tip, sh))
        guess_e = add(sh, mul(sub(tip, sh), min(0.45, up / max(reach, 1e-6))))
        el = centroid([p for p in arm if abs(length(sub(p, sh)) - up) < 0.03 * k], guess_e)
        # the wrist: along elbow -> finger tip as the skeleton splits forearm
        # and hand (with its fingers)
        hand = 2.0 * length(sub(J[f"hand.{s}"][1], J[f"hand.{s}"][0]))
        wr = add(el, mul(sub(tip, el), lo / (lo + hand)))
        out[f"shoulder.{s}"], out[f"elbow.{s}"], out[f"wrist.{s}"], out[f"tip.{s}"] = sh, el, wr, tip
    if "tail" in J:
        # a tail: from the hips back to the farthest point behind them
        back = [p for p in verts if p[2] < zc - 0.10 * k and p[1] < hip_y + 0.10 * k]
        end = min(back, key=lambda p: p[2]) if back else add(J["tail"][1], (0, 0, zc))
        out["tail"] = ((0.0, hip_y - 0.03 * k, zc - 0.06 * k), end)
    return out


def segments(F, sk):
    """bone -> (a, b, thickness) in the figure, for the nearest-bone test"""
    k = joints_of(sk)["head"][1][1] / 1.75
    head_k = max(1.0, (F["top"][1] - F["head"][1]) / (0.25 * k))
    seg = {
        "hips": (F["pelvis"], F["spine"], 0.16 * k),
        "spine": (F["spine"], F["chest"], 0.16 * k),
        "chest": (F["chest"], F["head"], 0.18 * k),
        "head": (F["head"], F["top"], 0.11 * k * head_k),
    }
    for s in "LR":
        seg[f"upperarm.{s}"] = (F[f"shoulder.{s}"], F[f"elbow.{s}"], 0.065 * k)
        seg[f"forearm.{s}"] = (F[f"elbow.{s}"], F[f"wrist.{s}"], 0.055 * k)
        seg[f"hand.{s}"] = (F[f"wrist.{s}"], F[f"tip.{s}"], 0.05 * k)
        seg[f"thigh.{s}"] = (F[f"hip.{s}"], F[f"knee.{s}"], 0.09 * k)
        seg[f"shin.{s}"] = (F[f"knee.{s}"], F[f"ankle.{s}"], 0.07 * k)
        seg[f"foot.{s}"] = (F[f"ankle.{s}"], F[f"toe.{s}"], 0.06 * k)
    return seg


def bone_of(p, seg, F, hip_y, extra):
    best, who = None, "chest"
    for name, (a, b, r) in list(seg.items()) + extra:
        if name.endswith(".L") and p[0] > 0.02:
            continue
        if name.endswith(".R") and p[0] < -0.02:
            continue
        if name.split(".")[0] in ("thigh", "shin", "foot") and p[1] > hip_y + 0.02:
            continue
        d = seg_dist(p, a, b) / r
        if best is None or d < best:
            best, who = d, name
    return who


def limb_map(a_m, b_m, a_s, b_s, stretch=True):
    """the figure's segment a_m -> b_m onto the bone a_s -> b_s: turned,
    stretched along the bone to its length"""
    R = turn_between(sub(b_m, a_m), sub(b_s, a_s))
    u = norm(sub(b_s, a_s))
    k = length(sub(b_s, a_s)) / max(length(sub(b_m, a_m)), 1e-6) if stretch else 1.0

    def go(p):
        q = rot(R, sub(p, a_m))
        q = add(q, mul(u, (k - 1.0) * dot(q, u)))
        return add(a_s, q)
    return go


def transforms(F, sk):
    """bone -> function taking a figure point to the skeleton's rest"""
    J = joints_of(sk)
    zc = F["zc"]
    trunk = lambda p: (p[0], p[1], p[2] - zc)         # noqa: E731
    limb = limb_map

    out = {n: trunk for n in ("root", "hips", "spine", "chest", "head")}
    for s in "LR":
        out[f"upperarm.{s}"] = limb(F[f"shoulder.{s}"], F[f"elbow.{s}"], *J[f"upperarm.{s}"])
        out[f"forearm.{s}"] = limb(F[f"elbow.{s}"], F[f"wrist.{s}"], *J[f"forearm.{s}"])
        out[f"hand.{s}"] = limb(F[f"wrist.{s}"], F[f"tip.{s}"], *J[f"hand.{s}"], stretch=False)
        out[f"thigh.{s}"] = limb(F[f"hip.{s}"], F[f"knee.{s}"], *J[f"thigh.{s}"])
        out[f"shin.{s}"] = limb(F[f"knee.{s}"], F[f"ankle.{s}"], *J[f"shin.{s}"])
        # the foot turns with the shin (its own direction is the toe's guess)
        out[f"foot.{s}"] = limb(F[f"knee.{s}"], F[f"ankle.{s}"], J[f"shin.{s}"][0], J[f"shin.{s}"][1])
    return out


def two_bones(m, f0, v0):
    """no face of m (from face f0, vertex v0 on) on three bones (the vertex
    shader takes faces on one bone or two): a corner whose bone the other
    corners have not goes to the bone most of its neighbours have; where
    three parts meet (the crotch, an armpit) the face gets its own copy of
    the corner, on the bone of another corner"""
    faces, vbone = m.faces, m.vbone
    three = lambda f: len({vbone[f[0]], vbone[f[1]], vbone[f[2]]}) == 3      # noqa: E731
    for _ in range(4):
        bad = [f for f in faces[f0:] if three(f)]
        if not bad:
            return
        near = {}
        for f in faces[f0:]:
            for v in f[:3]:
                near.setdefault(v, []).extend(vbone[w] for w in f[:3] if w != v)
        for f in bad:
            v = min(f[:3], key=lambda v: sum(1 for b in near.get(v, ()) if b == vbone[v]))
            others = {vbone[w] for w in f[:3] if w != v}
            votes = [b for b in near.get(v, ()) if b in others]
            if v >= v0 and votes:
                vbone[v] = max(set(votes), key=votes.count)
    for i in range(f0, len(faces)):
        f = faces[i]
        if three(f):
            m.verts.append(m.verts[f[0]])
            m.vbone.append(vbone[f[1]])
            m.vhard.append(False)
            faces[i] = (len(m.verts) - 1,) + tuple(f[1:])


class TexMesh(Mesh):
    """a Mesh whose faces may carry texture corners (a, b, c, colour, uv)"""

    def model(self):
        return {"name": self.name, "verts": self.verts,
                "faces": [(f[0], f[1], f[2], f[3], f[4] if len(f) > 4 else (0,) * 6) for f in self.faces]}


def dress(old, sk, name, keep_extra=()):
    """the hero's model `old` (a geo.Mesh on skeleton sk) with the body of
    the Meshy model `name`: -> TexMesh. keep_extra: bones whose figure
    part follows them (a tail: its rest segment, as the skeleton has it)"""
    hi, lo = load(name)
    J = joints_of(sk)
    top = J["head"][1][1]
    hip_y = J["hips"][0][1]
    m = TexMesh(old.name)
    ox, oy = slot(name)
    # the figure at the skeleton's height, on its feet, facing +z (Meshy's
    # front is glTF's +z, which meshy2mesh turned to -z)
    allv = hi["verts"] + lo["verts"]
    y0 = min(p[1] for p in allv)
    s = top / max(max(p[1] for p in allv) - y0, 1e-6)
    hi = dict(hi, verts=[(-p[0], p[1], -p[2]) for p in hi["verts"]])
    lo = dict(lo, verts=[(-p[0], p[1], -p[2]) for p in lo["verts"]])
    fits = None
    for model, lods in ((hi, (2, 3)), (lo, (0, 1))):
        verts = [(p[0] * s, (p[1] - y0) * s, p[2] * s) for p in model["verts"]]
        rig = read_rig(name)
        labels = None
        if rig:
            labels, rjoints = rig_labels(rig, verts)
        if fits is None:
            F = fit(verts, sk)
            if rig:
                # the rig's joints for the limbs (its shoulders, elbows,
                # wrists, hips, knees, ankles), the finger tips the farthest
                # points of the hands
                F.update(rig_joints(rjoints))
                for s_ in "LR":
                    hand = [p for p, b in zip(verts, labels) if b == "hand." + s_]
                    if hand:
                        F["tip." + s_] = max(hand, key=lambda p: length(sub(p, F["wrist." + s_])))
            seg = segments(F, sk)
            extra = [(b, F[b] + (0.08 * top / 1.75,) if b in F else (J[b][0], J[b][1], 0.08 * top / 1.75))
                     for b in keep_extra]
            go = transforms(F, sk)
            for b in keep_extra:
                if b in F:              # fitted on the figure: turned onto the bone
                    go[b] = limb_map(F[b][0], F[b][1], *J[b])
                else:
                    go[b] = lambda p, zc=F["zc"]: (p[0], p[1], p[2] - zc)
            fits = (F, seg, extra, go)
        F, seg, extra, go = fits
        base = len(m.verts)
        for i, p in enumerate(verts):
            b = labels[i] if labels else bone_of(p, seg, F, hip_y, extra)
            if labels and extra and b in ("hips", "spine", "thigh.L", "thigh.R"):
                # a part the rig has not (a tail): the nearest bone again
                bx = bone_of(p, seg, F, hip_y, extra)
                if bx in keep_extra:
                    b = bx
            if b not in go:
                b = "chest"
            m.verts.append(go[b](p))
            m.vbone.append(sk.index[b])
            m.vhard.append(False)
        colour = TEXTURED | lod_bits(*lods)
        for a, b, c, _, uv in model["faces"]:
            m.faces.append((base + a, base + b, base + c, colour,
                            tuple(uv[i] + (ox if i % 2 == 0 else oy) for i in range(6))))
        two_bones(m, len(m.faces) - len(model["faces"]), base)
    # what the old model has on other bones: the weapon, the props
    body = BODY_BONES | set(keep_extra)
    names = {i: n for n, i in sk.index.items()}
    used = {}
    for a, b, c, col in old.faces:
        if names[old.vbone[a]] in body:
            continue
        ids = []
        for v in (a, b, c):
            if v not in used:
                used[v] = len(m.verts)
                m.verts.append(old.verts[v])
                m.vbone.append(old.vbone[v])
                m.vhard.append(old.vhard[v])
            ids.append(used[v])
        m.faces.append((ids[0], ids[1], ids[2], col))
    return m


# ------------------------------------------------------------------ the mechs

# the mech's bone that takes each bone of the figure (measured as a person):
# the head is the cockpit (the canopy, the dome), where hits are critical
# (the forms' `head` in src/5x_*.lua, hit3d)
MECH_BONES = {
    "rally": {"hips": "hips", "spine": "body", "chest": "body", "head": "canopy", "upperarm": "arm", "forearm": "gun",
              "hand": "gun"},
    "kaiju": {"hips": "hips", "spine": "body", "chest": "body", "head": "dome", "upperarm": "arm", "forearm": "fore",
              "hand": "fore"},
}
MECH_DROP = {"rally": {"canopy"}, "kaiju": {"dome"}}       # old parts the figure replaces


def dress_mech(mod, kind, old, sk_old, name):
    """a mech (kind "rally" or "kaiju", its module mod) with the body of the
    Meshy model `name`: a skeleton with the old one's bones, placed on the
    figure (the arms hang from the shoulders, the forearms are the
    cannons, pointing forward; the legs as the figure has them), its clips
    made again by mod.mech_clips with the legs' constants of the figure ->
    (TexMesh, Skeleton, clips). The parts on other bones (missile pods,
    the blade) move with their bone."""
    import humanoid
    from rig import Skeleton
    hi, lo = load(name)
    allv = hi["verts"] + lo["verts"]
    y0 = min(p[1] for p in allv)
    top = max(p[1] for p in allv) - y0
    turn = lambda ps: [(-p[0], p[1] - y0, -p[2]) for p in ps]       # noqa: E731
    proxy = humanoid.skeleton(humanoid.Body(height=top))
    F = fit(turn(hi["verts"]), proxy)
    rig = read_rig(name)
    if rig:
        labels_hi, rjoints = rig_labels(rig, turn(hi["verts"]))
        F.update(rig_joints(rjoints))
        for s_ in "LR":
            hand = [p for p, b in zip(turn(hi["verts"]), labels_hi) if b == "hand." + s_]
            if hand:
                F["tip." + s_] = max(hand, key=lambda p: length(sub(p, F["wrist." + s_])))
    zc = F["zc"]
    z0 = lambda p: (p[0], p[1], p[2] - zc)                          # noqa: E731
    hip_y = F["hips"][1]
    J_old = joints_of(sk_old)
    # the parts the figure has not (pods, the sword, the shield) grow with it
    grow = top / max(v[1] for v in old.verts)
    new = {}                                                        # bone -> (head, tail)
    new["root"] = ((0, 0, 0), (0, 0.5, 0))
    new["hips"] = ((0, hip_y, 0), (0, F["spine"][1], 0))
    new["body"] = ((0, F["spine"][1], 0), (0, F["top"][1], 0))
    for b in MECH_DROP[kind]:
        # the cockpit's bone at the figure's neck (it turns the head)
        h, t = J_old[b]
        nh = (0.0, F["head"][1], 0.0)
        new[b] = (nh, add(nh, sub(t, h)))
    for s, x in (("L", -1), ("R", 1)):
        sh, el, tip = F[f"shoulder.{s}"], F[f"elbow.{s}"], F[f"tip.{s}"]
        up, fore = length(sub(el, sh)), length(sub(tip, el))
        a_h = (sh[0], sh[1], 0.0)
        a_t = add(a_h, (x * 0.05 * up, -up, 0.05 * up))
        new[f"arm.{s}"] = (a_h, a_t)
        fname = "gun" if kind == "rally" else "fore"
        oh, ot = J_old[f"{fname}.{s}"]
        new[f"{fname}.{s}"] = (a_t, add(a_t, mul(norm(sub(ot, oh)), fore)))
        new[f"thigh.{s}"] = ((F[f"hip.{s}"][0], hip_y, 0.0), z0(F[f"knee.{s}"]))
        new[f"shin.{s}"] = (z0(F[f"knee.{s}"]), z0(F[f"ankle.{s}"]))
        new[f"foot.{s}"] = (z0(F[f"ankle.{s}"]), z0(F[f"toe.{s}"]))
        if kind == "rally":
            oh, ot = J_old[f"pod.{s}"]
            ph = (x * 0.8 * abs(sh[0]), sh[1] + 0.22 * top / 2.4, oh[2] * grow)
            new[f"pod.{s}"] = (ph, add(ph, mul(sub(ot, oh), grow)))
            # the rotary gun's barrels at the end of the forearm
            gh, gt = new[f"gun.{s}"]
            oh, ot = J_old[f"rotor.{s}"]
            rh = add(gt, mul(sub(oh, J_old[f"gun.{s}"][1]), grow))
            new[f"rotor.{s}"] = (rh, add(rh, mul(sub(ot, oh), grow)))
    if kind == "kaiju":
        # the sword in the left fist (at the forearm's end), the shield on
        # the outer side of the right forearm, as far along it as before and
        # just out of the figure's forearm
        oh, ot = J_old["blade"]
        bh = new["fore.L"][1]
        new["blade"] = (bh, add(bh, mul(sub(ot, oh), grow)))
        oh, ot = J_old["shield"]
        fh, ft = J_old["fore.R"]
        d_old = norm(sub(ft, fh))
        off = sub(oh, fh)
        along = sum(a * b for a, b in zip(off, d_old))
        perp = sub(off, mul(d_old, along))
        nh, nt = new["fore.R"]
        d_new = norm(sub(nt, nh))
        el, tip = F["elbow.R"], F["tip.R"]
        axis = norm(sub(tip, el))
        fore_pts = [p for p, b in zip(turn(hi["verts"]), labels_hi) if b == "forearm.R"] if rig else []
        radial = sorted(length(sub(sub(p, el), mul(axis, sum(a * b for a, b in zip(sub(p, el), axis)))))
                        for p in fore_pts)
        r = radial[int(len(radial) * 0.9)] if radial else 0.2 * grow
        sh_h = add(add(nh, mul(d_new, along / max(length(sub(ft, fh)), 1e-6) * length(sub(nt, nh)))),
                   (r + 0.03, perp[1] * grow, perp[2] * grow))
        new["shield"] = (sh_h, add(sh_h, mul(sub(ot, oh), grow)))
    sk = Skeleton()
    for n, parent, _, _ in sk_old.bones:
        sk.bone(n, sk_old.bones[parent][0] if parent >= 0 else None, *new[n])
    # the clips: the legs' constants of the module from the figure, for
    # mod.mech_clips (and back as they were)
    leg = new["thigh.R"], new["shin.R"], new["foot.R"]
    consts = {"HIP_Y": hip_y, "KNEE": (leg[0][1][1], leg[0][1][2])}
    if kind == "rally":
        consts.update(LEG_X=abs(leg[0][0][0]), HOCK=(leg[1][1][1], leg[1][1][2]), TOE=(leg[2][1][1], leg[2][1][2]))
    else:
        consts.update(ANKLE=(leg[1][1][1], leg[1][1][2]))
    saved = {c: getattr(mod, c) for c in consts}
    for c, v in consts.items():
        setattr(mod, c, v)
    try:
        clips = mod.mech_clips(sk)
    finally:
        for c, v in saved.items():
            setattr(mod, c, v)
    # the figure on the bones
    J = joints_of(proxy)
    seg = segments(F, proxy)
    go_h = transforms(F, proxy)
    go = {}
    for b in go_h:
        go[b] = go_h[b]
    fname = "gun" if kind == "rally" else "fore"
    for s in "LR":
        go[f"upperarm.{s}"] = limb_map(F[f"shoulder.{s}"], F[f"elbow.{s}"], *new[f"arm.{s}"])
        chain = limb_map(F[f"elbow.{s}"], F[f"tip.{s}"], *new[f"{fname}.{s}"])
        go[f"forearm.{s}"] = go[f"hand.{s}"] = chain
        go[f"thigh.{s}"] = limb_map(F[f"hip.{s}"], F[f"knee.{s}"], *new[f"thigh.{s}"])
        go[f"shin.{s}"] = limb_map(F[f"knee.{s}"], F[f"ankle.{s}"], *new[f"shin.{s}"])
        go[f"foot.{s}"] = go[f"shin.{s}"]
    for b in ("hips", "spine", "chest", "head", "root"):
        go[b] = z0
    rename = MECH_BONES[kind]
    m = TexMesh(old.name)
    ox, oy = slot(name)
    for model, lods in ((hi, (2, 3)), (lo, (0, 1))):
        base = len(m.verts)
        tv = turn(model["verts"])
        labels = rig_labels(rig, tv)[0] if rig else None
        for i, p in enumerate(tv):
            b = labels[i] if labels else bone_of(p, seg, F, hip_y, [])
            part, _, side = b.partition(".")
            mb = rename.get(part, part) + ("." + side if side else "")
            m.verts.append(go[b](p))
            m.vbone.append(sk.index[mb])
            m.vhard.append(False)
        colour = TEXTURED | lod_bits(*lods)
        for a, b, c, _, uv in model["faces"]:
            m.faces.append((base + a, base + b, base + c, colour,
                            tuple(uv[i] + (ox if i % 2 == 0 else oy) for i in range(6))))
        two_bones(m, len(m.faces) - len(model["faces"]), base)
    # the old parts on the bones the figure has not (pods, the blade), moved with their bone
    figure = {"root", "hips", "body"} | MECH_DROP[kind] | {f"{b}.{s}" for b in (
        "arm", fname, "thigh", "shin", "foot") for s in "LR"}
    names = {i: n for n, i in sk_old.index.items()}
    used = {}
    for a, b, c, col in old.faces:
        bn = names[old.vbone[a]]
        if bn in figure:
            continue
        ids = []
        for v in (a, b, c):
            if v not in used:
                bv = names[old.vbone[v]]
                used[v] = len(m.verts)
                m.verts.append(add(new[bv][0], mul(sub(old.verts[v], J_old[bv][0]), grow)))
                m.vbone.append(sk.index[names[old.vbone[v]]])
                m.vhard.append(old.vhard[v])
            ids.append(used[v])
        m.faces.append((ids[0], ids[1], ids[2], col))
    return m, sk, clips


# ------------------------------------------------------------------ for models.py

MECHS = {"rally_mech": "rally", "kaiju_mech": "kaiju"}


def apply(mod, built):
    """a hero's models (module mod, its build() list) with the Meshy bodies
    where there is one: -> (the list, the names of the Meshy models used)"""
    out, used = [], []
    for m, sk, clips in built:
        if m.name not in MODELS or not available(m.name):
            out.append((m, sk, clips))
            continue
        if m.name in MECHS:
            out.append(dress_mech(mod, MECHS[m.name], m, sk, m.name))
        else:
            extra = [b for b in ("tail",) if b in sk.index]
            out.append((dress(m, sk, m.name, keep_extra=extra), sk, clips))
        used.append(m.name)
    return out, used
