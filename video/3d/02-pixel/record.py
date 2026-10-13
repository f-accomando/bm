#!/usr/bin/env python3
"""Tropic Trail, episode 2: textures in bm Pixel, then painting the models in bm Studio.

  python3 video/3d/02-pixel/record.py [--start forge|chain] [--stage pixel|studio] [--build-only] [--no-video]

Two recordings on the SAME SD (like the 2D series): build/pixel.bm with input-pixel.txt, then build/studio.bm with
input-studio.txt. The keys come from ONE Script (continuous frames = the seconds of the edited video, 60 fps) which is
split at the cut (Pixel 0:08-2:22 = frames 480-8520, Studio 2:24-3:30 = frames 8640-12600; each file restarts at 0).

Writes video/3d/02-pixel/out/: input.txt (all the keys, montage frames), input-pixel.txt, input-studio.txt, overlay.json
(lib3d.write_overlay), shots/, sd/ (the SD after both recordings: carts/TROPIC.BME is the project saved by the editors),
bin/ (the two editors built here) and, when ffmpeg exists, ep02.mp4 (--no-video: the default without ffmpeg).
Deterministic: the same script gives the same keys and the same project.

Start: --start chain (default when video/3d/chain/01-studio.bme exists) copies the project of video 1 as
carts/TROPIC.BME; --start forge uses the stand-in of standin.py (the reference models of video/3d/assets, flat colours,
on a sheet with bm Studio's starter tiles).

Result: out/sd/carts/TROPIC.BME (project) -> video/3d/chain/02-pixel.bme and chain/02-pixel.bm (the same cartridge
without the project flag: `bmres` rewrite, the editors never touch a .bm; the .bm is what the next video opens).

Editors: build/pixel.bm and build/studio.bm are NOT used: the Makefile rules of the suite apps pass `$<` (= bm.info)
as --lua, so those files hold the bm.info text as code ("main.lua:1: unexpected symbol near '#'"). This script
builds its own copies with scripts/mkbm.py into out/bin/ (same options the Makefile intends).
"""
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "video", "3d"))
sys.path.insert(0, HERE)

import lib3d                                    # noqa: E402  (also puts video/lib and scripts/ on sys.path)
import art                                      # noqa: E402
import calib                                    # noqa: E402
import studio_part                              # noqa: E402
import bmres                                    # noqa: E402
from bmvideo import Script, FPS, call, encode   # noqa: E402

OUT = os.path.join(HERE, "out")
SD = os.path.join(OUT, "sd")
BIN = os.path.join(OUT, "bin")
PIXEL_START, PIXEL_END = 8, 142                 # seconds of the edited video: Pixel records 0:08 .. 2:22
STUDIO_START, STUDIO_END = 144, 210             # Studio: 2:24 .. 3:30
TITLE = "Tropic Trail  ·  Episode 2: textures and painting"
TOL = 1.0                                       # seconds of tolerance on the shot list times


# ---------------------------------------------------------------------------------------------- helpers
class Ep(Script):
    """Script with absolute times: until(2:16) waits for that second of the edited video."""
    late = []

    def until(self, sec, what=""):
        target = round(sec * FPS)
        if self.t > target + TOL * FPS:
            Ep.late.append("%s: wanted %s, is %.1f s" % (what or "?", fmt(sec), self.t / FPS))
        elif self.t < target:
            self.t = target
        return self


