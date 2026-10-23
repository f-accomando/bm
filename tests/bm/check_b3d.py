#!/usr/bin/env python3
"""Checks the 3D Bench's two quick runs on the PC (make test-b3d): both
reports saved, every test with its profiles (the V3D emulated has them
all), the second report read the first, every page of results drawn."""
import os
import sys

TESTS = ["spheres", "spheres_smooth", "spheres_tex", "spheres_unlit", "spheres_baked", "spheres_shine", "heroes",
         "heroes_tex", "heroes_skin", "heroes_shadow", "clip", "tiny", "draws", "quad_flat", "quad_smooth", "quad_tex", "quad_alpha",
         "quad_screen", "quad_texscreen", "texswap", "split", "match", "mix", "queue", "bilinear", "gpu2d", "big", "big_logic"]
FUTURE = []
# the 2D scenes: only when `tests=` names them (the default run has no row of them)
TESTS2D = {"map8": {"ARM"}, "sprites": {"ARM", "GPU+2D"}, "stack": {"ARM"}, "fade": {"ARM", "QPU"}, "light": {"ARM"},
           "herotris": {"ARM", "GPU", "GPU+VS", "GPU+VS+Q", "GPU+VS+Q2", "GPU+VS+A8", "GPU+VS+A8+Q", "GPU+VS+FS2"},
           "cel_lit": {"ARM", "GPU+VS"}, "cel": {"ARM", "GPU+VS"}, "cel_lit_nd": {"ARM", "GPU+VS"}, "cel_nd": {"ARM", "GPU+VS"},
           "cel_outline": {"ARM", "GPU+VS"}, "stress": {"GPU+VS+Q"}}


def check_2d(d):
    """make test-b3d, third run: only the 2D scenes, by name; every report field there and sane."""
    fails = 0
    text = open(os.path.join(d, "3d0001.txt")).read() if os.path.exists(os.path.join(d, "3d0001.txt")) else ""
    rows = [l.split(",") for l in text.splitlines() if l.startswith("R,")]
    for t, need in TESTS2D.items():
        got = {r[2]: r for r in rows if r[1] == t}
        ok = need <= set(got)
        for r in got.values():
            # ms, worst, gpu_ms, clean_kcyc, inval_kcyc, fill_px, ns_px_frame (the PC has no ARM counters: instr is 0)
            ok = ok and len(r) == 36 and 0 <= float(r[7]) <= float(r[8]) and float(r[15]) >= 0
            ok = ok and float(r[25]) >= 0 and float(r[26]) >= 0 and float(r[32]) >= -1
            if not t.startswith("cel"):             # the cel tests draw no 2D: no pixels counted
                ok = ok and int(r[30]) > 0
            ok = ok and float(r[34]) >= 0 and float(r[35]) >= 0       # bin_us, rnd_us
            if t == "herotris":
                ok = ok and int(r[9]) > 6000        # triangles given (a plain GPU keeps fewer after the cull)
                if r[2] != "ARM":
                    ok = ok and float(r[35]) > 0 and float(r[14]) > 0
                    if "+Q" not in r[2]:        # a queued job that ended before the ARM looked is not timed
                        ok = ok and float(r[34]) > 0
        print(("ok   " if ok else "FAIL ") + f"2D scene {t}: {' '.join(sorted(got))}")
        fails += not ok
    # Q1: an H line for every herotris profile; the VS profiles say what the mesh path did with the hero
    hrows = [l.split(",") for l in text.splitlines() if l.startswith("H,")]
    hgot = {r[2]: r for r in hrows if r[1] == "herotris"}
    ok = set(TESTS2D["herotris"]) <= set(hgot) and all(len(r) == 17 for r in hrows)
    for p, r in hgot.items():
        want = "ACCEPTED" if "VS" in p else "NA"
        ok = ok and r[11] == want and int(r[4]) > 0 and int(r[6]) > 0 and int(r[7]) > 0
        ok = ok and (r[15] != "-") == (p in ("GPU+VS+Q2",))
    print(("ok   " if ok else "FAIL ") + f"H lines (hero triangles at 60 fps): {len(hgot)} profiles, mesh path ACCEPTED on the VS ones")
    fails += not ok
    # Q2: C lines, the four cel tests at the same n, ms and ratios
    crows = [l.split(",") for l in text.splitlines() if l.startswith("C,")]
    ok = bool(crows) and {"ARM", "GPU+VS"} <= {r[1] for r in crows} and all(len(r) == 12 for r in crows)
    ok = ok and all(float(r[4]) >= 0 and float(r[5]) >= 0 and int(r[3]) > 0 for r in crows if r[2] == "1")
    print(("ok   " if ok else "FAIL ") + f"C lines (cel cost): {len(crows)} rows")
    fails += not ok
    # the stress test: the ramp's row, then a T line for 1x and 2x of its load at 60 fps (2x has twice the quarter sets)
    tline = [l for l in text.splitlines() if l.startswith("stress path=")]
    trows = [l.split(",") for l in text.splitlines() if l.startswith("T,")]
    ok = len(tline) == 1 and "path=GPU+VS+Q" in tline[0] and [r[2] for r in trows] == ["1x", "2x"]
    ok = ok and all(len(r) == 17 and int(r[5]) > 0 and 0 < float(r[7]) <= float(r[8]) and int(r[9]) >= 0 and float(r[11]) >= 0
                    and int(r[13]) >= 0 and int(r[14]) >= 0 and r[16] == "0" for r in trows)
    ok = ok and len(trows) == 2 and int(trows[1][3]) == 2 * int(trows[0][3]) and int(trows[1][4]) > int(trows[0][4])
    print(("ok   " if ok else "FAIL ") + f"stress (T lines, 1x and 2x): {tline[0] if tline else 'missing'}; {len(trows)} rows")
    fails += not ok
    hdr = "size 640x360" in text and "vsprobe gpu3d_vshader=" in text
    print(("ok   " if hdr else "FAIL ") + "size and vsprobe lines in the header")
    fails += not hdr
    # the fade on the QPU profile: one qpufade line (the PC has no QPU hook: the ARM path ran, key 0)
    qline = [l for l in text.splitlines() if l.startswith("qpufade ")]
    ok = len(qline) == 1 and "key=0 path=arm" in qline[0] and "n60_qpu=" in qline[0] and "ms_arm=" in qline[0]
    print(("ok   " if ok else "FAIL ") + f"qpufade line (no hook: ARM): {qline[0] if qline else 'missing'}")
    fails += not ok
    others = {r[1] for r in rows} - set(TESTS2D)
    print(("ok   " if not others else "FAIL ") + "2D scenes: only the named tests ran")
    fails += bool(others)
    print(f"b3d 2D: {'all ok' if not fails else f'{fails} failed'}")
    return 1 if fails else 0


