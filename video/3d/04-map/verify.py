#!/usr/bin/env python3
"""Checks the project that bm SDK saved in the recording of episode 4 (out/sd/carts/TROPIC.BME), without looking at a picture.

  python3 video/3d/04-map/verify.py [SD_DIR]

 1. the map is 256x256 with the four layers floor, height, walls, objects (made by T);
 2. floor: a corridor of grass at least 3 cells wide (Shift+W x2, Shift+P) and at least 10 cells long, with a hole in it;
 3. height and walls have at least one step / block each;
 4. objects: apple placeholders 1026 (Shift+A rows), a crate 1028 (Shift+C) and a lamp 1032 (Shift+O, put back after Ctrl+Z);
 5. the project keeps the models and the sheet of episode 3 (sections MESH, ANIM, SHEET), TROPIC.BM has the same sections,
    bm_forge check passes.
Exit 0 only when everything passes.
"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmres  # noqa: E402

ok_all = True


def check(cond, msg):
    global ok_all
    print("  %s  %s" % ("ok " if cond else "BAD", msg))
    if not cond:
        ok_all = False
    return cond


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    sd = args[0] if args else os.path.join(HERE, "out", "sd")
    path = os.path.join(sd, "carts", "TROPIC.BME")
    if not os.path.exists(path):
        print("no", path)
        return 1
    f = bmres.read(path)
    print("project:", path)
    check(f.header[18] & 1 == 1, "it is a project (.bme flag)")

    print("layers")
    w, h = bmres.map_get(f)[:2]
    L = bmres.layers_get(f)
    names = [n for n, _ in L]
    check((w, h) == (256, 256), "map 256x256 (%dx%d)" % (w, h))
    check(names == ["floor", "height", "walls", "objects"], "layers %s" % names)
    cells = dict(L)
    pts = lambda n: [(i % w, i // w, v) for i, v in enumerate(cells.get(n, [])) if v]

    print("floor")
    fl = pts("floor")
    rows = {}
    for x, y, _ in fl:
        rows.setdefault(y, []).append(x)
    full = [y for y, xs in rows.items() if max(xs) - min(xs) + 1 >= 6 and len(xs) >= 6]   # 16x16 brush: a cell is 2x2 tiles, width 3 = 6 tiles
    check(len(full) >= 10, "a corridor at least 3 cells wide and 10 rows long (%d rows)" % len(full))
    if rows:
        # the 16x16 brush draws 2x2 tiles per cell, so rows alternate (3 and 6 tiles): a hole is a row with fewer tiles than the rows 2 above and below
        holes = [y for y in rows if len(rows.get(y - 2, [])) > len(rows[y]) < len(rows.get(y + 2, []))]
        check(bool(holes), "a hole in the corridor (thin rows %s)" % holes[:4])

    print("height and walls")
    check(len(pts("height")) >= 2, "height steps: %d tiles" % len(pts("height")))
    check(len(pts("walls")) >= 2, "walls blocks: %d tiles" % len(pts("walls")))

    print("objects")
    vals = [v for _, _, v in pts("objects")]
    check(vals.count(1026) >= 6, "apples (1026): %d tiles" % vals.count(1026))
    check(1028 in vals, "a crate (1028)")
    check(1032 in vals, "a lamp (1032)")

    print("sections / forge")
    for sec, name in ((bmres.SEC_MESH, "MESH"), (bmres.SEC_ANIM, "ANIM")):
        check(f.get(sec) is not None, "section %s kept from episode 3" % name)
    check(f.get(bmres.SEC_SHEET) is not None or f.get(bmres.SEC_SHEET8) is not None, "the sheet is kept from episode 3")
    bm = os.path.join(sd, "carts", "TROPIC.BM")
    if os.path.exists(bm):
        g = bmres.read(bm)
        check(g.header[18] & 1 == 0, "TROPIC.BM is a game (project flag clear)")
        check(g.get(bmres.SEC_LAYERS) == f.get(bmres.SEC_LAYERS) and g.get(bmres.SEC_MAP) == f.get(bmres.SEC_MAP), "TROPIC.BM has the same map layers")
    else:
        check(False, "TROPIC.BM built by Ctrl+B")
    r = subprocess.run([sys.executable, os.path.join(ROOT, ".claude", "skills", "bm-forge", "scripts", "bm_forge.py"), "check", path,
                        "--no-sync", "--json"], capture_output=True, text=True)
    try:
        fl_ = json.loads(r.stdout)["files"][0]
        check(r.returncode == 0 and fl_.get("ok"), "bm_forge check: ok")
    except Exception as e:
        check(False, "bm_forge check: %s %s" % (e, r.stdout[:150] + r.stderr[:150]))
    print("RESULT:", "ALL OK" if ok_all else "FAILED")
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