def fmt(sec):
    return "%d:%02d" % (int(sec) // 60, int(sec) % 60)


class Painter:
    """The pointer of bm Pixel, the tool and the colour: only the keys that change something are pressed."""
    PAL = 30                                    # colours of bm Studio's palette (after F3, 2); "." wraps at PAL+1

    def __init__(self, s):
        self.s, self.x, self.y, self.tool, self.ci = s, 0, 0, None, 1

    def key(self, k, what="", cap=None, hold=None):
        self.s.key(k, what, cap, hold)

    def use(self, k, what):
        if self.tool != k:
            self.key(k, what)
            self.tool = k

    def goto(self, x, y):
        while self.x < x:
            self.key("right"); self.x += 1
        while self.x > x:
            self.key("left"); self.x -= 1
        while self.y < y:
            self.key("down"); self.y += 1
        while self.y > y:
            self.key("up"); self.y -= 1

    def colour(self, n):
        """Colour n of bm Studio's palette: 1-9 and 0 (=10) are keys, then "." and "," step."""
        if n == self.ci:
            return
        name = art.COLOUR_NAMES.get(n, "colour %d" % n)
        if n <= 10:
            self.key(str(n % 10), "colour %d: %s" % (n, name))
        else:
            fwd = (n - 10) if self.ci < 10 else (n - self.ci) % (self.PAL + 1)
            base = 10 if self.ci < 10 else self.ci
            back = (base - n) % (self.PAL + 1)
            if self.ci < 10 or fwd <= back:
                if self.ci < 10:
                    self.key("0", "colour 10")
                for i in range(n - 10 if self.ci < 10 else fwd):
                    self.key(".", "next colour" if i == 0 else "", hold=2)
            else:
                for i in range(back):
                    self.key(",", "previous colour" if i == 0 else "", hold=2)
        self.ci = n

    def path(self, cells):
        todo, out, cur = list(cells), [], (self.x, self.y)
        while todo:
            todo.sort(key=lambda c: (abs(c[0] - cur[0]) + abs(c[1] - cur[1]), c[1], c[0]))
            cur = todo.pop(0)
            out.append(cur)
        return out

    def dab(self, x, y, what=""):
        self.goto(x, y)
        self.key("space", what)


def play(s, p, name, caps):
    """The program of tile `name` (art.TILES) as keys. caps: the caption of the first key of each tool."""
    for op in art.TILES[name]:
        kind = op[0]
        if kind == "fill":
            p.use("g", caps.get("fill", "g: fill"))
            p.colour(op[2])
            p.dab(op[1][0], op[1][1], "fill the tile")
        elif kind == "dots":
            p.use("b", caps.get("pencil", "b: pencil"))
            p.colour(op[1])
            for i, (x, y) in enumerate(p.path(op[2])):
                p.dab(x, y, "pencil" if i == 0 else "")
        elif kind in ("line", "oval"):
            p.use("l" if kind == "line" else "O", caps.get(kind, "l: line" if kind == "line" else "O: filled oval"))
            p.colour(op[1])
            (x0, y0), (x1, y1) = op[2], op[3]
            p.dab(x0, y0, "first point")
            p.dab(x1, y1, "second point")
            s.sec(0.4)


# ------------------------------------------------------------------------------------------ the Pixel part
def build_pixel(s):
    p = Painter(s)
    s.sec(PIXEL_START)                          # 0:00-0:08 is the hook (made from the other recordings)
    s.chapter("Open the project in bm Pixel")
    s.scene("bm Pixel ▸ start", [("Ctrl+O", "open"), ("F1 F2 F3", "the three pages")])
    s.say("bm Pixel is the pixel editor inside the console. Open our project with Control O. "
          "Textures are pieces of the same sprite sheet that bm Studio uses.")
    s.until(11, "Ctrl+O")
    s.shot("p_start")
    s.chord("o", "Ctrl+O: open")
    s.until(14, "Enter")
    s.shot("p_list")
    s.key("enter", "TROPIC.BME")
    s.until(16, "F1")
    s.key("f1", "F1: draw")
    s.scene("bm Pixel ▸ F1 draw", [("z", "sprite size"), ("F3", "palette"), ("2", "bm Studio's palette")])
    s.shot("p_draw")
    s.until(20, "z")
    s.chapter("Size and the palette of bm Studio")
    s.say("Draw at sixteen by sixteen: Z changes the size. On F3, key two loads the palette of bm Studio, "
          "so the colours match our models.")
    # a size change snaps the sprite to the grid of the new size: do it here, before going to the free row
    for i in range(5):                          # 16 -> 32 -> 64 -> 128 -> 8 -> 16: back to the size we want
        s.key("z", "z: sprite size" if i == 0 else "", hold=44)
    s.until(24, "F3")
    s.key("f3", "F3: palette")
    s.scene("bm Pixel ▸ F3 palette", [("F1", "draw"), ("2", "bm Studio's palette"), ("1", "the SDK's palette")])
    s.until(27, "2")
    s.shot("p_pal_sdk")
    s.key("2", "2: bm Studio's palette")
    s.sec(1)
    s.shot("p_pal_studio")
    # the free row of the sheet: the first 48 rows hold the starter tiles of bm Studio (art.ROW_Y)
    s.until(29, "F2")
    s.scene("bm Pixel ▸ F2 sheet", [("Arrows", "choose a sprite"), ("Enter", "draw it"), ("F1", "draw")])
    s.key("f2", "F2: the sheet")
    for _ in range(art.ROW_Y // 16):
        s.key("down", "next row of sprites", hold=14)
    s.sec(0.3)
    s.key("enter", "Enter: draw this sprite")
    s.scene("bm Pixel ▸ F1 draw", [("g", "fill"), ("b", "pencil"), ("l", "line"), ("O", "filled oval"), ("PgDn", "next sprite")])
    s.shot("p_row3")

    def tile(name, t0, title, text, caps, until, note=""):
        s.until(t0, name)
        s.chapter(title)
        s.say(text)
        p.tool = None                           # a new sprite: the caption of the tool again
        play(s, p, name, caps)
        s.sec(0.6)
        s.shot("p_" + name)
        s.until(until, name + " end")

    tile("grass", 32, "Grass",
         "First tile: grass. G fills the whole tile with green, then the pencil adds darker and lighter blades.",
         {"fill": "g: fill", "pencil": "b: pencil · blades"}, 52)

    s.until(52, "PgDn")
    s.key("pgdn", "PgDn: next sprite")
    tile("path", 54, "Path", "PgDn moves to the next sprite. A path: sandy brown, with a few darker pebbles.",
         {"fill": "Fill: sand", "pencil": "Pebbles and highlights"}, 67)

    s.key("pgdn", "PgDn: next sprite")
    tile("rock", 69, "Rock", "Rock: grey, a diagonal crack with L, and a lighter patch with the filled oval.",
         {"fill": "PgDn · fill grey", "line": "l: line · a crack", "oval": "O: filled oval"}, 82)

    s.key("pgdn", "PgDn: next sprite")
    tile("wood", 84, "Wood", "Wood: brown planks, dark lines between them, and a few dots of grain.",
         {"fill": "PgDn · fill brown", "line": "l: plank lines", "pencil": "b: grain"}, 97)

    s.until(97, "PgDn skin")
    s.key("pgdn", "PgDn: next sprite")
    tile("skin", 98, "Pico's skin",
         "Pico's skin is mint green with darker scales. Copy it with Control C, move on, paste with Control V "
         "and press Enter: this copy will get the eyes.",
         {"fill": "PgDn · fill mint", "pencil": "b: scales"}, 106)
    s.chord("c", "Ctrl+C: copy")
    s.until(107, "PgDn face")
    s.key("pgdn", "PgDn")
    s.scene("bm Pixel ▸ F1 face", [("Ctrl+C", "copy"), ("Ctrl+V", "paste"), ("Enter", "put it down")])
    p.goto(0, 0)                                # a paste lands where the pointer is
    s.until(108, "Ctrl+V")
    s.chord("v", "Ctrl+V: paste")
    s.until(110, "Enter")
    s.key("enter", "Enter: put it down")
    s.sec(0.6)
    s.shot("p_face")
    s.until(112, "apple")
    s.scene("bm Pixel ▸ F1 draw", [("g", "fill"), ("b", "pencil"), ("O", "filled oval"), ("PgDn", "next sprite")])
    s.key("pgdn", "PgDn: next sprite")
    tile("apple", 113, "Apple and the HUD icon", "The apple: red, with a lighter shine drawn with the oval.",
         {"fill": "PgDn · fill red", "oval": "O: shine", "pencil": "b: shading"}, 124)

    s.key("pgdn", "PgDn: HUD icon")
    tile("apple_icon", 125, "Apple icon",
         "And a small apple icon for the score, on a transparent background. We will draw it in the HUD later.",
         {"oval": "O: filled oval", "pencil": "Stem and leaf"}, 136)

    s.chapter("The sheet and save")
    s.scene("bm Pixel ▸ F2 sheet", [("F2", "the sheet"), ("Ctrl+S", "save")])
    s.say("F2 shows the whole sheet. Control S saves it in the project.")
    s.key("f2", "F2: the sheet")
    s.until(140, "Ctrl+S")
    s.shot("p_sheet")
    s.chord("s", "Ctrl+S: save")
    s.until(141.3, "saved")
    s.sec(1)
    s.shot("p_saved")
    s.until(PIXEL_END, "end of Pixel")
    s.end()


# ---------------------------------------------------------------------------------------------- the run
def build(cal, session):
    art.check()
    Ep.late = []
    s = Ep()
    s.chapter("Textures for Tropic Trail")
    build_pixel(s)
    studio_part.build_studio(s, cal, session, STUDIO_START, STUDIO_END)
    return s


def split_input(lines, a, b):
    """The lines of the input with a <= frame < b, frames restarting at 0."""
    out = []
    for ln in lines:
        f, rest = ln.split(" ", 1)
        if a <= int(f) < b:
            out.append("%d %s" % (int(f) - a, rest))
    return out


def write_lines(path, lines):
    with open(path, "w", newline="\n") as f:          # LF even from Windows (a CR would be an Enter key)
        f.write("\n".join(lines) + "\n")


def build_editors():
    os.makedirs(BIN, exist_ok=True)
    mk = [sys.executable, os.path.join(ROOT, "scripts", "mkbm.py")]
    subprocess.check_call(mk + ["-o", os.path.join(BIN, "pixel.bm"), "--lua", "carts/pixel/main.lua", "--cover",
                                "carts/pixel/icon.png", "--info", "carts/pixel/bm.info"], cwd=ROOT)
    subprocess.check_call(mk + ["-o", os.path.join(BIN, "studio.bm"), "--lua", "carts/studio/main.lua", "--cover",
                                "carts/studio/icon.png", "--sheet", "carts/studio/assets/sheet.png", "--sheet8",
                                "--info", "carts/studio/bm.info"], cwd=ROOT)


def start_file(kind):
    chain = os.path.join(lib3d.CHAIN, "01-studio.bme")
    if kind == "auto":
        kind = "chain" if os.path.exists(chain) else "forge"
    if kind == "chain":
        return chain, "chain"
    sys.path.insert(0, HERE)
    import standin
    return standin.build(os.path.join(ROOT, "video", "3d", "out", "standin", "TROPIC.BME")), "forge"


def sha(*paths):
    h = hashlib.sha1()
    for p in paths:
        h.update(open(p, "rb").read())
    return h.hexdigest()


def calibration(src):
    """Which arrows reach which face of each model (calib.py), cached in out/calib.json by what it depends on."""
    key = sha(src, os.path.join(ROOT, "carts", "studio", "main.lua"), os.path.join(ROOT, "src", "script", "bm3d.lua"),
              os.path.join(HERE, "calib.py"))
    cache = os.path.join(OUT, "calib.json")
    if os.path.exists(cache):
        d = json.load(open(cache))
        if d.get("key") == key:
            return d["models"]
    if not os.path.exists(lib3d.BMHOST):
        raise SystemExit("no build/host/bmhost-bin and no valid out/calib.json")
    sd = lib3d.fresh_sd(os.path.join(OUT, "sd-calib"), [(src, "TROPIC.BME")])
    models = calib.dump(sd, os.path.join(OUT, "dbg"))
    json.dump({"key": key, "models": models}, open(cache, "w"))
    return models


def main():
    args = sys.argv[1:]
    opt = lambda n, d=None: args[args.index(n) + 1] if n in args else d
    os.makedirs(OUT, exist_ok=True)
    src, kind = start_file(opt("--start", "auto"))
    shutil.copy(src, os.path.join(OUT, "start.bme"))                 # what verify.py compares the models with
    print("start:", kind, os.path.relpath(src, ROOT))
    cal = calibration(src)
    sd_cal = os.path.join(OUT, "sd-calib")
    key = sha(src, os.path.join(ROOT, "carts", "studio", "main.lua"), os.path.join(ROOT, "src", "script", "bm3d.lua"),
              os.path.join(HERE, "calib.py"))
    lib3d.fresh_sd(sd_cal, [(src, "TROPIC.BME")])
    session = calib.Session(sd_cal, os.path.join(OUT, "dbg"), os.path.join(OUT, "plan_cache.json"), key)
    s = build(cal, session)
    session.save()
    print("pointer plan: %d replays of the editor (cached: the rest)" % session.runs)
    write_lines(os.path.join(OUT, "input.txt"), s.lines)
    pix = split_input(s.lines, PIXEL_START * FPS, STUDIO_START * FPS - 1)
    stu = split_input(s.lines, STUDIO_START * FPS, 10 ** 9)
    write_lines(os.path.join(OUT, "input-pixel.txt"), pix)
    write_lines(os.path.join(OUT, "input-studio.txt"), stu)
    lib3d.write_overlay(s, os.path.join(OUT, "overlay.json"), TITLE)
    print("keys: %d lines, %.1f s (target %s)" % (len(s.lines), s.t / FPS, fmt(STUDIO_END)))
    for m in Ep.late:
        print("LATE:", m)
    if "--build-only" in args:
        return 1 if Ep.late else 0
    stage = opt("--stage", "all")
    build_editors()
    shots = os.path.join(OUT, "shots")
    shutil.rmtree(shots, ignore_errors=True)
    os.makedirs(shots, exist_ok=True)
    if stage in ("all", "pixel"):
        lib3d.fresh_sd(SD, [(src, "TROPIC.BME")])
        r = lib3d.run_headless(os.path.join(BIN, "pixel.bm"), os.path.join(OUT, "input-pixel.txt"),
                               PIXEL_END - PIXEL_START, SD, shots=shots)
        print(r.stdout[-400:], r.stderr[-400:])
    if stage in ("all", "studio"):
        if not os.path.isdir(os.path.join(SD, "carts")):
            raise SystemExit("no SD: run the pixel stage first")
        r = lib3d.run_headless(os.path.join(BIN, "studio.bm"), os.path.join(OUT, "input-studio.txt"),
                               STUDIO_END - STUDIO_START, SD, shots=shots)
        print(r.stdout[-400:], r.stderr[-400:])
    saved = os.path.join(SD, "carts", "TROPIC.BME")
    if stage == "all" and os.path.exists(saved):
        lib3d.chain_put(saved, "02-pixel.bme")
        f = bmres.read(saved)
        f.header = bytearray(f.header)
        struct.pack_into("<H", f.header, 18, struct.unpack_from("<H", f.header, 18)[0] & ~1)   # .bm: not a project
        bmres.write(os.path.join(lib3d.CHAIN, "02-pixel.bm"), f)
        print("chain: video/3d/chain/02-pixel.bm")
    if not lib3d.have_ffmpeg() or "--no-video" in args:
        print("no video (ffmpeg missing or --no-video): keys, overlay, shots and the project are in", OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
