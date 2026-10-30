"""enemy_clips: the clips of the enemies (12 frames a second, linear, a key on every frame), in place.

  villager, huntsman   idle, walk, run, attack1, attack2, hit, death      (the Hunter's solver: IK for legs and arms)
  dog                  idle, walk, run, bite, leap, hit, death            (local rotations of the bones, no IK)

The two men share the skeleton of the Hunter, so the clips are made with hunter_clips (its solve, from_keys, make_clip) and a
hunched base pose: hips and spine forward, the head lifted, the arms hanging. Axes of the finished models: +x is the .L side,
forward is -z. Positive x rotation raises a toe or a nose and swings a hanging leg forward.
"""
import math
import os
import sys

os.environ["HUNTER_LOD"] = "lo"
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "hunter"))
import hunter_clips as HC  # noqa: E402
from hunter_clips import merge, from_keys, make_clip, cyc  # noqa: E402
from bm_core import animgen  # noqa: E402

# ---------------------------------------------------------------- the men

HUNCH = dict(
    hips=[8, 0, 0], hmove=[0, -0.07, 0], spine=[18, 0, 0], chest=[10, 0, 0], neck=[-8, 0, 0], head=[-16, 0, 0],
    fL=[0.14, 0.09, 0.05, 0, 4], fR=[0.14, 0.09, -0.05, 0, -4], wL=[0, -0.8, 0], wR=[0, -0.8, 0], fw=0.0,
    hL=[0.06, -0.50, 0.10], hR=[0.06, -0.50, 0.10], dL=[0.1, -1.0, 0.0], dR=[0.1, -1.0, 0.0], rL=0, rR=0,
    gL=0.7, gR=0.7, eL=[0.5, -1.0, 0.0], eR=[0.5, -1.0, 0.0],
)
BASES = {
    # the cleaver in the right hand, held low and forward; the left arm, wrapped in rags, hangs
    "villager": merge(HUNCH, dict(hR=[0.04, -0.34, 0.30], dR=[0.8, -0.55, 0.0], gR=1.0, hL=[0.10, -0.52, 0.04])),
    # the torch raised in the left hand, the axe low in the right
    "huntsman": merge(HUNCH, dict(hL=[0.10, 0.34, 0.12], dL=[0.15, 1.0, 0.0], gL=1.0, hR=[0.05, -0.38, 0.26], dR=[0.7, -0.7, 0.0],
                                  gR=1.0, eL=[0.9, 0.2, -0.3])),
}


def shamble(base, frames, stride, lift, sway, arms, tilt):
    def fn(f):
        p = f / float(frames)
        d = merge(base, {})
        for side, ph in (("L", p), ("R", cyc(p + 0.5))):
            if ph < 0.5:
                u = ph / 0.5
                fwd, up, pitch = stride - 2 * stride * u, 0.09, 6 * (1 - u) - 18 * max(0.0, (u - 0.7) / 0.3) ** 2
            else:
                u = (ph - 0.5) / 0.5
                fwd = -stride + 2 * stride * (u * u * (3 - 2 * u))
                up = 0.09 + lift * math.sin(math.pi * u)
                pitch = -18 * (1 - u) + 6 * u
            d["f" + side] = [0.12, up, fwd, pitch, 0]
        sw = math.sin(2 * math.pi * p)
        d["hmove"] = [sway * sw, base["hmove"][1] - 0.025 * math.cos(4 * math.pi * p), 0]
        d["hips"] = [base["hips"][0], -6 * sw, tilt * sw]
        d["spine"] = [base["spine"][0], 9 * sw, 0]
        d["head"] = [base["head"][0] + 3 * math.sin(4 * math.pi * p), -8 * sw, 4 * sw]
        for side, k in (("L", 1), ("R", -1)):                      # the free arm swings, the one with a weapon only a little
            h = list(base["h" + side])
            h[2] = h[2] + k * arms * sw * (0.4 if base["g" + side] >= 1.0 else 1.0)
            d["h" + side] = h
        return d
    return fn


