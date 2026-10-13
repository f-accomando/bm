#!/usr/bin/env python3
"""Checks the project that bm Pixel and bm Studio saved in the recording of episode 2 (out/sd/carts/TROPIC.BME), without
looking at a picture: the file is what the editors really wrote.

  python3 video/3d/02-pixel/verify.py [SD_DIR] [--start START.bme]

 1. the sheet: the eight tiles (grass, path, rock, wood, skin, face, apple, apple_icon) at x = 16 * n, y = 48, pixel by pixel
    against art.py (RGB565 rounding allowed); "face" is the skin with the eyes painted in bm Studio; nothing else on the
    rows from y = 48 down, and the starter tiles of bm Studio (rows 0-47) are untouched;
 2. the models: names and triangle counts as in the start project (video 1), budgets of PIANO.md (PICO <= 300, APPLE <= 80,
    CRATE <= 40); every face of CRATE uses the tile wood, every face of APPLE the tile apple; PICO: the faces that were
    legs, belly and tail tip (warm flat colours) stay flat with the same colours, the two outer front faces of the head
    use the tile face, all the others the tile skin;
 3. bm_forge check (budgets, structure) and the chain files (video/3d/chain/02-pixel.bm / .bme) when present.
Exit 0 only when everything passes.
"""
import json
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
sys.path.insert(0, HERE)
import bmres  # noqa: E402
import art    # noqa: E402

BUDGET = {"PICO": 300, "APPLE": 80, "CRATE": 40}
EXPECT = ["CRATE", "APPLE", "PICO"]                 # the order video 1 builds them in
ok_all = True


def check(cond, msg):
    global ok_all
    print("  %s  %s" % ("ok " if cond else "BAD", msg))
    if not cond:
        ok_all = False
    return cond


def rgb_of(c):
    return (c >> 16 & 255, c >> 8 & 255, c & 255)


def near(a, b, tol=8):
    return a is not None and b is not None and all(abs(x - y) <= tol for x, y in zip(a, b))


def sheet_px(f):
    w, h, rgba = bmres.sheet_get(f)

    def px(x, y):
        i = 4 * (y * w + x)
        return None if rgba[i + 3] < 128 else (rgba[i], rgba[i + 1], rgba[i + 2])
    return w, h, px


def tris(rec):
    """[(vertices of the triangle (3 xyz), colour, uv corners in sheet pixels)] of a model record"""
    nv, nf = struct.unpack_from("<HH", rec, bmres.NAME_LEN)
    verts = [struct.unpack_from("<3f", rec, bmres.NAME_LEN + 8 + 12 * i) for i in range(nv)]
    out = []
    base = bmres.NAME_LEN + 8 + 12 * nv
    for i in range(nf):
        o = base + 24 * i
        a, b, c = struct.unpack_from("<3H", rec, o)
        colour = struct.unpack_from("<I", rec, o + 8)[0]
        uv = struct.unpack_from("<6H", rec, o + 12)
        out.append(((verts[a], verts[b], verts[c]), colour, [(uv[0] / 8, uv[1] / 8), (uv[2] / 8, uv[3] / 8), (uv[4] / 8, uv[5] / 8)]))
    return out


