#!/usr/bin/env python3
"""Checks the project bm Animator saved in the recording (no pictures: the file is what the editor really wrote).

  python3 video/3d/03-animator/verify.py [PROJECT.bme|.bm] [--start START.bm]

PROJECT defaults to video/3d/chain/03-animator.bme (else out/sd/carts/TROPIC.BME). START is the cartridge the episode began with
(default: chain/02-pixel.bm when it exists, else the stand-in video/3d/out/assets/models.bm): the MESH must be the same.
Reads with scripts/bmres.py and bm_core (bm-forge), runs `bm_forge.py check` and compares the key poses with the reference
clips of video/3d/assets (tropic_assets.bm, the output of video/3d/assets/build.py).
Exit 0 only if everything passes. The numbers of the checks are the answers of docs/video/03-animator/*: PIANO.md section 2
(the nine bones, the five clips, 300 triangles), the lengths of the clips (12 frames a second), loop on in idle and run only.
"""
import math
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
sys.path.insert(0, os.path.join(ROOT, ".claude", "skills", "bm-forge"))
import bmres  # noqa: E402
from bm_core import container  # noqa: E402

FORGE = os.path.join(ROOT, ".claude", "skills", "bm-forge", "scripts", "bm_forge.py")
CHAIN = os.path.join(ROOT, "video", "3d", "chain")
ASSETS = os.path.join(ROOT, "video", "3d", "out", "assets", "tropic_assets.bm")

BONES = ["root", "spine", "head", "tail1", "tail2", "arm.L", "arm.R", "leg.L", "leg.R"]   # PIANO.md section 2 (any order)
PARENTS = {"root": None, "spine": "root", "head": "spine", "tail1": "root", "tail2": "tail1",
           "arm.L": "spine", "arm.R": "spine", "leg.L": "root", "leg.R": "root"}
CLIPS = ["idle", "run", "jump", "spin", "hit"]
LOOP = {"idle": True, "run": True, "jump": False, "spin": False, "hit": False}
FRAMES = {"idle": 24, "run": 8, "jump": 12, "spin": 6, "hit": 6}        # 12 frames a second; spin: 3 keys of 120 degrees
# the bones that tell a clip, and the ones whose direction (sign) must agree with the reference (Pico faces +z)
KEY_BONES = {"idle": ["spine"], "run": ["arm.L", "arm.R", "leg.L", "leg.R"], "jump": ["arm.L", "arm.R", "leg.L", "leg.R", "spine"],
             "spin": ["root"], "hit": ["spine", "head"]}
SIGN_BONES = {"idle": [], "run": ["leg.L", "leg.R"], "jump": ["arm.L", "arm.R", "leg.L", "leg.R"], "spin": ["root"], "hit": ["spine", "head"]}

results = []


def check(ok, what, detail=""):
    results.append(ok)
    print("%-4s %s%s" % ("ok" if ok else "FAIL", what, ("  (" + detail + ")") if detail else ""))
    return ok


# --------------------------------------------------------------------- quaternions (x, y, z, w)
def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def qconj(a):
    return (-a[0], -a[1], -a[2], a[3])


def signed_angles(q):
    """The rotation as (degrees about x, y, z) of its axis-angle, the shortest way (angle 0..180)."""
    x, y, z, w = q
    if w < 0:
        x, y, z, w = -x, -y, -z, -w
    n = math.sqrt(x * x + y * y + z * z)
    ang = 2 * math.atan2(n, w)
    if n < 1e-9:
        return 0.0, 0.0, 0.0
    return tuple(math.degrees(ang) * c / n for c in (x, y, z))


def angle(q):
    return math.sqrt(sum(a * a for a in signed_angles(q)))


def amplitude(rig, clip, bone):
    """(largest rotation in degrees over the keys, its signed angles about x, y, z)"""
    i = [b.name for b in rig.bones].index(bone)
    best = (0.0, (0.0, 0.0, 0.0))
    for k in clip.keys:
        s = signed_angles(tuple(k.pose[i].q))
        a = math.sqrt(sum(v * v for v in s))
        if a > best[0] + 1e-6:
            best = (a, s)
    return best


def turn_about_y(rig, clip, bone):
    """total degrees about y when the keys are played in order (each step the shortest way, as the engine's slerp)."""
    i = [b.name for b in rig.bones].index(bone)
    total, off_axis = 0.0, 0.0
    for a, b in zip(clip.keys, clip.keys[1:]):
        rel = qmul(tuple(b.pose[i].q), qconj(tuple(a.pose[i].q)))
        sx, sy, sz = signed_angles(rel)
        total += sy
        off_axis = max(off_axis, abs(sx), abs(sz))
    return total, off_axis


