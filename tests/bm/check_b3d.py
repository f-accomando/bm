#!/usr/bin/env python3
"""Checks the 3D Bench's two quick runs on the PC (make test-b3d): both
reports saved, every test with its profiles (the V3D emulated has them
all), the second report read the first, every page of results drawn."""
import os
import sys

TESTS = ["spheres", "spheres_smooth", "spheres_tex", "spheres_unlit", "spheres_baked", "spheres_shine", "heroes",
         "heroes_tex", "heroes_skin", "heroes_shadow", "clip", "tiny", "draws", "quad_flat", "quad_smooth", "quad_tex", "quad_alpha",
         "quad_screen", "texswap", "split", "match", "queue"]
FUTURE = ["gpu2d", "bilinear"]


def main():
    d = sys.argv[1]
    fails = 0

    def check(ok, what):
        nonlocal fails
        print(("ok   " if ok else "FAIL ") + what)
        fails += not ok

    reports = sorted(f for f in os.listdir(d) if f.startswith("3D") and f.endswith(".TXT"))
    check(reports == ["3D0001.TXT", "3D0002.TXT"], f"two reports ({' '.join(reports)})")
    if len(reports) < 2:
        return 1
    text = open(os.path.join(d, reports[1])).read()
    lines = text.splitlines()
    check(lines[0] == "bm 3D Bench" and any(l.startswith("drivers bm3d ") for l in lines), "the header")
    check("previous 3D0001.TXT" in lines, "the second run read the first report")
    rows = [l.split(",") for l in lines if l.startswith("R,")]
    got = {(r[1], r[2]) for r in rows}
    for t in TESTS:
        profs = {p for (tt, p) in got if tt == t}
        need = {"GPU+VS", "GPU+VS+Q"} if t == "queue" else {"ARM", "GPU"}
        check(need <= profs, f"{t}: {' '.join(sorted(profs))}")
    for t in FUTURE:
        check(any(l.startswith(f"F,{t},") for l in lines), f"{t}: shown as not developed yet")
    # the vertex shader's rows: the models it takes cost the ARM fewer instructions (here: fewer
    # vertices placed by r3d)
    vs = {r[1]: r for r in rows if r[2] == "GPU+VS"}
    gpu = {r[1]: r for r in rows if r[2] == "GPU"}
    check(all(int(vs[t][11]) < int(gpu[t][11]) or int(gpu[t][11]) == 0 for t in ("spheres", "heroes", "heroes_tex", "heroes_skin") if t in vs),
          "GPU+VS: r3d places fewer vertices than GPU")
    check(all(float(r[4]) >= 0 or r[4] == "-1" for r in rows), "loads at 60 fps")
    pages = sorted(f for f in os.listdir(d) if f.startswith("page-"))
    check(len(pages) == 2 + len(TESTS) + len(FUTURE), f"{len(pages)} pages drawn")
    print(f"b3d: {'all ok' if not fails else f'{fails} failed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