def tile_of(uv):
    """the tile (art.NAMES) a triangle's texture corners lie in, None when it is not inside one tile of the row"""
    cx = sum(p[0] for p in uv) / 3
    cy = sum(p[1] for p in uv) / 3
    n, row = int(cx // 16), int(cy // 16)
    if row != art.ROW_Y // 16 or not 0 <= n < len(art.NAMES):
        return None
    if any(int(p[0] // 16) != n and abs(p[0] - 16 * n) > 1 and abs(p[0] - 16 * (n + 1)) > 1 for p in uv):
        return None
    return art.NAMES[n]


def centre_normal(t):
    (a, b, c) = t
    cen = [(a[i] + b[i] + c[i]) / 3 for i in range(3)]
    u = [b[i] - a[i] for i in range(3)]
    v = [c[i] - a[i] for i in range(3)]
    n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
    ln = sum(x * x for x in n) ** 0.5 or 1
    return cen, [x / ln for x in n]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    sd = args[0] if args else os.path.join(HERE, "out", "sd")
    start = os.path.join(HERE, "out", "start.bme")
    if "--start" in sys.argv:
        start = sys.argv[sys.argv.index("--start") + 1]
    path = os.path.join(sd, "carts", "TROPIC.BME")
    if not os.path.exists(path):
        print("no", path, "- files:", os.listdir(os.path.join(sd, "carts")) if os.path.isdir(os.path.join(sd, "carts")) else "none")
        return 1
    f = bmres.read(path)
    print("project:", path)
    check(f.header[18] & 1 == 1, "it is a project (.bme flag)")

    # ---------------------------------------------------------------- 1. the sheet
    print("sheet")
    w, h, px = sheet_px(f)
    check((w, h) == (256, 256), "sheet 256x256 (%dx%d)" % (w, h))
    for n, name in enumerate(art.NAMES):
        if name == "face":
            want = [[None if c is None else rgb_of(c) for c in row] for row in art.face_expected_rgb()]
        else:
            want = [[None if c is None else rgb_of(art.rgb(c)) for c in row] for row in art.render(name)]
        wrong = []
        for y in range(16):
            for x in range(16):
                got = px(16 * n + x, art.ROW_Y + y)
                w_ = want[y][x]
                if not (got is None if w_ is None else near(got, w_)):
                    wrong.append((x, y, w_, got))
        check(not wrong, "tile %d %-10s %s" % (n, name, "256 pixels as art.py" if not wrong else "%d wrong, first %s" % (len(wrong), wrong[:3])))
    extra = [(x, y) for y in range(art.ROW_Y, h) for x in range(w) if px(x, y) and not (art.ROW_Y <= y < art.ROW_Y + 16 and x < 16 * len(art.NAMES))]
    check(not extra, "nothing else on the rows from y=%d down (%d stray pixels)" % (art.ROW_Y, len(extra)))
    if os.path.exists(start):
        sf = bmres.read(start)
        sw, sh, spx = sheet_px(sf)
        diff = [(x, y) for y in range(min(art.ROW_Y, sh)) for x in range(min(w, sw)) if px(x, y) != spx(x, y) and not near(px(x, y), spx(x, y), 8)]
        check(not diff, "the starter tiles of bm Studio (rows 0-47) are untouched (%d pixels differ)" % len(diff))
    else:
        print("  --  no start project (%s): starter tiles not compared" % start)

    # ---------------------------------------------------------------- 2. the models
    print("models")
    inset, recs = bmres.mesh_parse(f.get(bmres.SEC_MESH))
    names = [n for n, _ in recs]
    check(names == EXPECT, "models %s (expected %s)" % (names, EXPECT))
    by = dict(recs)
    start_by = {}
    if os.path.exists(start):
        start_by = dict(bmres.mesh_parse(bmres.read(start).get(bmres.SEC_MESH))[1])
    for name in EXPECT:
        if name not in by:
            continue
        t = tris(by[name])
        if name in start_by:
            check(len(t) == len(tris(start_by[name])), "%s: %d triangles as in video 1 (%d)" % (name, len(t), len(tris(start_by[name]))))
        check(len(t) <= BUDGET[name], "%s: %d triangles <= budget %d" % (name, len(t), BUDGET[name]))
    for name, tile in (("CRATE", "wood"), ("APPLE", "apple")):
        if name in by:
            use = {}
            for _, colour, uv in tris(by[name]):
                k = "flat" if (colour and uv == [(0, 0)] * 3) else (tile_of(uv) or "?")
                use[k] = use.get(k, 0) + 1
            check(list(use) == [tile], "%s: every face uses the tile %s %s" % (name, tile, use))
    if "PICO" in by:
        t = tris(by["PICO"])
        flat, use = [], {}
        for tri, colour, uv in t:
            if colour and uv == [(0, 0)] * 3:
                flat.append(colour)
                use["flat"] = use.get("flat", 0) + 1
            else:
                k = tile_of(uv) or "?"
                use[k] = use.get(k, 0) + 1
        print("  --  PICO faces:", use)
        warm = lambda c: (c >> 16 & 255) > (c & 255) + 40
        if "PICO" in start_by:
            want = sorted(c for _, c, _ in tris(start_by["PICO"]) if warm(c))
            check(sorted(flat) == want, "PICO: %d flat triangles = the legs, belly and tail tip of video 1 (%d), same colours" % (len(flat), len(want)))
        else:
            check(all(warm(c) for c in flat) and flat, "PICO: the flat triangles are warm colours (legs, belly, tail tip)")
        check(set(use) <= {"flat", "skin", "face"}, "PICO: tiles used %s (only skin, face, flat)" % sorted(use))
        check(use.get("face", 0) == 4, "PICO: the tile face on 4 triangles = 2 quad faces (%d)" % use.get("face", 0))
        ys = [c[1] for tri, _, _ in t for c in tri]
        ymin, ymax = min(ys), max(ys)
        eyes = [centre_normal(tri) for tri, colour, uv in t if tile_of(uv) == "face"]
        check(all(n[2] > 0.9 and cen[1] >= ymin + 0.85 * (ymax - ymin) for cen, n in eyes), "PICO: the face tile is on front faces (+z) of the top of the head")
        ex = [centre_normal(tri)[0][0] for tri, colour, uv in t if tile_of(uv) == "face"]
        check(min(ex) < -0.05 and max(ex) > 0.05 and abs(min(ex) + max(ex)) < 0.02,
              "PICO: the faces with the tile face are one on each side of the head (x %.2f / %.2f)" % (min(ex), max(ex)))
        check(use.get("skin", 0) == len(t) - len(flat) - use.get("face", 0), "PICO: all the other triangles use the tile skin (%d)" % use.get("skin", 0))

    # ---------------------------------------------------------------- 3. forge and chain
    print("forge / chain")
    r = subprocess.run([sys.executable, os.path.join(ROOT, ".claude", "skills", "bm-forge", "scripts", "bm_forge.py"), "check", path,
                        "--no-sync", "--json"], capture_output=True, text=True)
    try:
        j = json.loads(r.stdout)
        fl = j["files"][0]
        check(r.returncode == 0 and fl.get("ok"), "bm_forge check: ok, %d triangles, models %s" % (fl.get("triangles", 0), {m["name"]: m["faces"] for m in fl["models"]}))
    except Exception as e:
        check(False, "bm_forge check: %s %s" % (e, r.stdout[:200] + r.stderr[:200]))
    chain = os.path.join(ROOT, "video", "3d", "chain")
    for ext in (".bm", ".bme"):
        p = os.path.join(chain, "02-pixel" + ext)
        if os.path.exists(p):
            g = bmres.read(p)
            same = g.sections == f.sections
            check(same, "chain/02-pixel%s has the sections of the project" % ext)
            check((g.header[18] & 1 == 1) == (ext == ".bme"), "chain/02-pixel%s: project flag %s" % (ext, "set" if ext == ".bme" else "clear"))
        else:
            print("  --  chain/02-pixel%s not there (full record.py run writes it)" % ext)
    print("RESULT:", "ALL OK" if ok_all else "FAILED")
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