def check_qpufade(d):
    """make test-b3d, fourth run: --tests=fade --profiles=ARM,QPU with a stand-in QPU hook: both rows, path=qpu."""
    text = open(os.path.join(d, "3d0001.txt")).read()
    rows = {l.split(",")[2] for l in text.splitlines() if l.startswith("R,fade,")}
    qline = [l for l in text.splitlines() if l.startswith("qpufade ")]
    ok = rows == {"ARM", "QPU"} and len(qline) == 1 and "key=1 path=qpu" in qline[0] and "frames_arm=0" in qline[0]
    ok = ok and "ms_qpu=" in qline[0] and "ms_arm=" in qline[0] and "frames_qpu=0" not in qline[0]
    print(("ok   " if ok else "FAIL ") + f"qpufade (stand-in hook): {' '.join(sorted(rows))}; {qline[0] if qline else 'missing'}")
    return 0 if ok else 1


def main():
    d = sys.argv[1]
    if len(sys.argv) > 2 and sys.argv[2] == "--qpufade":
        return check_qpufade(d)
    if len(sys.argv) > 2 and sys.argv[2] == "--2d":
        return check_2d(d)
    fails = 0

    def check(ok, what):
        nonlocal fails
        print(("ok   " if ok else "FAIL ") + what)
        fails += not ok

    reports = sorted(f for f in os.listdir(d) if f.startswith("3d") and f.endswith(".txt"))
    check(reports == ["3d0001.txt", "3d0002.txt"], f"two reports ({' '.join(reports)})")
    if len(reports) < 2:
        return 1
    text = open(os.path.join(d, reports[1])).read()
    lines = text.splitlines()
    check(lines[0] == "bm 3D Bench" and any(l.startswith("drivers bm3d ") for l in lines), "the header")
    check("previous 3d0001.txt" in lines, "the second run read the first report")
    rows = [l.split(",") for l in lines if l.startswith("R,")]
    got = {(r[1], r[2]) for r in rows}
    for t in TESTS:
        profs = {p for (tt, p) in got if tt == t}
        need = {"GPU", "GPU+Q", "GPU+VS", "GPU+VS+Q"} if t == "queue" else {"GPU"} if t == "bilinear" else {"ARM", "GPU"}
        if t in ("split", "match"):
            need |= {"GPU+Q", "GPU+2D"}
        if t == "gpu2d":
            need |= {"GPU+2D"}
        if t in ("spheres_tex", "heroes_tex", "quad_tex", "match", "mix"):
            need |= {"GPU+T16", "GPU+FS2", "GPU+ETC1"}     # bm3d 7.3: the opaque sheets as ETC1
        if t in ("heroes_skin", "quad_alpha", "quad_texscreen"):
            need |= {"GPU+FS2"}             # bm3d 6.3: every textured shader with two threads
        if t in ("spheres", "heroes", "match", "big", "big_logic"):
            need |= {"GPU+VS+S"}
        if t != "bilinear":
            need |= {"GPU+VS+FS2"}          # bm3d 6.6: the driver as the games get it, wherever the GPU runs
        check(need <= profs, f"{t}: {' '.join(sorted(profs))}")
    check(not any(r[1] in TESTS2D for r in rows), "the 2D scenes: not in the default run")
    for t in FUTURE:
        check(any(l.startswith(f"F,{t},") for l in lines), f"{t}: shown as not developed yet")
    # the vertex shader's rows: the models it takes cost the ARM fewer instructions (here: fewer
    # vertices placed by r3d)
    vs = {r[1]: r for r in rows if r[2] == "GPU+VS"}
    gpu = {r[1]: r for r in rows if r[2] == "GPU"}
    check(all(int(vs[t][11]) < int(gpu[t][11]) or int(gpu[t][11]) == 0 for t in ("spheres", "heroes", "heroes_tex", "heroes_skin") if t in vs),
          "GPU+VS: r3d places fewer vertices than GPU")
    check(all(float(r[4]) >= 0 or r[4] == "-1" for r in rows), "loads at 60 fps")
    check(all(len(r) == 36 and int(r[21]) >= int(r[22]) >= 0 for r in rows), "triangles at 60 fps: given, drawn")
    # the cost of a pixel: a P line for every fill test and gpu2d, the GPU's for every GPU profile
    pix = {(r[1], r[2]): r for r in (l.split(",") for l in lines if l.startswith("P,"))}
    want = [(r[1], r[2]) for r in rows if r[1].startswith("quad_") or r[1] in ("bilinear", "gpu2d")]
    check(want and all(k in pix and len(pix[k]) == 12 for k in want), f"pixel cost: {len(pix)} P lines")
    # (the PC's ms are noise, a slope may come out -1; the emulated V3D's job time is 0: only the shape)
    check(all(int(r[30]) > 0 for r in rows if (r[1], r[2]) in want), "pixel cost: the pixels filled are counted")
    check(all(all(v == "-1" or float(v) >= 0 for v in pix[k][3:]) and (k[1] == "ARM") == (pix[k][3] == "-1")
              for k in want if k in pix), "pixel cost: the GPU's job on the GPU profiles only")
    # the score: the three drivers, every test of the score in it (the emulator runs them all)
    score = {r[1]: r for r in (l.split(",") for l in lines if l.startswith("S,"))}
    check(set(score) == {"games", "GPU", "ARM"} and all(int(r[3]) > 0 and r[5] == "1" for r in score.values()),
          f"the score: {' '.join(f'{k} {v[3]} ({v[4]} tests)' for k, v in score.items())}")
    mix = [l for l in lines if l.startswith("triangles at 60 fps ")]
    check(len(mix) == 1 and int(mix[0].split()[4]) > 0, mix[0] if mix else "triangles at 60 fps: missing")
    pages = sorted(f for f in os.listdir(d) if f.startswith("page-"))
    check(len(pages) == 4 + len(TESTS) + len(TESTS2D) + len(FUTURE), f"{len(pages)} pages drawn")
    print(f"b3d: {'all ok' if not fails else f'{fails} failed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
