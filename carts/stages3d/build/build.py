#!/usr/bin/env python3
"""
Stages 3D: the shared logic (common/) plus one front per edition (bm/ now, b16/ later).

  carts/stages3d/build/build.py EDITION OUT.lua [--map OUT.map]

EDITION is `bm` (the Pi, 640x360) or `b16` (the RGB30, 360x360: no front yet).
The sources of the edition are COPIED to OUT's folder under src/ (common/*.lua and
<EDITION>/*.lua), then joined into one main.lua in file-name order (the two-digit prefix
decides; on a tie common/ wins). The first file (00_core.lua) stays at the top level;
every other file goes inside `do ... end`, so its locals never count against Lua's limit
of 200 per function. The map lists where each file starts in OUT.lua.

The rules of the structure:
 1. common/ holds logic and data only; the screen is known by the front alone (SCREEN_W,
    SCREEN_H, camera3d, light3d, draw3d, print, rect).
 2. The front is the only file that changes from one edition to the other and implements
    the FRONT interface (title_draw, play_draw, clear_draw, final_draw, record_draw).
 3. Each edition has its own front sources and bm.info, its own Makefile rule and its own
    .bm/.b16: writing b16/ changes no file of bm/.
 4. Changing common/ changes both editions: the logic test (make test-stages3d) runs on
    the .bm and must be repeated on the .b16.
"""
import argparse
import glob
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def sources(edition):
    found = {}
    for sub in (edition, "common"):             # common/ last: on a tie its file wins
        for path in glob.glob(os.path.join(ROOT, sub, "*.lua")):
            found[os.path.basename(path)] = path
    return [found[n] for n in sorted(found)]


def build(edition, out):
    files = sources(edition)
    if not any(p.startswith(os.path.join(ROOT, edition)) for p in files):
        sys.exit(f"{edition}: no front yet")
    src = os.path.join(os.path.dirname(os.path.abspath(out)), "src")
    shutil.rmtree(src, ignore_errors=True)
    os.makedirs(src)
    text_out, spans, line = [], [], 1
    for i, path in enumerate(files):
        name = os.path.basename(path)
        shutil.copyfile(path, os.path.join(src, name))
        text = open(os.path.join(src, name), encoding="utf-8").read().replace("\r\n", "\n")
        if not text.endswith("\n"):
            text += "\n"
        wrap = i > 0
        text_out.append(f"do -- {name}\n" if wrap else f"-- {name}\n")
        line += 1
        n = text.count("\n")
        spans.append((line, line + n - 1, name))
        text_out.append(text)
        line += n
        if wrap:
            text_out.append("end\n")
            line += 1
    return "".join(text_out), spans


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("edition", choices=("bm", "b16"))
    ap.add_argument("out")
    ap.add_argument("--map")
    a = ap.parse_args()
    text, spans = build(a.edition, a.out)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    if a.map:
        with open(a.map, "w") as f:
            for first, last, name in spans:
                f.write(f"{first} {last} {name}\n")


if __name__ == "__main__":
    main()