def idle_fn(base, frames=24):
    def fn(f):
        p = f / float(frames)
        b = math.sin(2 * math.pi * p)
        d = merge(base, {})
        d["hmove"] = [0.01 * math.sin(2 * math.pi * p * 2), base["hmove"][1] + 0.01 * b, 0]
        d["spine"] = [base["spine"][0] + 2 * b, 3 * math.sin(2 * math.pi * p + 1), 0]
        d["head"] = [base["head"][0] - 2 * b, 6 * math.sin(2 * math.pi * p * 2), 0]
        for side in ("L", "R"):
            h = list(base["h" + side])
            h[1] = h[1] + 0.012 * b
            d["h" + side] = h
        return d
    return fn


def man_clips(kind):
    base = BASES[kind]
    out = []
    out.append(make_clip("idle", 24, idle_fn(base), True))
    out.append(make_clip("walk", 16, shamble(base, 16, 0.15, 0.05, 0.03, 0.10, 3), True))
    out.append(make_clip("run", 10, shamble(merge(base, dict(hips=[16, 0, 0], spine=[24, 0, 0], head=[-22, 0, 0], hmove=[0, -0.12, 0])),
                                           10, 0.30, 0.15, 0.02, 0.22, 4), True))
    if kind == "villager":
        a1 = [  # a chop: the cleaver goes up behind, then down in front
            (0, {}, "lin"),
            (4, dict(hips=[2, -18, 0], spine=[8, -22, 0], chest=[4, -14, 0], head=[-8, 10, 0], hR=[0.25, 0.34, -0.10], dR=[-0.2, 1.0, -0.1],
                     hL=[0.12, -0.2, 0.2]), "smooth"),
            (7, dict(hips=[22, 14, 0], hmove=[0, -0.16, 0.28], spine=[30, 18, 0], chest=[16, 10, 0], head=[-20, -8, 0],
                     hR=[0.04, -0.50, 0.62], dR=[0.8, -0.6, 0.0], hL=[0.14, -0.30, 0.20],
                     fL=[0.14, 0.09, 0.30, 0, 0], fR=[0.14, 0.09, -0.12, 0, 0]), "in"),
            (9, dict(hips=[22, 14, 0], hmove=[0, -0.16, 0.28], spine=[30, 18, 0], chest=[16, 10, 0], head=[-20, -8, 0],
                     hR=[0.04, -0.52, 0.60], dR=[0.7, -0.7, 0.0], hL=[0.14, -0.30, 0.20],
                     fL=[0.14, 0.09, 0.30, 0, 0], fR=[0.14, 0.09, -0.12, 0, 0]), "out"),
            (15, {}, "smooth"),
        ]
        out.append(make_clip("attack1", 16, from_keys(a1, 16, base=base), False))
        a2 = [  # a sweep from the right to the left
            (0, {}, "lin"),
            (5, dict(hips=[6, 30, 0], spine=[12, 30, 0], chest=[6, 20, 0], head=[-10, -14, 0], hR=[0.38, -0.05, -0.05], dR=[0.3, 0.1, -0.95],
                     hL=[0.14, -0.2, 0.2], fR=[0.2, 0.09, -0.2, 0, 24]), "smooth"),
            (8, dict(hips=[16, -36, 0], hmove=[0, -0.10, 0.22], spine=[20, -34, 0], chest=[10, -22, 0], head=[-16, 18, 0],
                     hR=[-0.30, -0.15, 0.50], dR=[0.4, -0.1, 0.9], hL=[0.2, -0.2, 0.1], fL=[0.14, 0.09, 0.26, 0, -20]), "in"),
            (10, dict(hips=[16, -40, 0], hmove=[0, -0.10, 0.22], spine=[20, -38, 0], chest=[10, -24, 0], head=[-16, 20, 0],
                     hR=[-0.36, -0.15, 0.46], dR=[0.25, -0.1, 1.0], hL=[0.2, -0.2, 0.1], fL=[0.14, 0.09, 0.26, 0, -20]), "out"),
            (17, {}, "smooth"),
        ]
        out.append(make_clip("attack2", 18, from_keys(a2, 18, base=base), False))
    else:
        a1 = [  # a long horizontal axe swing, the torch stays up
            (0, {}, "lin"),
            (6, dict(hips=[6, 38, 0], spine=[12, 34, 0], chest=[6, 22, 0], head=[-12, -16, 0], hR=[0.45, 0.0, -0.1], dR=[0.2, 0.1, -1.0],
                     fR=[0.22, 0.09, -0.24, 0, 30]), "smooth"),
            (10, dict(hips=[16, -44, 0], hmove=[0, -0.10, 0.26], spine=[20, -38, 0], chest=[10, -24, 0], head=[-16, 20, 0],
                      hR=[-0.40, -0.15, 0.55], dR=[0.3, -0.1, 0.95], fL=[0.14, 0.09, 0.30, 0, -20]), "in"),
            (12, dict(hips=[16, -48, 0], hmove=[0, -0.10, 0.26], spine=[20, -40, 0], chest=[10, -26, 0], head=[-16, 22, 0],
                      hR=[-0.46, -0.15, 0.46], dR=[0.15, -0.1, 1.0], fL=[0.14, 0.09, 0.30, 0, -20]), "out"),
            (19, {}, "smooth"),
        ]
        out.append(make_clip("attack1", 20, from_keys(a1, 20, base=base), False))
        a2 = [  # an overhead chop, slow, the whole body falls forward
            (0, {}, "lin"),
            (7, dict(hips=[-6, 4, 0], hmove=[0, -0.03, -0.05], spine=[2, 4, 0], chest=[-4, 3, 0], head=[-4, 0, 0],
                     hR=[0.12, 0.55, -0.02], dR=[-0.2, 1.0, -0.1]), "smooth"),
            (11, dict(hips=[28, 0, 0], hmove=[0, -0.22, 0.36], spine=[34, 0, 0], chest=[18, 0, 0], head=[-24, 0, 0],
                      hR=[0.06, -0.55, 0.60], dR=[0.8, -0.6, 0.0], fL=[0.14, 0.09, 0.40, 0, 0], fR=[0.14, 0.09, -0.20, 0, 0]), "in"),
            (14, dict(hips=[28, 0, 0], hmove=[0, -0.22, 0.36], spine=[34, 0, 0], chest=[18, 0, 0], head=[-24, 0, 0],
                      hR=[0.06, -0.55, 0.58], dR=[0.7, -0.7, 0.0], fL=[0.14, 0.09, 0.40, 0, 0], fR=[0.14, 0.09, -0.20, 0, 0]), "lin"),
            (21, {}, "smooth"),
        ]
        out.append(make_clip("attack2", 22, from_keys(a2, 22, base=base), False))
    hit = [  # a stagger backwards
        (0, {}, "lin"),
        (2, dict(hips=[-6, 0, 4], hmove=[0, -0.04, -0.12], spine=[-8, 0, 6], chest=[-8, 0, 3], head=[8, 0, -8],
                 hL=[0.3, -0.2, -0.05], hR=[0.3, -0.2, -0.05]), "out"),
        (5, dict(hips=[-4, 0, 3], hmove=[0, -0.06, -0.08], spine=[4, 0, 3], head=[-4, 0, -4]), "smooth"),
        (8, {}, "smooth"),
    ]
    out.append(make_clip("hit", 9, from_keys(hit, 9, base=base), False))
    death = [  # the knees give, the body falls on its back and lies there
        (0, {}, "lin"),
        (3, dict(hips=[-2, 0, 0], hmove=[0, -0.2, -0.05], spine=[4, 0, 0], head=[-4, 0, 0], fL=[0.12, 0.09, 0.0, 0, 0], fR=[0.12, 0.09, 0.0, 0, 0],
                 wL=[0.1, -0.5, 0.25], wR=[0.1, -0.5, 0.25]), "smooth"),
        (8, dict(hips=[-48, 0, 6], hmove=[0, -0.55, -0.35], spine=[-8, 0, 0], chest=[-6, 0, 0], neck=[0, 0, 0], head=[10, 0, 0], fw=1.0,
                 wL=[0.12, -0.7, 0.2], wR=[0.14, -0.7, 0.1], hL=[0.3, -0.2, -0.1], hR=[0.3, -0.2, -0.1], dL=[0.5, -0.5, 0.5], dR=[0.5, -0.5, 0.5]), "in"),
        (13, dict(hips=[-88, 0, 4], hmove=[0, -0.78, -0.55], spine=[0, 0, 0], chest=[0, 0, 0], neck=[0, 0, 0], head=[18, 0, 0], fw=1.0,
                  wL=[0.14, -0.78, 0.1], wR=[0.16, -0.76, 0.0], hL=[0.5, -0.1, -0.1], hR=[0.5, -0.1, -0.1], dL=[0.2, -0.3, 1.0], dR=[0.2, -0.3, -1.0],
                  gL=0.3, gR=0.3), "out"),
        (19, dict(hips=[-90, 0, 4], hmove=[0, -0.80, -0.55], spine=[0, 0, 0], chest=[0, 0, 0], neck=[0, 0, 0], head=[18, 0, 0], fw=1.0,
                  wL=[0.14, -0.78, 0.1], wR=[0.16, -0.76, 0.0], hL=[0.5, -0.1, -0.1], hR=[0.5, -0.1, -0.1], dL=[0.2, -0.3, 1.0], dR=[0.2, -0.3, -1.0],
                  gL=0.3, gR=0.3), "lin"),
    ]
    out.append(make_clip("death", 20, from_keys(death, 20, base=base), False))
    return out


