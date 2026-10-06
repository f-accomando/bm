#!/usr/bin/env python3
"""
Overbit is written in several Lua files (carts/overbit/src/*.lua, in
name order); a cartridge has one script, so they are joined into one
main.lua. The first file (00_*.lua) declares the shared locals and stays at
the top level; every other file goes inside `do ... end`, so its own locals
never count against Lua's limit of 200 per function.

  carts/overbit/build.py OUT.lua [--map OUT.map] [--start MODE]

The map lists where each file starts in OUT.lua, to find the source line
of an error message ("main.lua:1234").
"""
import argparse
import glob
import hashlib
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def build(extra=()):
    files = sorted(glob.glob(os.path.join(HERE, "src", "*.lua")) + list(extra), key=os.path.basename)
    out, spans, line = [], [], 1
    for i, path in enumerate(files):
        name = os.path.basename(path)
        text = open(path, encoding="utf-8").read()
        if not text.endswith("\n"):
            text += "\n"
        wrap = i > 0
        head = f"do -- {name}\n" if wrap else f"-- {name}\n"
        out.append(head)
        line += 1
        n = text.count("\n")
        spans.append((line, line + n - 1, name))
        out.append(text)
        line += n
        if wrap:
            out.append("end\n")
            line += 1
    return "".join(out), spans


def minify(text):
    """a line that is only a comment, or blank, goes (no source has a long
    string or a --[[ comment: build checks); the code is the same"""
    import re
    if re.search(r"\[=*\[", text):
        raise SystemExit("build.py --minify: a long string or comment ([[ ]]) is in the code")
    keep = []
    for ln in text.split("\n"):
        t = ln.strip()
        if not t or (t.startswith("--") and not t.startswith("--[")):
            continue
        keep.append(ln)
    return "\n".join(keep) + "\n"


def where(spans, n):
    for first, last, name in spans:
        if first <= n <= last:
            return name, n - first + 1
    return None, n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--map")
    ap.add_argument("--start", help="the mode at start (tests, reels): menu, range, match, explore, reel, bench")
    ap.add_argument("--hero", help="the hero chosen at start (tests): rally, kaiju, ...")
    ap.add_argument("--quality", type=int, help="a fixed quality level 0..4 (benchmarks)")
    ap.add_argument("--minify", action="store_true",
                    help="the release (.b16): no comment-only lines or blank ones (the map's lines no longer match)")
    ap.add_argument("--define", action="append", default=[], help="NAME=lua value, a global set after the code (tests)")
    ap.add_argument("--extra", action="append", default=[],
                    help="a generated file to join too, in name order with the others (the map: 21_map.lua)")
    a = ap.parse_args()
    text, spans = build(a.extra)
    # which build this is, on the screen (title, Select panel) and in the
    # log: the first 7 hex digits of the sources' SHA-1, so the same game
    # always has the same tag and any change gives a new one
    tag = hashlib.sha1(text.encode("utf-8")).hexdigest()[:7]
    if a.minify:
        text = minify(text)
    text += f'OVERBIT_BUILD = "{tag}"\n'
    if a.start:
        text += f'OVERBIT_START = "{a.start}"\n'
    if a.hero:
        text += f'OVERBIT_HERO = "{a.hero}"\n'
    if a.quality is not None:
        text += f"OVERBIT_QUALITY = {a.quality}\n"
    for d in a.define:
        name, value = d.split("=", 1)
        text += f"{name} = {value}\n"
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w", encoding="utf-8") as f:
        f.write(text)
    if a.map:
        with open(a.map, "w") as f:
            for first, last, name in spans:
                f.write(f"{first} {last} {name}\n")


if __name__ == "__main__":
    main()
