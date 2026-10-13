#!/usr/bin/env python3
"""Tropic Trail, video 1: bm Studio. Pico, Apple and Crate built from blocks with the console's own editor
(carts/studio/main.lua) run by bmhost --tool, from an empty project and a clean SD.

  python3 video/3d/01-studio/record.py [--build-only] [--stage N] [--log] [--no-video] [--cart FILE]

Writes video/3d/01-studio/out/: input.txt (the keys, frame at 60 fps), overlay.json (key caps, scenes, narration, chapters),
shots/ (PNG after each shot-list row), sd/carts/TROPIC.BME (what the editor saved), and the mp4 when ffmpeg is there
(--no-video is the default where ffmpeg is missing: nothing else changes). Deterministic: the same script, the same run.

The editor saves a PROJECT (.bme), not a .bm. The chain (video/3d/chain/) keeps both:
  chain/01-studio.bme   the very file bm Studio saved (the next video starts from it: `.bm` are read only in the editors)
  chain/01-studio.bm    the real step: `bm_forge.py export models TROPIC.BME -o chain/01-studio.bm --title tropic`
                        (the cartridge of the models; the SDK's Ctrl+B would give the game, Studio has no build key)
--start is accepted for uniformity with the other episodes: this one has no previous video (empty project, clean SD).
The 0:00-0:08 hook of the shot list needs the finished models: it is cut in post from the end of the run (hook_cmd),
so the first 8 s of the recording show the Studio menu waiting (the Dev tab shot, 0:08, is a QEMU/board card: PIANO D1).
Studio needs build/host/bmhost-bin and its cartridge: build/studio.bm is used when it runs, else the same cartridge is
made here from carts/studio with scripts/mkbm.py (out/studio.bm), so the run never depends on a stale build.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import lib3d                                         # noqa: E402  (also puts video/lib and scripts on sys.path)
from lib3d import ROOT, FPS                           # noqa: E402
from bmvideo import Script                            # noqa: E402

OUT = os.path.join(HERE, "out")
SD = os.path.join(OUT, "sd")
SHOTS = os.path.join(OUT, "shots")
TITLE = "TROPIC TRAIL  ·  1  ·  bm Studio"
BME = os.path.join(SD, "carts", "TROPIC.BME")

# the colours of bm Studio's palette (T.PALETTE, 1-based), 6 per row on the picker
# the arrows from the corner the vertex pointer stands on (-1,2,2) to each corner of the top face of the middle
# block of APPLE, in the order chosen: found with navsearch.py on the real editor (the camera eases, so a table is not enough)
APPLE_CORNERS = [(["right", "right"], "(0,3,1)"), (["right"], "(1,3,1)"),
                 (["down", "down", "right", "right"], "(1,3,0)"), (["down", "down", "left", "right", "right"], "(0,3,0)")]
PAL = {"brown": 14, "red": 9, "orange": 11, "mint": 26, "yellow": 12, "cream": 13}


class Rec:
    """The script of the run: keys, with the cursor of the build page followed on the side."""

    def __init__(self):
        self.s = Script()
        self.x = self.z = self.y = 0         # the cell of the cursor (view turned 0: up = +z, right = +x)
        self.pal = 1                         # the swatch the colour picker is on
        self.colours = False                 # the picker opens on the colours (it remembers)
        self.late = []

    # ---------------------------------------------------------------- time
    def at(self, sec):
        """Wait until `sec` of the shot list. A row that overruns is reported, not hidden."""
        f = round(sec * FPS)
        if self.s.t > f:
            self.late.append((sec, (self.s.t - f) / FPS))
        else:
            self.s.wait(f - self.s.t)
        return self

    def shot(self, name):
        self.s.shot(name)

    # ------------------------------------------------------------- the keys
    def key(self, name, what="", hold=None):
        self.s.key(name, what, hold=hold)

    def keys(self, name, n, what="", hold=5):
        for i in range(n):
            self.s.key(name, what if i == 0 else "", hold=hold)

    def name_box(self, old, new, what):
        """A text box with the old text already in it: Backspace it away, type the new one, Enter."""
        self.keys("bksp", len(old), "Backspace: clear the name", hold=3)
        self.s.type_text(new, what, per_key=4)
        self.s.sec(0.5)
        self.key("enter", hold=3)

    def go(self, x, z, y=None, hold=5):
        """The cursor to the cell (x, z) at level y: level first, then the walk, one arrow at a time."""
        if y is not None:
            while self.y < y:
                self.s.key("pgup", hold=hold); self.y += 1
            while self.y > y:
                self.s.key("pgdn", hold=hold); self.y -= 1
        while self.x < x:
            self.s.key("right", hold=hold); self.x += 1
        while self.x > x:
            self.s.key("left", hold=hold); self.x -= 1
        while self.z < z:
            self.s.key("up", hold=hold); self.z += 1
        while self.z > z:
            self.s.key("down", hold=hold); self.z -= 1

    def put(self, x, z, y=None, what="", hold=5):
        self.go(x, z, y, hold)
        self.s.key("space", what, hold=hold)

    def swatch(self, colour, what, first=False):
        """Tab, (c once), the arrows to the colour, Enter. The picker remembers the colours page and the swatch."""
        s = self.s
        s.key("tab", "Tab: the sheet" if first else "", hold=10)
        if not self.colours:
            s.key("c", "c: tiles or colours", hold=20)
            self.colours = True
        target = PAL[colour]
        delta = target - self.pal
        best = None
        for d in (delta, delta - 30, delta + 30):          # a row is 6 swatches; 30 swatches wrap round
            for v in range(-5, 6):
                h = d - 6 * v
                cost = abs(v) + abs(h)
                if best is None or cost < best[0]:
                    best = (cost, v, h)
        _, v, h = best
        for _ in range(abs(v)):
            s.key("down" if v > 0 else "up", hold=8)
        for _ in range(abs(h)):
            s.key("right" if h > 0 else "left", hold=8)
        self.pal = target
        s.sec(0.5)
        s.key("enter", what, hold=10)
        s.sec(0.5)


def build():
    r = Rec()
    s = r.s

    # ---- 0:00 hook (cut in post); Studio waits on its menu ------------------------------------------------------
    s.scene("bm Studio ▸ menu", [("F1", "build"), ("F2", "models"), ("Esc", "menu"), ("Ctrl+N", "new project")])
    s.chapter("Pico, Apple and Crate")
    s.say("Pico, an apple and a crate: three 3D models, built from nothing but blocks, inside the console. Let's make them.")
    r.at(8)
    s.chapter("Open bm Studio and a new project")
    s.scene("Dev ▸ bm Studio", [("Esc", "the menu"), ("Ctrl+N", "new project"), ("Ctrl+S", "save")])
    s.say("bm Studio is the 3D modelling editor of bm. Open it from the Dev tab, press Escape, and choose New project. "
          "We save it right away as TROPIC.")
    # 0:08 Dev ▸ bm Studio: card; the editor starts on its menu, Esc goes to the page and Esc again to the menu
    s.mark("Dev ▸ bm Studio", "Dev tab ▸ bm Studio")
    r.at(11)
    r.key("esc", "Esc: the menu")                       # menu -> the build page it came from
    s.sec(0.5)
    r.key("esc", "Esc: the menu")                       # build page -> menu
    r.at(13)
    r.shot("r03")
    s.chord("n", "Ctrl+N: new project")
    s.sec(1.2)
    r.at(16)
    s.chord("s", "Ctrl+S: save")
    s.sec(0.6)
    r.shot("r05")
    r.name_box("MY3D.BME", "TROPIC.BME", "TROPIC.BME saved in /carts")  # Backspace x8: the box holds MY3D.BME
    s.sec(1.0)
    r.shot("r07")

    # ---- 0:20 the two pages and the key help ------------------------------------------------------------------
    r.at(20)
    s.chapter("The two pages and the key help")
    s.say("Two pages: F1 to build, F2 for the list of models. Press the question mark at any time to see the keys of the "
          "page you are on.")
    s.scene("bm Studio ▸ F1 build", [("F1", "build"), ("F2", "models"), ("?", "keys of the page (hold)")])
    r.key("f1", "F1: build")
    r.at(23)
    s.scene("bm Studio ▸ F2 models", [("F1", "build"), ("F2", "models"), ("?", "keys of the page (hold)")])
    r.key("f2", "F2: models")
    r.at(26)
    # F12 held (usage 0x45): the host has no "held ?" key, F12 is the same help
    s.mark("F12 held", "F12: keys of the page")
    s.lines.append("%d keys 0x45" % s.t)
    s.sec(2.5)
    s.shot("r10_help")                                  # the key list on screen (F12 held)
    s.sec(2.5)
    s.lines.append("%d keys none" % s.t)
    s.sec(0.5)
    r.shot("r10")

    # ---- 0:32 blocks and the 3D view ------------------------------------------------------------------------
    r.at(32)
    s.chapter("Blocks and the 3D view")
    s.say("Tool one is the block. Arrows move the cursor, Space puts a cube. Page Up lifts the cursor one level, and a "
          "second cube sits on top. Q turns the view by forty-five degrees, so you can check every side. Backspace takes "
          "a block away. One cube is all a crate needs.")
    s.scene("bm Studio ▸ F1 build ▸ block", [("1", "block tool"), ("Space", "put a block"), ("PgUp/PgDn", "level"),
                                            ("q / e", "turn the view"), ("Backspace", "remove")])
    r.key("f1", "F1: build")
    s.sec(0.5)
    r.key("1", "1: block tool")
    r.at(36)
    r.key("space", "Space: put a block")
    r.at(40)
    r.key("pgup", "PgUp: one level up"); r.y = 1
    r.at(43)
    r.key("space", "Space: second block")
    r.at(47)
    for i in range(3):
        r.key("q", "q: turn the view 45 degrees" if i == 0 else "", hold=40)
    s.sec(1.0)
    r.shot("r15")
    r.at(53)
    for i in range(3):
        r.key("e", "e: turn back" if i == 0 else "", hold=40)
    s.sec(1.0)
    r.at(57)
    r.key("bksp", "Backspace: remove the block")
    r.at(61)
    s.chord("z", "Ctrl+Z undo")
    s.sec(1.5)
    s.chord("y", "Ctrl+Y redo")
    s.sec(1.0)
    r.shot("r18")

    # ---- 1:05 the colour brush --------------------------------------------------------------------------------
    r.at(65)
    s.chapter("The colour brush")
    s.say("Tab opens the sheet. C switches from tiles to colours. Pick a brown, press Enter, and the faces take that flat "
          "colour. Textures come in the next video.")
    s.scene("bm Studio ▸ F1 build ▸ brush", [("Tab", "the sheet"), ("c", "tiles / colours"), ("Enter", "take as brush")])
    r.key("tab", "Tab: the sheet", hold=10)
    r.at(68)
    r.key("c", "c: tiles or colours", hold=20)
    r.colours = True
    r.at(70)
    r.colours = True
    # the swatch keys (inline: the Tab and c are on the shot list rows 19 and 20)
    r.pal = 1
    delta = PAL["brown"] - r.pal
    for k in ("down", "down", "right"):                  # 1 -> 7 -> 13 -> 14
        r.key(k, "Pick a brown" if k == "down" else "", hold=15)
    r.pal = PAL["brown"]
    s.sec(1.0)
    r.shot("r21")
    r.at(73)
    r.key("enter", "Enter: take it as the brush", hold=10)
    r.at(75)
    s.scene("bm Studio ▸ F1 build ▸ block", [("PgDn", "level down"), ("Backspace", "remove"), ("Space", "put a block")])
    r.key("pgdn", "", hold=8); r.y = 0                   # the cursor is on level 1: back to the cube
    r.key("bksp", "", hold=10)
    r.key("space", "Flat colour, no texture", hold=10)
    s.sec(1.5)
    r.shot("r23")

    # ---- 1:20 the models page: CRATE ---------------------------------------------------------------------------
    r.at(80)
    s.chapter("The models page: CRATE")
    s.say("F2 lists the models. R renames: this one is CRATE. Twelve triangles, far below our budget of forty.")
    s.scene("bm Studio ▸ F2 models", [("r", "rename"), ("n", "new model"), ("-", "reduce triangles"), ("Enter", "build it")])
    r.key("f2", "F2: the model list")
    r.at(84)
    r.key("r", "r: rename", hold=20)
    r.name_box("model", "CRATE", "CRATE")
    r.at(91)
    s.sec(0.5)
    r.shot("r27")
    r.at(95)

    # ---- 1:35 APPLE from blocks --------------------------------------------------------------------------------
    s.chapter("APPLE from blocks")
    s.say("N makes a new model: APPLE. Three layers: a plus, a full square, a plus again. Fifty-four faces, one hundred and "
          "eight triangles.")
    r.key("n", "n: new model", hold=30)                  # the new model opens on the build page
    r.x = r.z = r.y = 0                                  # a new model: the cursor is on cell 0,0,0 again
    r.at(98)
    s.scene("bm Studio ▸ F2 models", [("r", "rename"), ("n", "new model"), ("Enter", "build it")])
    r.key("f2", "F2: the model list")
    r.key("r", "r: rename", hold=15)
    r.name_box("model", "APPLE", "APPLE")
    r.at(101)
    r.key("enter", "Enter: build it", hold=20)
    r.at(103)
    s.scene("bm Studio ▸ F1 build ▸ brush", [("Tab", "the sheet"), ("Enter", "take as brush")])
    r.swatch("red", "Red brush", first=True)
    r.at(107)
    s.scene("bm Studio ▸ F1 build ▸ block", [("1", "block tool"), ("PgUp", "level up"), ("Space", "put a block")])
    r.key("1", "1: block tool")
    # layer 1: a plus; layer 2: the 3x3 square; layer 3: a plus
    plus = [(0, 0), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1), (1, 0)]
    plus_cells = [(0, 0), (0, 1), (0, -1), (-1, 0), (1, 0)]
    square = [(1, 1), (0, 1), (-1, 1), (-1, 0), (0, 0), (1, 0), (1, -1), (0, -1), (-1, -1)]
    first = True
    for c in [(0, 0), (0, 1), (0, -1), (-1, 0), (1, 0)]:
        r.put(c[0], c[1], 0, "Layer 1: a plus" if first else "", hold=6)
        first = False
    r.at(112)
    first = True
    for c in square:
        r.put(c[0], c[1], 1, "Layer 2: a full square" if first else "", hold=6)
        first = False
    r.at(116)
    first = True
    for c in [(1, 0), (0, 0), (0, 1), (0, -1), (-1, 0)]:
        r.put(c[0], c[1], 2, "Layer 3: a plus again" if first else "", hold=6)
        first = False
    r.at(119)
    r.shot("r35")

    # ---- 2:00 moving corners -----------------------------------------------------------------------------------
    r.at(120)
    s.chapter("Moving corners")
    s.say("Tool four moves corners. Choose the four corners at the top centre, press G, and push them down a little: the "
          "dimple of an apple.")
    s.scene("bm Studio ▸ F1 build ▸ vertex", [("4", "vertex tool"), ("Space", "choose a corner"), ("g", "move"),
                                              ("Tab", "the step"), ("Enter", "leave it there")])
    r.key("4", "4: vertex tool")
    r.at(123)
    if probe(r, "vertex"):
        return r
    # the pointer already stands on a corner (-1,2,2); the arrows of the real transition table (navplan.py) to the four
    # corners of the top face of the middle block, a Space on each
    for i, (steps, what) in enumerate(APPLE_CORNERS):
        for d in steps:
            r.key(d, "", hold=15)
        r.key("space", "Space: choose a corner" if i == 0 else "", hold=20)
    r.at(128)
    r.shot("r37")
    r.key("g", "g: move", hold=20)
    r.at(129)
    r.keys("tab", 2, "Tab: the step (1/4)", hold=15)    # the step goes 1, 1/2, 1/4
    r.at(131)
    r.key("pgdn", "PgDn: down a little", hold=30)
    r.shot("r40")
    r.at(133)
    r.key("enter", "Enter: leave it there", hold=20)
    r.shot("r41")

    # ---- 2:15 reducing triangles -------------------------------------------------------------------------------
    r.at(135)
    s.chapter("Reducing triangles")
    s.say("One hundred and eight is over our budget of eighty. Press minus, type eighty, and Enter. The reducer keeps the "
          "borders. Control Z would undo it.")
    s.scene("bm Studio ▸ F2 models ▸ reduce", [("-", "reduce triangles"), ("Enter", "do it"), ("Ctrl+Z", "undo"),
                                              ("Ctrl+Y", "redo")])
    r.key("f2", "F2: the counts")
    r.at(138)
    r.key("-", "-: reduce the triangles", hold=30)
    r.shot("r43")
    r.at(141)
    r.name_box("54", "80", "Wanted: 80")
    r.at(144)
    r.shot("r45")
    r.at(146)
    s.chord("z", "Ctrl+Z undoes it")
    s.sec(1.0)
    r.shot("r46a")
    s.chord("y", "Ctrl+Y redo")
    s.sec(0.8)
    r.shot("r46")

    # ---- 2:30 PICO from blocks -----------------------------------------------------------------------------------
    r.at(150)
    s.chapter("PICO from blocks")
    s.say("Now Pico. N, then the name. Orange for the legs, mint green for the body, head and arms, and a tail that ends in "
          "yellow. Sixteen blocks, in time-lapse.")
    s.scene("bm Studio ▸ F2 models", [("n", "new model"), ("r", "rename"), ("Enter", "build it")])
    r.key("n", "n: new model", hold=30)                  # the new model opens on the build page
    r.x = r.z = r.y = 0                                  # a new model: the cursor is on cell 0,0,0 again
    r.key("f2", "F2: the model list")
    r.key("r", "r: rename", hold=15)
    r.name_box("model", "PICO", "PICO")
    r.at(156)
    r.key("enter", "Enter: build it", hold=20)
    r.at(158)
    s.scene("bm Studio ▸ F1 build ▸ block", [("Tab", "the sheet"), ("Space", "put a block"), ("PgUp/PgDn", "level"),
                                            ("Enter", "take as brush")])
    r.swatch("orange", "Orange brush: the legs")
    r.key("1", "1: block tool")
    r.at(161)
    r.put(0, 0, 0, "Legs", hold=6)
    r.put(2, 0, 0, hold=6)
    r.at(164)
    r.swatch("mint", "Mint green brush")
    r.at(167)
    for x in (2, 1, 0):
        r.put(x, 0, 1, "Body, row 1" if x == 2 else "", hold=6)
    r.at(170)
    for x in (0, 1, 2):
        r.put(x, 0, 2, "Body, row 2" if x == 0 else "", hold=6)
    r.at(173)
    for x in (2, 1, 0):
        r.put(x, 0, 3, "Head" if x == 2 else "", hold=6)
    r.at(176)
    r.put(-1, 0, 2, "Arms", hold=6)
    r.put(3, 0, 2, hold=6)
    r.at(178)
    r.put(1, -1, 1, "Tail", hold=6)
    r.put(1, -2, 1, hold=6)
    r.at(180)
    r.swatch("yellow", "Yellow brush: tail tip")
    r.at(182)
    r.put(1, -3, 1, "Tail tip", hold=6)
    r.at(184)
    r.shot("r58")
    s.chapter("Selecting and moving faces")
    s.say("Tool three selects faces. Space on the six front faces of the body, then Enter puts the brush on them: a cream "
          "belly.")
    s.scene("bm Studio ▸ F1 build ▸ select", [("3", "select tool"), ("Space", "choose a face"), ("Enter", "put the brush"),
                                             ("g", "move"), ("PgUp", "up"), ("Esc", "put it back")])
    r.swatch("cream", "Cream brush: the belly")
    r.key("3", "3: select tool")
    r.keys("q", 4, "q: turn the view to see the belly", hold=16)
    r.key("z", "z: frame the whole model", hold=10)
    r.keys("-", 3, "-: zoom out", hold=6)
    s.sec(0.6)
    if probe(r, "belly"):
        return r
    for i, steps in enumerate(BELLY):
        for d in steps:
            r.key(d, "", hold=6)
        r.key("space", "Space: choose a face" if i == 0 else "", hold=8)
    r.shot("r61")
    r.key("enter", "Enter: put the brush on the faces", hold=30)
    r.shot("r62")
    r.key("a", "a: release them all", hold=20)
    r.keys("e", 4, "e: turn the view back", hold=16)
    s.sec(0.6)
    if probe(r, "tip"):
        return r
    for d in TIP:
        r.key(d, "", hold=6)
    r.key("space", "Choose the tail end", hold=10)
    r.shot("r63")
    r.key("g", "g: move", hold=15)
    r.key("pgup", "PgUp: lift", hold=30)
    r.shot("r65")
    r.key("enter", "Enter: leave it there", hold=20)
    r.shot("r66")
    if probe(r, "end"):
        return r

    # ---- 3:16 check and save ---------------------------------------------------------------------------------
    r.at(196)
    s.chapter("Check and save")
    s.say("F2 shows the counts: Pico is well under three hundred triangles. Control S saves the project.")
    s.scene("bm Studio ▸ F2 models", [("F2", "the counts"), ("Ctrl+S", "save")])
    r.key("f2", "F2: the counts", hold=30)
    s.sec(1.0)
    r.shot("r67")
    r.at(199)
    s.chord("s", "Ctrl+S: save")
    s.sec(1.0)
    r.shot("r68")
    r.at(200)
    s.say("Next time: bm Pixel. We draw grass, path, rock, wood and skin, and paint these models.")
    s.scene("bm Studio ▸ F2 models", [("Up / Down", "the list")])
    for k in ("up", "up", "down", "down"):
        r.key(k, "Next: bm Pixel, textures" if k == "up" and r.s.t < 12100 else "", hold=100)
    r.at(210)
    r.shot("r69")
    return r


BELLY = [["down", "right", "down", "down"], ["left"], ["down", "left"], ["up"], ["down", "right", "up"],
         ["down", "right", "up"]]     # the arrows to the six front faces of the body (navsearch.py select ...), a Space after each
TIP = ["down", "down", "right", "right", "left", "down", "down"]       # the arrows to the end face of the tail


def probe(r, tag):
    """Development only: keys from out/probe.txt (one key name per line, `shot NAME`, `wait SECONDS`) after the script."""
    f = os.path.join(OUT, "probe.txt")
    if os.path.exists(f) and os.environ.get("STUDIO_PROBE") == tag:
        for line in open(f, encoding="utf-8").read().splitlines():
            w = line.split()
            if not w:
                continue
            if w[0] == "shot":
                r.s.shot(w[1])
            elif w[0] == "wait":
                r.s.sec(float(w[1]))
            elif w[0] == "type":
                r.s.type_text(w[1])
            else:
                r.s.key(w[0], hold=20 if w[0] in ("space", "enter") else 15)
        r.s.sec(1)
        return True
    return False


LOG_PATCH = '''
local _say0 = T.say
T.say = function(t, ...) log("F" .. tostring(S.frame) .. " SAY " .. tostring(t)); return _say0(t, ...) end
'''


def logged_lua(src):
    """carts/studio/main.lua with log() lines (key, tool, cell, counts, pointer): only for finding the keys."""
    t = src.replace("\r\n", "\n")
    t = t.replace("local M, say = T.M, T.say", LOG_PATCH + "local M, say = T.M, T.say", 1)
    t = t.replace("function build.key(k)", "function build.key0(k)", 1)
    t = t.replace("function build.pad()", '''function build.key(k)
  build.key0(k)
  local hp = bd.hotp and bd.pts and bd.pts[bd.hotp]
  local hf = bd.hot and faces()[bd.hot]
  local nt = M() and M().nt
  local nsel = 0
  for _ in pairs(bd.sel) do nsel = nsel + 1 end
  local np = 0
  for _ in pairs(bd.psel) do np = np + 1 end
  log(string.format("F%d BK %q tool=%d cell=%d,%d,%d faces=%d tri=%s hotp=%s hotf=%s sel=%d psel=%d move=%s yaw=%.2f",
    S.frame, k, bd.tool, bd.cell[1], bd.cell[2], bd.cell[3], #faces(), tostring(nt),
    hp and string.format("%.2f,%.2f,%.2f", hp.p[1], hp.p[2], hp.p[3]) or "-",
    hf and string.format("c=%.2f,%.2f,%.2f n=%.0f,%.0f,%.0f", T.face_center(hf)[1], T.face_center(hf)[2], T.face_center(hf)[3],
      T.face_normal(hf)[1], T.face_normal(hf)[2], T.face_normal(hf)[3]) or "-",
    nsel, np, bd.move and string.format("%.3f", bd.move.step) or "-", bd.yaw_to))
  if k == "j" and (bd.tool == 3 or bd.tool == 4) then      -- the table of the pointer: where each arrow goes from each one
    local pts = bd.tool == 4
    local n = pts and #points() or #faces()
    local pos = pts and point_pos or face_pos
    for i = 1, n do
      local row = {}
      for _, d in ipairs({ "left", "right", "up", "down" }) do row[#row + 1] = tostring(T.nav(n, i, d, pos)) end
      local info
      if pts then
        local p = points()[i].p
        info = string.format("%.2f,%.2f,%.2f", p[1], p[2], p[3])
      else
        local f = faces()[i]
        local c, nn = T.face_center(f), T.face_normal(f)
        info = string.format("%.2f,%.2f,%.2f n=%.0f,%.0f,%.0f", c[1], c[2], c[3], nn[1], nn[2], nn[3])
      end
      log(string.format("NAV %d %s | %s", i, info, table.concat(row, " ")))
    end
    local init = pts and T.nav(n, nil, "right", pos) or (function() local h = hot_face(); return h and bd.hot end)()
    log("NAVINIT " .. tostring(init) .. " n=" .. n)
  end
end

function build.pad()''', 1)
    t = t.replace("function models_page.key(k)", "function models_page.key0(k)", 1)
    t = t.replace("function models_page.pad()", '''function models_page.key(k)
  models_page.key0(k)
  local m = M()
  log(string.format("F%d MK %q model=%d/%d %s tri=%s faces=%s", S.frame, k, S.cur, #S.models, m and m.name or "-",
    tostring(m and m.nt), tostring(m and #m.faces)))
end

function models_page.pad()''', 1)
    return t


def make_cart(log=False, cart=None):
    """The cartridge of bm Studio: `cart`, or build/studio.bm when it runs, else made here (out/studio.bm)."""
    os.makedirs(OUT, exist_ok=True)
    src = os.path.join(ROOT, "carts", "studio")
    out = os.path.join(OUT, "studio_log.bm" if log else "studio.bm")
    if cart and not log:
        return cart
    lua = os.path.join(src, "main.lua")
    if log:
        lua = os.path.join(OUT, "main_log.lua")
        with open(os.path.join(src, "main.lua"), encoding="utf-8", newline="") as f:
            open(lua, "w", encoding="utf-8", newline="\n").write(logged_lua(f.read()))
    cmd = [sys.executable, os.path.join(ROOT, "scripts", "mkbm.py"), "-o", out, "--lua", lua,
           "--cover", os.path.join(src, "icon.png"), "--sheet", os.path.join(src, "assets", "sheet.png"), "--sheet8",
           "--info", os.path.join(src, "bm.info")]
    subprocess.run(cmd, check=True, cwd=ROOT, stdout=subprocess.DEVNULL)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-only", action="store_true", help="only write input.txt and overlay.json")
    ap.add_argument("--seconds", type=float, help="stop the run at this time (default: the end of the script)")
    ap.add_argument("--stage", type=float, help="alias of --seconds")
    ap.add_argument("--log", action="store_true", help="the instrumented cartridge, log on the terminal")
    ap.add_argument("--no-video", action="store_true", help="no mp4 (the default where ffmpeg is missing)")
    ap.add_argument("--cart", help="a bm Studio cartridge to use instead of the one made here")
    ap.add_argument("--start", choices=("forge", "chain"), help="accepted, unused: this video starts from an empty project")
    a = ap.parse_args()
    r = build()
    s = r.s.end()
    os.makedirs(OUT, exist_ok=True)
    inp = os.path.join(OUT, "input.txt")
    s.write_input(inp)
    lib3d.write_overlay(s, os.path.join(OUT, "overlay.json"), TITLE)
    for sec, over in r.late:
        print("LATE: the row of %.1f s starts %.1f s late" % (sec, over), file=sys.stderr)
    print("script: %d frames = %.1f s" % (s.t, s.t / FPS))
    if a.build_only:
        return 0
    seconds = a.seconds or a.stage or (s.t + 60) / FPS
    lib3d.fresh_sd(SD)
    shutil.rmtree(SHOTS, ignore_errors=True)
    cart = make_cart(a.log, a.cart)
    if a.log:
        cmd = [lib3d.BMHOST, cart, "--sd", SD, "--seconds", str(seconds), "--input", inp, "--tool", "--shots", SHOTS]
        os.makedirs(SHOTS, exist_ok=True)
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        open(os.path.join(OUT, "run.log"), "w", encoding="utf-8").write(p.stdout + p.stderr)
        print(p.stdout[-6000:] + p.stderr[-500:])
        return p.returncode
    p = lib3d.run_headless(cart, inp, seconds, SD, shots=SHOTS)
    print(p.stdout[-800:] + p.stderr[-800:])
    if p.returncode or not os.path.exists(BME):
        print("the run failed or saved nothing")
        return 1
    # the chain: the project as saved, and the cartridge of the models made from it (bm_forge, the real step)
    lib3d.chain_put(BME, "01-studio.bme")
    forge = os.path.join(ROOT, ".claude", "skills", "bm-forge", "scripts", "bm_forge.py")
    dst = os.path.join(lib3d.CHAIN, "01-studio.bm")
    q = subprocess.run([sys.executable, forge, "export", "models", BME, "-o", dst, "--title", "tropic"],
                       capture_output=True, text=True, cwd=ROOT)
    print(q.stdout.strip() or q.stderr.strip())
    if q.returncode:
        print("export models failed (exit %d): chain/01-studio.bm missing, the .bme is kept" % q.returncode)
    if lib3d.have_ffmpeg() and not a.no_video:
        from bmvideo import write_ass, encode, run_bmhost, call
        ass = os.path.join(OUT, "overlay.ass")
        write_ass(s, ass, TITLE, "bm · the bare-metal fantasy console", s.t + 30)
        raw = os.path.join(OUT, "raw.rgb")
        lib3d.fresh_sd(SD)
        call(run_bmhost(lib3d.BMHOST, cart, inp, seconds, SD, raw) + ["--tool"])
        call(encode(raw, ass, os.path.join(OUT, "ep01.mp4")))
        os.remove(raw)
    else:
        print("no mp4 (no ffmpeg or --no-video): input.txt, overlay.json, shots/ and the saved project are the output")
    return 0


if __name__ == "__main__":
    sys.exit(main())