# ---------------------------------------------------------------- the dog: local rotations, no IK

DOG_NAMES = ["hips", "spine", "chest", "neck", "head", "jaw", "tail1", "tail2", "fupper.L", "ffore.L", "fpaw.L", "hthigh.L", "hshin.L",
             "hfoot.L", "fupper.R", "ffore.R", "fpaw.R", "hthigh.R", "hshin.R", "hfoot.R"]


def dog_clip(name, frames, fn, loop=False):
    """fn(frame) -> {bone: (rx, ry, rz) degrees, "move": (x, y, z)}; every bone is written on every frame"""
    keys = []
    for f in range(frames):
        d = fn(f)
        pose = {}
        for n in DOG_NAMES:
            rot = d.get(n, (0, 0, 0))
            q = animgen.euler_quat(*rot)
            e = {"q": [round(x, 6) for x in q]}
            if n == "hips" and "move" in d:
                e["move"] = [round(x, 6) for x in d["move"]]
            pose[n] = e
        keys.append({"frame": f, "pose": pose})
    return {"name": name, "loop": loop, "frames": frames, "curve": "linear", "keys": keys}


def legs(d, front_phase, hind_phase, af, ah, lift_f, lift_h):
    """the four legs from two phases (0..1): the swing is a sine, the lower leg folds while the leg goes forward"""
    for side, pf, ph in (("L", front_phase, hind_phase), ("R", front_phase + 0.5, hind_phase + 0.5)):
        sf, sh = math.sin(2 * math.pi * pf), math.sin(2 * math.pi * ph)
        lf, lh = max(0.0, math.cos(2 * math.pi * pf)), max(0.0, math.cos(2 * math.pi * ph))
        d["fupper." + side] = (af * sf, 0, 0)
        d["ffore." + side] = (-lift_f * lf, 0, 0)
        d["fpaw." + side] = (0.4 * lift_f * lf - 0.2 * af * sf, 0, 0)
        d["hthigh." + side] = (ah * sh, 0, 0)
        d["hshin." + side] = (-lift_h * lh, 0, 0)
        d["hfoot." + side] = (0.6 * lift_h * lh - 0.25 * ah * sh, 0, 0)


