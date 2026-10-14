#!/usr/bin/env python3
"""Experiment: a smoother Pico (ellipsoids, not boxes) written as an untextured .glb for bm-forge.

  python video/3d/assets/smooth/gen_glb.py N OUT.glb     N = iteration 1, 2 or 3

Built in bm space (y up, Pico faces +z, ground y = 0, about 1.2 tall like pico_boxes.json), then
z is negated for glTF (the forge glb reader flips it back). One glTF primitive and one material
per body part, so each part is one flat colour. Not part of the series: it only tests bm-forge.
"""
import json
import math
import struct
import sys


def ellipsoid(c, r, segs, rings, col, tilt=0.0, yaw=0.0, cut=None):
    """tris of an ellipsoid at c with radii r; tilt turns about x, yaw about y (radians)."""
    if max(r) < 0.11:
        segs, rings = min(segs, 6), min(rings, 4)
    pts = []
    for i in range(rings + 1):
        th = math.pi * i / rings
        for j in range(segs):
            ph = 2 * math.pi * j / segs
            pts.append((math.sin(th) * math.cos(ph) * r[0], math.cos(th) * r[1], math.sin(th) * math.sin(ph) * r[2]))
    def rot(p):
        x, y, z = p
        y, z = y * math.cos(tilt) - z * math.sin(tilt), y * math.sin(tilt) + z * math.cos(tilt)
        x, z = x * math.cos(yaw) + z * math.sin(yaw), -x * math.sin(yaw) + z * math.cos(yaw)
        return (x + c[0], y + c[1], z + c[2])
    pts = [rot(p) for p in pts]
    tris = []
    for i in range(rings):
        for j in range(segs):
            a = i * segs + j
            b = i * segs + (j + 1) % segs
            c2 = (i + 1) * segs + j
            d = (i + 1) * segs + (j + 1) % segs
            if i > 0:
                tris.append((a, c2, b))
            if i < rings - 1:
                tris.append((b, c2, d))
    return pts, tris, col


def capsule_chain(a, b, ra, rb, segs, rings, col, n=2, f=0.75):
    """a few ellipsoids along a->b with radius ra->rb: a soft limb"""
    out = []
    for k in range(n):
        t = (k + 0.5) / n
        c = tuple(a[i] + (b[i] - a[i]) * t for i in range(3))
        rr = ra + (rb - ra) * t
        ln = math.dist(a, b) / n * f
        # long axis along the limb: build with the axis as the ellipsoid's y then orient
        dx, dy, dz = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        tilt = math.atan2(dz, -dy) if abs(dy) + abs(dz) > 1e-9 else 0
        out.append(ellipsoid(c, (rr, max(ln, rr), rr), segs, rings, col, tilt=tilt))
    return out


MINT, MINT2, BELLY, ORANGE, WHITE, DARK, YELLOW = (0.56, 0.90, 0.76), (0.48, 0.85, 0.71), (1.0, 0.95, 0.78), (1.0, 0.54, 0.16), (1, 1, 1), (0.06, 0.06, 0.1), (1.0, 0.82, 0.24)


def parts(it):
    s, r = (8, 5) if it == 1 else (10, 6) if it == 2 else (12, 7)
    P = []
    # body, tilted a little forward, belly patch on the front (+z)
    P.append(ellipsoid((0, 0.62, -0.02), (0.26, 0.30, 0.22), s, r, MINT, tilt=0.12))
    P.append(ellipsoid((0, 0.60, 0.10), (0.20, 0.24, 0.14), s, r, BELLY, tilt=0.12))
    # head with a snout
    P.append(ellipsoid((0, 1.02, 0.04), (0.25, 0.20, 0.24), s, r, MINT2))
    P.append(ellipsoid((0, 0.97, 0.26), (0.16, 0.10, 0.16), s, r, MINT2, tilt=-0.15))
    # eyes: white ball + dark pupil on the top-front of the head
    for sx in (-1, 1):
        P.append(ellipsoid((sx * 0.14, 1.14, 0.14), (0.09, 0.09, 0.09), s, r, WHITE))
        P.append(ellipsoid((sx * 0.15, 1.15, 0.22), (0.04, 0.05, 0.03), 6, 4, DARK))
    # arms
    for sx in (-1, 1):
        P += capsule_chain((sx * 0.24, 0.72, 0.02), (sx * 0.34, 0.50, 0.10), 0.07, 0.06, s, r, MINT2, 2)
    # legs and orange shoes
    for sx in (-1, 1):
        P += capsule_chain((sx * 0.13, 0.38, 0.0), (sx * 0.13, 0.10, 0.02), 0.09, 0.07, s, r, MINT, 2)
        P.append(ellipsoid((sx * 0.13, 0.05, 0.08), (0.10, 0.05, 0.16), s, r, ORANGE))
    # tail: three tapering segments going back (-z) and up, yellow tip
    P.append(ellipsoid((0, 0.40, -0.30), (0.14, 0.13, 0.20), s, r, MINT, tilt=0.5))
    P.append(ellipsoid((0, 0.34, -0.58), (0.09, 0.09, 0.18), s, r, MINT, tilt=0.5))
    P.append(ellipsoid((0, 0.36, -0.80), (0.055, 0.055, 0.14), s, r, YELLOW, tilt=0.5))
    if it >= 2:
        # rounder cheeks, a mouth line, toes
        for sx in (-1, 1):
            P.append(ellipsoid((sx * 0.20, 0.97, 0.12), (0.08, 0.08, 0.08), s, r, MINT2))
        P.append(ellipsoid((0, 0.93, 0.36), (0.11, 0.02, 0.05), 8, 4, DARK))
    if it >= 3:
        # back spikes (little ridge), tail ridge, hands
        for k, (y, z, rr) in enumerate([(0.88, -0.10, 0.045), (0.76, -0.18, 0.05), (0.62, -0.24, 0.05)]):
            P.append(ellipsoid((0, y, z), (rr, rr * 1.2, rr), 8, 5, MINT2))
        for sx in (-1, 1):
            P.append(ellipsoid((sx * 0.35, 0.46, 0.13), (0.06, 0.06, 0.06), 8, 5, BELLY))
    return P

