#!/usr/bin/env python3
"""Checks the project bm Studio saved in the recording (out/sd/carts/TROPIC.BME) against the answer of video 1.
No pictures: the file is what the editor really wrote (scripts/bmres.py, scripts/bmmesh.py).

  python3 video/3d/01-studio/verify.py [SD_DIR] [--reference] [--chain]

  - the file opens (bmres.read: CRC, sections, MESH well formed) and is a project (.bme)
  - three models, named CRATE, APPLE, PICO in this order, flat colours only (no texture)
  - triangles inside the budgets of PIANO.md section 2: PICO <= 300, APPLE <= 80, CRATE <= 40 (and < 1200 in all)
  - CRATE is one cube (12 triangles), APPLE has its dimple (the top face lower at the middle) and 80 triangles at most
  - PICO faces +z as in video/3d/assets: its cream belly looks to +z, its tail (yellow tip) lies at -z, 16 blocks of the
    six colours, 3 blocks wide, about 4 high
  - --reference: the silhouette against video/3d/out/assets (bm_forge check --reference, front view), IoU printed
  - --chain: video/3d/chain/01-studio.bme and .bm exist and hold the same three models
Exit 0 only when all pass.
"""
import json
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmmesh   # noqa: E402
import bmres    # noqa: E402

BUDGET = {"PICO": 300, "APPLE": 80, "CRATE": 40}
ORDER = ["CRATE", "APPLE", "PICO"]
FORGE = os.path.join(ROOT, ".claude", "skills", "bm-forge", "scripts", "bm_forge.py")
CHAIN = os.path.join(HERE, "..", "chain")
# T.PALETTE of bm Studio (src/script/bm3d.lua): the colours the video picks
COL = {"brown": 0x8A5A30, "red": 0xE84A5A, "orange": 0xF09030, "mint": 0x60D0C0, "yellow": 0xF0D040, "cream": 0xFFF4C0}

bad = []


def check(ok, text):
    print(("ok   " if ok else "FAIL ") + text)
    if not ok:
        bad.append(text)
    return ok


def rgb(c):
    return c & 0xFFFFFF


def load_models(path):
    f = bmres.read(path)
    body = f.get(bmres.SEC_MESH)
    if body is None:
        raise SystemExit("FAIL no MESH section in " + path)
    models, inset = bmmesh.decode(body)
    return f, models


def tri_normal(m, face):
    a, b, c = (m["verts"][i] for i in face[:3])
    u = [b[k] - a[k] for k in range(3)]
    v = [c[k] - a[k] for k in range(3)]
    n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
    ln = sum(x * x for x in n) ** 0.5 or 1
    return [x / ln for x in n]


def centre(m, face):
    return [sum(m["verts"][i][k] for i in face[:3]) / 3 for k in range(3)]


def bbox(m):
    return [(min(v[k] for v in m["verts"]), max(v[k] for v in m["verts"])) for k in range(3)]


def faces_of(m, colour):
    return [f for f in m["faces"] if rgb(f[3]) == colour]


