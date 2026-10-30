#!/usr/bin/env python3
"""
herotable.py: the table of the tests of the hero cart (carts/hero) for the manual "Graphics techniques catalogue".

The cart (hero.bm) runs tests when the bridge sets the keys `hero_test` and `hero_prof` (docs/progress/hero.md): each test on each
profile (arm, gpu) is a sweep of the load, and the report (kind `hero`) holds the lines

    hero 1 result ID PROF load60=.. load30=.. top=..

The reports come to the `reports` branch (reports/<branch>/<time>_hero_<board>_<kernel>.txt). This script reads the newest report of
each board and prints the table: the load that fits in 16.7 ms (60 fps) and in 33.3 ms (30 fps) of each test on the ARM and on the
GPU of each board (V3D on the Pi Zero W, Mali on the RGB30). A test with no result is "not measured".

  herotable.py REPORT_DIR [--lang en|it]            print the table (markdown)
  herotable.py REPORT_DIR --update FILE.md ...      replace the text between the two marker lines in each file:
                                                    <!-- hero-table:start --> and <!-- hero-table:end -->

REPORT_DIR is a folder of report files (git show origin/reports:reports/bm-core/... saved apart, or a checkout of the branch).
"""
import argparse
import glob
import os
import re
import sys

TESTS = [
    ("plaza", "plaza + heroes (smooth light)", "heroes"),
    ("hero-tex", "heroes textured", "heroes"),
    ("hero-holes", "heroes, texels with holes", "heroes"),
    ("hero-screen", "heroes, screen-door", "heroes"),
    ("hero-baked", "heroes, baked light", "heroes"),
    ("light-none", "no light", "heroes"),
    ("light-flat", "flat light", "heroes"),
    ("tex-off", "plaza flat colours", "heroes"),
    ("z-off", "no depth test", "heroes"),
    ("dither-off", "no dither", "heroes"),
    ("fog", "fog", "heroes"),
    ("sky", "sky light, rim, gloss", "heroes"),
    ("lamps2", "2 lamps", "heroes"),
    ("lamps4", "4 lamps", "heroes"),
    ("shadows", "shadows", "heroes"),
    ("cel4", "cel, 4 bands", "heroes"),
    ("outline-hull", "outline, baked hull", "heroes"),
    ("outline-512", "outline, flag 512", "heroes"),
    ("impostor16", "impostors at 16 px", "heroes"),
    ("bilinear", "textured heroes, bilinear", "heroes"),
    ("aa", "4x MSAA", "heroes"),
    ("vs2", "vertex shader 2", "heroes"),
    ("queue2", "queue 2", "heroes"),
    ("nolater", "queue 2, no frame later", "heroes"),
    ("2d-sprites", "2D sprites over the 3D", "sprites"),
    ("2d-text", "2D text over the 3D", "lines"),
    ("2d-map", "2D map layers over the 3D", "layers"),
    ("2d-fade", "2D fade (dark_end)", "lamps"),
    ("2d-light", "2D light (light_end)", "lamps"),
]
IT = {
    "plaza + heroes": "piazza + eroi", "spheres, 96 faces": "sfere, 96 facce", "small faces (about 4 px)": "facce piccole (circa 4 px)",
    "draw calls, a cube each": "chiamate di disegno, un cubo l'una", "fill, flat": "riempimento, piatto",
    "fill, Gouraud": "riempimento, Gouraud", "fill, textured": "riempimento, texturato",
    "fill, texels with holes": "riempimento, texel con buchi", "fill, screen-door": "riempimento, retino",
    "fill, bilinear": "riempimento, bilineare", "three textures in turn": "tre texture a turno",
    "2D sprites over the 3D": "sprite 2D sopra il 3D", "2D text over the 3D": "testo 2D sopra il 3D",
    "2D map layers over the 3D": "strati di mappa 2D sopra il 3D", "2D fade (dark_end)": "dissolvenza 2D (dark_end)",
    "2D light (light_end)": "luce 2D (light_end)",
    "heroes": "eroi", "spheres": "sfere", "grids": "griglie", "draws": "chiamate", "quads": "quadri", "sprites": "sprite",
    "lines": "righe", "layers": "strati", "lamps": "lampade",
}
BOARDS = [("pi-zero-w", "Pi Zero W"), ("rgb30", "RGB30")]
RES = re.compile(r"^hero 1 result (\S+) (\S+) load60=(\S+) load30=(\S+) top=(\d+)", re.M)


def newest(dirpath, board):
    files = sorted(glob.glob(os.path.join(dirpath, "*_hero_%s_*.txt" % board)))
    return files[-1] if files else None


def load(dirpath):
    out = {}
    for board, _ in BOARDS:
        f = newest(dirpath, board)
        got = {}
        if f:
            text = open(f, encoding="utf-8", errors="replace").read()
            for tid, prof, l60, l30, top in RES.findall(text):
                got[(tid, prof)] = (l60, l30, int(top))
        out[board] = (os.path.basename(f) if f else None, got)
    return out


def cell(got, tid, prof, lang):
    r = got.get((tid, prof))
    if not r:
        return "—"
    l60, l30, top = r

    def one(v):
        if v.startswith(">"):
            return v
        try:
            return str(int(round(float(v))))
        except ValueError:
            return v
    return "%s / %s" % (one(l60), one(l30))


def table(dirpath, lang):
    data = load(dirpath)
    t = (lambda s: IT.get(s, s)) if lang == "it" else (lambda s: s)
    head = ["Test", "Unit", "Pi ARM", "Pi V3D", "RGB30 ARM", "RGB30 Mali"]
    if lang == "it":
        head[1] = "Unità"
    lines = ["| " + " | ".join(head) + " |", "|---|---|---|---|---|---|"]
    for tid, name, unit in TESTS:
        row = [t(name), t(unit)]
        for board, _ in BOARDS:
            got = data[board][1]
            row += [cell(got, tid, "arm", lang), cell(got, tid, "gpu", lang)]
        lines.append("| " + " | ".join(row) + " |")
    src = ", ".join("%s: %s" % (b, data[b][0] or "no report") for b, _ in BOARDS)
    note = ("Each cell is the load at 60 fps / at 30 fps (the reports: %s)." % src if lang == "en" else
            "Ogni cella è il carico a 60 fps / a 30 fps (i rapporti: %s)." % src)
    return "\n".join(lines) + "\n\n" + note + "\n"


def update(path, text):
    s = open(path, encoding="utf-8", newline="").read()
    nl = "\r\n" if "\r\n" in s else "\n"
    s = s.replace("\r\n", "\n")
    a, b = "<!-- hero-table:start -->", "<!-- hero-table:end -->"
    if a not in s or b not in s:
        sys.exit("%s: no hero-table markers" % path)
    i, j = s.index(a) + len(a), s.index(b)
    s = s[:i] + "\n" + text + s[j:]
    open(path, "wb").write(s.replace("\n", nl).encode("utf-8"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--lang", default="en", choices=["en", "it"])
    ap.add_argument("--update", nargs="+")
    a = ap.parse_args()
    if a.update:
        for f in a.update:
            lang = "it" if "/it/" in f.replace("\\", "/") else "en"
            update(f, table(a.dir, lang))
            print("updated", f)
    else:
        print(table(a.dir, a.lang))


if __name__ == "__main__":
    main()