def parts2(it):
    """iterations 2 and 3: connected limbs and tail, rounder head, smaller eyes"""
    s, r = (10, 6) if it == 2 else (12, 7)
    LIMB = (3, 0.75) if it == 2 else (2, 1.25)
    P = []
    P.append(ellipsoid((0, 0.62, -0.02), (0.27, 0.31, 0.23), s, r, MINT, tilt=0.12))
    P.append(ellipsoid((0, 0.58, 0.04), (0.19, 0.25, 0.17), s, r, BELLY, tilt=0.12))
    P.append(ellipsoid((0, 1.02, 0.04), (0.28, 0.23, 0.26), s, r, MINT2))
    P.append(ellipsoid((0, 0.96, 0.26), (0.15, 0.10, 0.14), s, r, MINT2, tilt=-0.1))
    for sx in (-1, 1):
        P.append(ellipsoid((sx * 0.13, 1.14, 0.15), (0.075, 0.075, 0.075), s, r, WHITE))
        P.append(ellipsoid((sx * 0.135, 1.14, 0.215), (0.035, 0.045, 0.03), 6, 4, DARK))
        P += capsule_chain((sx * 0.22, 0.72, 0.02), (sx * 0.36, 0.50, 0.10), 0.085, 0.065, s, r, MINT2, *LIMB)
        P += capsule_chain((sx * 0.13, 0.40, 0.0), (sx * 0.13, 0.12, 0.02), 0.12, 0.085, s, r, MINT, *LIMB)
        P.append(ellipsoid((sx * 0.13, 0.06, 0.07), (0.115, 0.06, 0.18), s, r, ORANGE))
    P.append(ellipsoid((0, 0.40, -0.26), (0.17, 0.15, 0.24), s, r, MINT, tilt=0.45))
    P.append(ellipsoid((0, 0.42, -0.50), (0.12, 0.11, 0.22), s, r, MINT, tilt=0.5))
    P.append(ellipsoid((0, 0.47, -0.70), (0.075, 0.075, 0.18) if it == 2 else (0.06, 0.06, 0.26), s, r, YELLOW, tilt=0.55))
    if it >= 3:
        for sx in (-1, 1):
            P.append(ellipsoid((sx * 0.22, 0.98, 0.12), (0.09, 0.08, 0.09), s, r, MINT2))
            P.append(ellipsoid((sx * 0.37, 0.47, 0.12), (0.07, 0.07, 0.07), 8, 5, BELLY))
        P.append(ellipsoid((0, 0.915, 0.37), (0.10, 0.018, 0.04), 8, 4, DARK))
        for y, z, rr in [(0.90, -0.12, 0.05), (0.78, -0.20, 0.055), (0.62, -0.27, 0.055)]:
            P.append(ellipsoid((0, y, z), (rr, rr * 1.3, rr), 8, 5, MINT2))
    return P


def write_glb(P, path, name="PICO_SOFT"):
    pos, idx, prims, mats = bytearray(), bytearray(), [], []
    vcount = 0
    views, accs = [], []
    for k, (pts, tris, col) in enumerate(P):
        p = [(x, y, -z) for x, y, z in pts]            # bm z -> glTF z
        t = [(a, c, b) for a, b, c in tris]            # mirrored: reverse winding
        pb = b"".join(struct.pack("<3f", *q) for q in p)
        ib = b"".join(struct.pack("<3I", *q) for q in t)
        lo = [min(q[i] for q in p) for i in range(3)]
        hi = [max(q[i] for q in p) for i in range(3)]
        o1 = len(pos); pos += pb
        o2 = len(idx); idx += ib
        views.append(("pos", o1, len(pb))); views.append(("idx", o2, len(ib)))
        accs.append({"bufferView": 2 * k, "componentType": 5126, "count": len(p), "type": "VEC3", "min": lo, "max": hi})
        accs.append({"bufferView": 2 * k + 1, "componentType": 5125, "count": len(t) * 3, "type": "SCALAR"})
        mats.append({"pbrMetallicRoughness": {"baseColorFactor": [*(c ** 2.2 for c in col), 1.0], "metallicFactor": 0, "roughnessFactor": 1}})
        prims.append({"attributes": {"POSITION": 2 * k}, "indices": 2 * k + 1, "material": k, "mode": 4})
    # one buffer: all positions, then all indices
    binb = bytes(pos) + bytes(idx)
    bvs = []
    for kind, off, ln in views:
        bvs.append({"buffer": 0, "byteOffset": off if kind == "pos" else len(pos) + off, "byteLength": ln})
    js = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": name}],
          "meshes": [{"name": name, "primitives": prims}], "materials": mats, "accessors": accs, "bufferViews": bvs,
          "buffers": [{"byteLength": len(binb)}]}
    j = json.dumps(js, separators=(",", ":")).encode()
    j += b" " * (-len(j) % 4)
    binb += b"\0" * (-len(binb) % 4)
    out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(j) + 8 + len(binb))
    out += struct.pack("<II", len(j), 0x4E4F534A) + j + struct.pack("<II", len(binb), 0x004E4942) + binb
    open(path, "wb").write(out)
    return sum(len(t) for _, t, _ in P)


if __name__ == "__main__":
    n = int(sys.argv[1])
    print("triangles:", write_glb(parts(n) if n == 1 else parts2(n), sys.argv[2]))