def dog_idle(f, frames=24):
    p = f / float(frames)
    b = math.sin(2 * math.pi * p)
    d = {"move": (0, 0.004 * b, 0), "spine": (-1.2 * b, 0, 0), "chest": (1.5 * b, 0, 0), "head": (2 * math.sin(2 * math.pi * p * 2), 6 * math.sin(2 * math.pi * p), 0),
         "tail1": (0, 14 * math.sin(2 * math.pi * p * 2), 0), "tail2": (0, 20 * math.sin(2 * math.pi * p * 2 - 1), 0), "neck": (4, 0, 0)}
    return d


def dog_walk(f, frames=12):
    p = f / float(frames)
    d = {"move": (0, 0.014 * math.sin(4 * math.pi * p), 0), "spine": (0, 5 * math.sin(2 * math.pi * p), 0), "hips": (0, -4 * math.sin(2 * math.pi * p), 0),
         "chest": (0, 3 * math.sin(2 * math.pi * p), 0), "neck": (6, 8 * math.sin(2 * math.pi * p + 1), 0), "head": (-4, 0, 0),
         "tail1": (0, 10 * math.sin(2 * math.pi * p * 2), 0), "tail2": (0, 16 * math.sin(2 * math.pi * p * 2 - 1), 0)}
    legs(d, p, p, 24, 22, 38, 34)
    return d