def verify_models(path, models):
    names = [m["name"] for m in models]
    check(names == ORDER, "models %s (want %s, in this order)" % (names, ORDER))
    by = {m["name"]: m for m in models}
    total = sum(len(m["faces"]) for m in models)
    for n in ORDER:
        m = by.get(n)
        if not m:
            check(False, n + " is missing")
            continue
        tris = len(m["faces"])
        check(tris <= BUDGET[n], "%s: %d triangles, %d vertices (budget %d)" % (n, tris, len(m["verts"]), BUDGET[n]))
        flat = sum(1 for f in m["faces"] if rgb(f[3]) != 0)
        check(flat == tris, "%s: %d of %d triangles are flat colours (no texture)" % (n, flat, tris))
    check(total < 1200, "scene total %d triangles (< 1200, Studio's own limit)" % total)
    if "CRATE" in by:
        c = by["CRATE"]
        check(len(c["faces"]) == 12 and len(c["verts"]) == 8, "CRATE is one cube: 12 triangles, 8 vertices")
        check({rgb(f[3]) for f in c["faces"]} == {COL["brown"]}, "CRATE is brown")
    if "APPLE" in by:
        a = by["APPLE"]
        cols = {rgb(f[3]) for f in a["faces"]}
        check(cols == {COL["red"]}, "APPLE is red (colours %s)" % sorted("%06X" % c for c in cols))
        (x0, x1), (y0, y1), (z0, z1) = bbox(a)
        check(abs((x1 - x0) - 3) < 0.01 and abs((z1 - z0) - 3) < 0.01 and abs((y1 - y0) - 3) < 0.01,
              "APPLE is 3 x 3 x 3 blocks (%.2f x %.2f x %.2f)" % (x1 - x0, y1 - y0, z1 - z0))
        # the dimple: some top vertex is lower than the top of the model (the four corners went down a quarter)
        tops = sorted({round(v[1], 3) for v in a["verts"]})
        check(any(abs(t - (y1 - 0.25)) < 0.01 for t in tops), "APPLE has a dimple (a corner a quarter below the top %.2f)" % y1)
    if "PICO" in by:
        p = by["PICO"]
        cols = {rgb(f[3]) for f in p["faces"]}
        check(cols == set(COL[k] for k in ("orange", "mint", "yellow", "cream")),
              "PICO colours: orange legs, mint body, yellow tail tip, cream belly (%s)" % sorted("%06X" % c for c in cols))
        (x0, x1), (y0, y1), (z0, z1) = bbox(p)
        check(abs((x1 - x0) - 5) < 0.01, "PICO is 5 wide with the arms (%.2f)" % (x1 - x0))
        check(abs((y1 - y0) - 4) < 0.6, "PICO is about 4 high (%.2f)" % (y1 - y0))
        check(z0 <= -2.9, "PICO's tail reaches z = %.2f (about -3)" % z0)
        cream = faces_of(p, COL["cream"])
        yellow = faces_of(p, COL["yellow"])
        if cream and yellow:
            nz = sum(tri_normal(p, f)[2] for f in cream) / len(cream)
            cz = sum(centre(p, f)[2] for f in cream) / len(cream)
            yz = sum(centre(p, f)[2] for f in yellow) / len(yellow)
            check(nz > 0.9, "PICO faces +z: the cream belly looks to +z (mean normal z = %.2f)" % nz)
            check(yz < cz - 1.5, "PICO's tail is behind it: yellow tip z = %.2f, belly z = %.2f" % (yz, cz))
        else:
            check(False, "PICO has a cream belly and a yellow tail tip")
        # the tip of the tail was lifted by the selection move: its end faces are higher than the tail's other blocks
        ys = [p["verts"][i][1] for f in faces_of(p, COL["yellow"]) for i in f[:3]]
        if ys:
            check(max(ys) > 2.2, "the tail tip was lifted (a yellow corner at y = %.2f, the block top is 2.00)" % max(ys))
    return by


def silhouette(path):
    """bm_forge: the silhouette of PICO against the reference of video/3d/assets (the answer, never shown)."""
    ref = os.path.join(ROOT, "video", "3d", "out", "assets", "tropic_assets.bm")
    if not os.path.exists(ref):
        print("skip silhouette: run python3 video/3d/assets/build.py first")
        return
    for view in ("front", "side"):
        p = subprocess.run([sys.executable, FORGE, "check", path, "--no-sync", "--model", "PICO", "--reference", ref,
                            "--ref-model", "PICO", "--view", view, "--min-iou", "0.0", "--json"],
                           capture_output=True, text=True, cwd=ROOT)
        txt = (p.stdout or p.stderr).strip()
        iou = "?"
        try:
            def find(o):
                if isinstance(o, dict):
                    for k, v in o.items():
                        if "iou" in k.lower() and isinstance(v, (int, float)):
                            return v
                        r = find(v)
                        if r is not None:
                            return r
                elif isinstance(o, list):
                    for v in o:
                        r = find(v)
                        if r is not None:
                            return r
            iou = find(json.loads(txt))
        except ValueError:
            pass
        print("silhouette %s vs reference PICO: IoU %s (information: hand-built blocks differ from the reference on purpose)" % (view, iou))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    sd = args[0] if args else os.path.join(HERE, "out", "sd")
    path = os.path.join(sd, "carts", "TROPIC.BME")
    if not os.path.exists(path):
        print("FAIL no", path, "- files:", os.listdir(os.path.join(sd, "carts")) if os.path.isdir(os.path.join(sd, "carts")) else "none")
        return 1
    f, models = load_models(path)
    data = open(path, "rb").read()
    check(data[:8] == b"BMCART\0\0" and struct.unpack_from("<H", data, 18)[0] & 1 == 1,
          "TROPIC.BME is a project (.bme: cartridge with the project bit), %d bytes, %d sections" % (len(data), len(f.sections)))
    verify_models(path, models)
    if "--reference" in sys.argv:
        silhouette(path)
    if "--chain" in sys.argv:
        for name in ("01-studio.bme", "01-studio.bm"):
            cp = os.path.join(CHAIN, name)
            if check(os.path.exists(cp), "chain/" + name + " exists"):
                _, m2 = load_models(cp)
                check([m["name"] for m in m2] == ORDER and [len(m["faces"]) for m in m2] == [len(m["faces"]) for m in models],
                      "chain/%s holds the same models" % name)
    print("\nverify: %s" % ("PASS" if not bad else "%d FAILED" % len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