def _geometry(model):
    """The triangles of a model as (corner positions, colour, texture corners), whatever the vertex numbering."""
    out = []
    for f in model.faces:
        pts = [tuple(round(c, 5) for c in model.verts[i]) for i in (f.a, f.b, f.c)]
        k = pts.index(min(pts))
        out.append((tuple(pts[k:] + pts[:k]), f.colour, tuple(f.uv8)))
    return sorted(out)


# --------------------------------------------------------------------- main
def main():
    args = sys.argv[1:]
    start = None
    if "--start" in args:
        i = args.index("--start")
        start = args[i + 1]
        del args[i:i + 2]
    path = args[0] if args else None
    if path is None:
        for c in (os.path.join(CHAIN, "03-animator.bme"), os.path.join(HERE, "out", "sd", "carts", "TROPIC.BME")):
            if os.path.exists(c):
                path = c
                break
    if not path or not os.path.exists(path):
        print("no project to check (run record.py first):", path)
        return 1
    if start is None:
        start = os.path.join(CHAIN, "02-pixel.bm")
        if not os.path.exists(start):
            start = os.path.join(ROOT, "video", "3d", "out", "assets", "models.bm")
    print("project:", os.path.relpath(path, ROOT), "  start:", os.path.relpath(start, ROOT))

    f = bmres.read(path)
    try:
        bmres.check(f)
        check(True, "bmres: the file reads and every section is allowed")
    except Exception as e:                                   # noqa: BLE001
        check(False, "bmres check", str(e))
    cart = container.read_file(path)
    mesh, anim = cart.mesh(), cart.anim()
    pico = mesh.get("PICO")
    check(pico is not None and mesh.get("APPLE") is not None and mesh.get("CRATE") is not None, "models PICO, APPLE, CRATE are still there")
    check(len(pico.faces) <= 300, "PICO has at most 300 triangles (PIANO.md section 2)", "%d" % len(pico.faces))
    rig = anim.get("PICO") if anim else None
    if not check(rig is not None, "PICO has a skeleton"):
        return summary()
    check(anim.get("APPLE") is None and anim.get("CRATE") is None, "APPLE and CRATE have no skeleton (only Pico is rigged)")

    # ---- the mesh is the one we started from
    s0 = container.read_file(start)
    same_bytes = bmres.read(start).get(bmres.SEC_MESH) == f.get(bmres.SEC_MESH)
    m0 = s0.mesh()
    check([m.name for m in m0.models] == [m.name for m in mesh.models] and
          all(_geometry(a) == _geometry(b) for a, b in zip(m0.models, mesh.models)),
          "the MESH section is the one the episode started from (same triangles, positions and colours)",
          "byte for byte" if same_bytes else "re-encoded: the skin splits the vertices of PICO (%d -> %d), same geometry" %
          (len(m0.get("PICO").verts), len(pico.verts)))

    # ---- bones
    names = [b.name for b in rig.bones]
    check(len(names) == 9 and sorted(names) == sorted(BONES), "9 bones with the names of PIANO.md section 2", " ".join(names))
    pn = {b.name: (rig.bones[b.parent].name if b.parent >= 0 else None) for b in rig.bones}
    check(pn == PARENTS, "the parents: head, arms on spine; legs, tail1, spine on root; tail2 on tail1", str(pn) if pn != PARENTS else "")
    check(all(len(n) <= 16 for n in names) and len(set(names)) == 9, "names are unique and at most 16 letters")
    lo = [min(v[k] for v in pico.verts) for k in range(3)]
    hi = [max(v[k] for v in pico.verts) for k in range(3)]
    inside = all(lo[k] - 0.3 <= c <= hi[k] + 0.3 for b in rig.bones for e in (b.head, b.tail) for k, c in enumerate(e))
    check(inside, "every bone lies on the model (inside its box plus 0.3)")
    ref = container.read_file(ASSETS if os.path.exists(ASSETS) else _build_assets()).anim().get("PICO")
    rt = {b.name: b.tail for b in ref.bones}
    far = [(b.name, [round(c, 2) for c in b.tail]) for b in rig.bones
           if b.name in rt and math.dist(b.tail, rt[b.name]) > 0.35]
    check(not far, "each bone's tail is near the reference skeleton's (0.35)", str(far) if far else "")

    # ---- skin
    used = {rig.bones[i].name for i in rig.vbones}
    check(len(used) >= 8, "the skin uses the bones (not all the faces on root)", "%d of 9: %s" % (len(used), " ".join(sorted(used))))
    vb = lambda cond: {rig.bones[rig.vbones[i]].name for i, v in enumerate(pico.verts) if cond(v)}   # noqa: E731
    check(vb(lambda v: v[0] >= 0.3 and v[1] <= 0.6) == {"arm.L"}, "the left hand follows arm.L", str(vb(lambda v: v[0] >= 0.3 and v[1] <= 0.6)))
    check(vb(lambda v: v[0] <= -0.3 and v[1] <= 0.6) == {"arm.R"}, "the right hand follows arm.R")
    check(vb(lambda v: v[1] >= 1.15) == {"head"}, "the top of the head follows head")
    check(vb(lambda v: v[2] <= -0.8) == {"tail2"}, "the tip of the tail follows tail2")

    # ---- clips
    clips = {c.name: c for c in rig.clips}
    check([c.name for c in rig.clips] == CLIPS, "5 clips: idle, run, jump, spin, hit (in this order)", " ".join(c.name for c in rig.clips))
    for n in CLIPS:
        c = clips.get(n)
        if c is None:
            check(False, "clip " + n)
            continue
        fr = round(c.length * 12, 2)
        check(c.loop == LOOP[n], "%s: loop %s" % (n, "on" if LOOP[n] else "off"), "loop=%s" % c.loop)
        check(abs(fr - FRAMES[n]) < 0.01, "%s: %d frames" % (n, FRAMES[n]), "%s frames" % fr)
        times = [k.time for k in c.keys]
        check(len(c.keys) >= 2 and times == sorted(times) and times[-1] <= c.length + 1e-6,
              "%s: %d keyframes in order, inside the length" % (n, len(c.keys)))
        rest = all(angle(tuple(p.q)) < 0.5 and max(abs(t) for t in p.t) < 1e-4 for k in c.keys for p in k.pose)
        check(not rest, "%s: not an empty clip" % n)

    # ---- spin
    sp = clips.get("spin")
    if sp:
        tot, off = turn_about_y(rig, sp, "root")
        check(abs(tot) >= 330 and off < 15, "spin: root turns %.0f degrees about y (a full circle: at least 330)" % abs(tot),
              "off-axis %.1f" % off)

    # ---- key poses against the reference clips (a wide window: the Animator poses by hand)
    rc = {c.name: c for c in ref.clips}
    for n in CLIPS:
        if n not in clips or n not in rc:
            continue
        bad = []
        for b in KEY_BONES[n]:
            a_me, s_me = amplitude(rig, clips[n], b)
            a_rf, s_rf = amplitude(ref, rc[n], b)
            lo_a, hi_a = (0.4 * a_rf, 2.5 * a_rf) if a_rf >= 10 else (0.0, a_rf + 20)
            if not lo_a <= a_me <= hi_a:
                bad.append("%s %.0f deg vs ref %.0f" % (b, a_me, a_rf))
            if b in SIGN_BONES[n]:
                ax = max(range(3), key=lambda k: abs(s_rf[k]))
                if s_me[ax] * s_rf[ax] <= 0:
                    bad.append("%s turns the other way about %s (%.0f vs ref %.0f)" % (b, "xyz"[ax], s_me[ax], s_rf[ax]))
        check(not bad, "%s: key poses near the reference clip (%s)" % (n, ", ".join(KEY_BONES[n])), "; ".join(bad))
    if "run" in clips:
        l, r = amplitude(rig, clips["run"], "leg.L")[1][0], amplitude(rig, clips["run"], "leg.R")[1][0]
        check(l * r < 0, "run: the legs swing in opposition")
        a, b = amplitude(rig, clips["run"], "arm.L")[1][0], amplitude(rig, clips["run"], "arm.R")[1][0]
        check(a * b < 0 and a * l < 0, "run: the arms swing in opposition, against the legs")

    # ---- the forge's own check (budget of the hero)
    r = subprocess.run([sys.executable, FORGE, "check", path, "--budget", "300", "--draw", "PICO=1", "--no-sync"],
                       capture_output=True, text=True)
    last = [ln for ln in r.stdout.splitlines() if ln.strip()][-3:]
    check(r.returncode == 0 and "9 bones" in r.stdout, "bm_forge check: file, rig and the budget of 300 triangles", " | ".join(x.strip() for x in last))
    return summary()


def _build_assets():
    subprocess.run([sys.executable, os.path.join(ROOT, "video", "3d", "assets", "build.py")], check=True, stdout=subprocess.DEVNULL)
    return ASSETS


def summary():
    bad = results.count(False)
    print("\n%s: %d checks, %d failed" % ("PASS" if not bad else "FAIL", len(results), bad))
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