def dog_run(f, frames=8):
    p = f / float(frames)
    s = math.sin(2 * math.pi * p)
    d = {"move": (0, 0.07 * abs(math.sin(2 * math.pi * p + 0.5)) - 0.03, 0), "hips": (6 * s, 0, 0), "spine": (-8 * s, 0, 0), "chest": (-5 * s, 0, 0),
         "neck": (-8, 0, 0), "head": (-6, 0, 0), "jaw": (-14, 0, 0), "tail1": (-10, 0, 0), "tail2": (-8, 0, 0)}
    # a gallop: the front legs together, the hind legs together, a little after
    for side in ("L", "R"):
        sf, sh = math.sin(2 * math.pi * p), math.sin(2 * math.pi * (p - 0.32))
        lf, lh = max(0.0, math.cos(2 * math.pi * p)), max(0.0, math.cos(2 * math.pi * (p - 0.32)))
        d["fupper." + side] = (44 * sf, 0, 0)
        d["ffore." + side] = (-48 * lf, 0, 0)
        d["fpaw." + side] = (20 * lf, 0, 0)
        d["hthigh." + side] = (44 * sh, 0, 0)
        d["hshin." + side] = (-44 * lh, 0, 0)
        d["hfoot." + side] = (26 * lh, 0, 0)
    return d


def dog_bite(f, frames=12):
    keys = {  # frame: (lunge forward, head pitch (+ up), jaw (- open), spine pitch)
        0: (0.0, 0, 0, 0), 3: (-0.05, 14, -4, -8), 5: (0.30, -22, -38, 14), 6: (0.34, -26, -40, 18), 8: (0.18, -6, -14, 6), 11: (0.0, 0, 0, 0)}
    ks = sorted(keys)
    for a, b in zip(ks, ks[1:]):
        if a <= f <= b:
            u = (f - a) / float(b - a)
            ua = u * u * (3 - 2 * u)
            v = [keys[a][i] + (keys[b][i] - keys[a][i]) * ua for i in range(4)]
            break
    lunge, hp, jaw, sp = v
    d = {"move": (0, -0.03 + 0.03 * abs(lunge), -lunge), "spine": (sp, 0, 0), "chest": (sp * 0.5, 0, 0), "neck": (hp * 0.5, 0, 0), "head": (hp * 0.6, 0, 0),
         "jaw": (jaw, 0, 0), "tail1": (0, 10 * math.sin(f), 0)}
    d["fupper.L"] = (lunge * 70, 0, 0)
    d["fupper.R"] = (lunge * 70, 0, 0)
    d["ffore.L"] = (-lunge * 40, 0, 0)
    d["ffore.R"] = (-lunge * 40, 0, 0)
    d["hthigh.L"] = (-lunge * 40, 0, 0)
    d["hthigh.R"] = (-lunge * 40, 0, 0)
    return d


def dog_leap(f, frames=16):
    keys = {  # frame: (up, forward, pitch of the body (+ nose up), front legs, hind legs, jaw)
        0: (0.0, 0.0, 0, 0, 0, 0), 3: (-0.18, -0.05, 10, -20, 30, 0), 5: (0.05, 0.1, -10, 40, -60, -20), 8: (0.55, 0.9, -24, 55, -45, -34),
        11: (0.45, 1.4, -8, 30, -20, -20), 13: (0.05, 1.6, 6, 10, 10, -4), 15: (0.0, 1.6, 0, 0, 0, 0)}
    ks = sorted(keys)
    v = keys[ks[-1]]
    for a, b in zip(ks, ks[1:]):
        if a <= f <= b:
            u = (f - a) / float(b - a)
            ua = u * u * (3 - 2 * u)
            v = [keys[a][i] + (keys[b][i] - keys[a][i]) * ua for i in range(6)]
            break
    up, fw, pitch, fl, hl, jaw = v
    d = {"move": (0, up, -fw * 0.0), "hips": (-pitch, 0, 0), "spine": (-pitch * 0.4, 0, 0), "neck": (-pitch * 0.3, 0, 0), "jaw": (jaw, 0, 0)}
    for side in ("L", "R"):
        d["fupper." + side] = (fl, 0, 0)
        d["ffore." + side] = (-abs(fl) * 0.6, 0, 0)
        d["hthigh." + side] = (hl, 0, 0)
        d["hshin." + side] = (-abs(hl) * 0.7, 0, 0)
        d["hfoot." + side] = (abs(hl) * 0.4, 0, 0)
    return d


def dog_hit(f, frames=8):
    s = math.sin(math.pi * min(1.0, f / 5.0)) if f < 5 else 0.0
    return {"move": (0, -0.03 * s, 0.12 * s), "hips": (0, 0, 6 * s), "spine": (10 * s, 0, 8 * s), "chest": (8 * s, 0, 0), "neck": (14 * s, 0, 0),
            "head": (18 * s, 0, -10 * s), "jaw": (-24 * s, 0, 0), "tail1": (-20 * s, 0, 0)}


def dog_death(f, frames=16):
    keys = {0: 0.0, 3: 0.25, 7: 0.75, 11: 1.0, 15: 1.0}
    ks = sorted(keys)
    k = 1.0
    for a, b in zip(ks, ks[1:]):
        if a <= f <= b:
            u = (f - a) / float(b - a)
            k = keys[a] + (keys[b] - keys[a]) * (u * u * (3 - 2 * u))
            break
    d = {"move": (0, -0.58 * k, 0), "hips": (0, 0, -84 * k), "neck": (0, 0, 10 * k), "head": (-8 * k, 0, 10 * k), "jaw": (-18 * k, 0, 0),
         "tail1": (0, 20 * k, 0)}
    for side, sx in (("L", 1), ("R", -1)):
        d["fupper." + side] = (30 * k, 0, 0)
        d["ffore." + side] = (-14 * k, 0, 0)
        d["hthigh." + side] = (-26 * k, 0, 0)
        d["hshin." + side] = (-16 * k, 0, 0)
    return d


def dog_clips():
    return [dog_clip("idle", 24, dog_idle, True), dog_clip("walk", 12, dog_walk, True), dog_clip("run", 8, dog_run, True),
            dog_clip("bite", 12, dog_bite), dog_clip("leap", 16, dog_leap), dog_clip("hit", 8, dog_hit), dog_clip("death", 16, dog_death)]


if __name__ == "__main__":
    for k in ("villager", "huntsman"):
        print(k, [c["name"] for c in man_clips(k)])
    print("dog", [c["name"] for c in dog_clips()])
