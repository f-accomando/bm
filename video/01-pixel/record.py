#!/usr/bin/env python3
"""Episode 1: bm Pixel. Draws Kip, the hero of Skyvale World, with the
console's own editor (carts/pixel/main.lua) run by bmhost.

  python3 video/01-pixel/record.py [--build-only] [--stage N]

Writes video/01-pixel/out/: input.txt (the keys), overlay.ass, the shots and
the mp4. Everything is deterministic: the same script gives the same video.
Needs build/host/bmhost-bin and build/pixel.bm (make bmhost build/pixel.bm).
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "video", "lib"))
sys.path.insert(0, HERE)

import art                                   # noqa: E402
from bmvideo import (Script, call, encode, write_ass, run_bmhost, FPS, write_storyboard, write_script,
                     card_cmd, hook_cmd, concat_cmd)   # noqa: E402

OUT = os.path.join(HERE, "out")
BMHOST = os.path.join(ROOT, "build", "host", "bmhost-bin")
PIXEL = os.path.join(ROOT, "build", "pixel.bm")
SD = os.path.join(OUT, "sd")

TITLE = "SKYVALE WORLD  ·  Episode 1: bm Pixel"
SUBTITLE = "bm · the bare-metal fantasy console"


class Painter:
    """The pointer of bm Pixel: arrows move it one pixel, Space uses the tool."""

    def __init__(self, s):
        self.s = s
        self.x = self.y = 0
        self.mirror = False

    def goto(self, x, y):
        s = self.s
        while self.x < x:
            s.key("right"); self.x += 1
        while self.x > x:
            s.key("left"); self.x -= 1
        while self.y < y:
            s.key("down"); self.y += 1
        while self.y > y:
            s.key("up"); self.y -= 1

    def dab(self, x, y, what=""):
        self.goto(x, y)
        self.s.key("space", what)

    def colour(self, n, name=""):
        """Colour number n (bm Pixel: 1-9, 0 the first ten; "." the next one)."""
        s = self.s
        if n <= 9:
            s.key(str(n), "colour %d %s" % (n, name))
        else:
            s.key("0", "colour 10 (the tenth)")
            for _ in range(n - 10):
                s.key(".", "next colour", hold=2)

    def path(self, cells):
        """The cells in an order that keeps the pointer's walk short."""
        todo, out, cur = list(cells), [], (self.x, self.y)
        while todo:
            todo.sort(key=lambda c: abs(c[0] - cur[0]) + abs(c[1] - cur[1]))
            cur = todo.pop(0)
            out.append(cur)
        return out

    def paint(self, cells, what=""):
        first = True
        for (x, y) in self.path(cells):
            self.dab(x, y, what if first else "")
            first = False


def cells_of(rows, ch, half=True):
    return [(x, y) for y, r in enumerate(rows) for x, c in enumerate(r)
            if c == ch and (x < 8 or not half)]


def build():
    art.check()
    s = Script()
    p = Painter(s)
    rows = art.KIP

    # -------------------------------------------------------------- start
    s.chapter("What is bm Pixel")
    s.scene("Dev ▸ bm Pixel", [("Esc", "menu"), ("F1 F2 F3", "the three pages")])
    s.say("bm Pixel is the pixel-art editor inside the console. Open it from the Dev tab; it works on the sprite sheet of a .bm.")
    s.sec(5)
    s.shot("start")

    s.scene("bm Pixel ▸ menu", [("Enter", "choose"), ("Esc", "back"), ("Ctrl+O", "open a .bm"), ("Ctrl+N", "new sheet"),
                              ("Ctrl+S", "save"), ("F5", "try the game")])
    s.say("The menu: open a cartridge, start a new sheet, save, or try the game with F5.")
    s.sec(4)

    s.chapter("The three pages: draw, sheet, palette")
    s.key("f1", "draw page")
    s.scene("bm Pixel ▸ F1 draw", [("F1", "draw"), ("F2", "sheet"), ("F3", "palette")])
    s.say("Three pages: F1 to draw, F2 for the whole sheet, F3 for the palette.")
    s.sec(2)
    s.key("f2", "sheet page")
    s.scene("bm Pixel ▸ F2 sheet", [("F1", "draw"), ("F2", "sheet"), ("F3", "palette")]); s.sec(2.5)
    s.key("f3", "palette page")
    s.scene("bm Pixel ▸ F3 palette", [("F1", "draw"), ("F2", "sheet"), ("F3", "palette")]); s.sec(2.5)
    s.key("f1", "draw page")
    s.scene("bm Pixel ▸ F1 draw", [("F1", "draw"), ("F2", "sheet"), ("F3", "palette")]); s.sec(1)
    s.shot("pages")

    # --------------------------------------------------- sizes and the keys
    s.chapter("Sprite sizes and the command list")
    s.scene("bm Pixel ▸ F1 draw", [("z", "sprite size"), ("Arrows", "move the pointer"), ("Space", "use the tool"),
                                 ("Tab", "list of commands")])
    s.say("Z changes the size of the sprite: 8, 16, 32, 64 up to 128 pixels. We will draw at 16 by 16.")
    for i in range(5):
        s.key("z", "sprite size"); s.sec(1.4)
    s.key("tab", "list of commands"); s.say("Tab opens the list of commands of the page. Hold F12 to see the keys at any time."); s.sec(5)
    s.key("esc", "close the list"); s.sec(1)
    s.shot("ready")

    # ------------------------------------------------------------ the mirror
    s.chapter("Pencil, colours and the mirror")
    s.scene("F1 draw ▸ pencil + mirror", [("y", "mirror left/right"), ("b", "pencil"), ("Space", "paint"),
                                       ("1-9, 0", "first ten colours"), (". ,", "next / previous colour")])
    s.say("Kip is symmetric, so we turn on the mirror with Y: everything on the left appears on the right too.")
    s.key("y", "mirror on"); p.mirror = True
    s.sec(2)
    s.key("b", "pencil")
    s.say("Colour 17 is the dark outline. 0 is the tenth colour, and the dot key walks to the next ones.")
    p.colour(17, "outline")
    s.sec(1)
    s.say("Arrows move the pointer, Space paints. Only the left half: the mirror does the rest.")
    p.paint(cells_of(rows, "K"), "pencil")
    s.sec(2)
    s.shot("outline")

    # ------------------------------------------------------------------ fill
    s.chapter("Fill")
    s.scene("F1 draw ▸ fill", [("g", "fill"), ("Space", "fill the area"), ("0 + .", "choose colour")])
    s.say("G is the fill: choose the orange, move inside the outline, Space. One press fills the whole body.")
    s.key("g", "fill")
    p.colour(10, "fur")
    p.dab(4, 4, "fill")
    s.sec(2)
    s.shot("filled")

    # ---------------------------------------------------------- the details
    s.chapter("Details and undo")
    s.scene("F1 draw ▸ details", [("b", "pencil"), ("8", "white"), ("0 + . . .", "scarf blue"), ("Ctrl+Z", "undo"), ("Ctrl+Y", "redo")])
    s.say("Back to the pencil for the details: the white muzzle, the pink ears and the blue scarf.")
    s.key("b", "pencil")
    p.colour(8, "muzzle")
    p.paint(cells_of(rows, "W"), "pencil")
    p.colour(15, "ear")
    p.paint(cells_of(rows, "P"), "pencil")
    p.colour(13, "scarf")
    p.paint(cells_of(rows, "S"), "pencil")
    s.sec(1)
    s.shot("details")

    # ------------------------------------------------------- undo and redo
    s.say("A mistake? Ctrl+Z undoes, Ctrl+Y redoes. Even a whole fill.")
    p.dab(7, 1, "a mistake")
    s.sec(1)
    s.chord("z", "undo"); s.sec(1.2)
    s.chord("y", "redo"); s.sec(1.2)
    s.chord("z", "undo"); s.sec(1.5)
    s.shot("undo")

    # ------------------------------------------------------------ walk frames
    s.chapter("Animation frames: copy, paste, onion skin")
    s.scene("F1 draw ▸ animation frames", [("Ctrl+C", "copy the sprite"), ("PgDn", "next sprite"), ("Ctrl+V", "paste"),
                                         ("Enter", "put it down"), ("e", "eraser"), ("k", "onion skin")])
    s.say("The sprites that follow in the sheet are the frames of an animation. Copy Kip with Ctrl+C, go to the next sprite with PgDn, paste with Ctrl+V.")
    s.shot("beforecopy")
    s.chord("c", "copy the sprite"); s.sec(1)
    legs = [art.LEGS_A, None, art.LEGS_B]
    for n, lg in enumerate(legs):
        s.key("pgdn", "next sprite"); s.sec(0.8)
        p.goto(0, 0)                        # the pasted block lands where the pointer is
        s.chord("v", "paste"); s.sec(0.8)
        s.shot("pasted%d" % n)
        s.key("enter", "put it down"); s.sec(0.8)
        s.shot("placed%d" % n)
        p.x = p.y = 0
        if n == 0:
            s.say("Only the legs change: the eraser on the old feet, the pencil for the new ones. The mirror is still on.")
        if lg:
            new_rows = art.frame(lg)
            gone = [(x, y) for y in (14, 15) for x in range(8)
                    if rows[y][x] != "." and new_rows[y][x] == "."]
            s.key("e", "eraser")
            p.paint(gone, "erase")
            for ch in "KO":
                cells = [(x, y) for y in (14, 15) for x in range(8)
                         if new_rows[y][x] == ch and rows[y][x] != ch]
                if cells:
                    s.key("b", "pencil")
                    p.colour(art.COLOURS[ch][0], art.COLOURS[ch][1])
                    p.paint(cells, "pencil")
        s.sec(1)
    s.shot("frames")

    s.say("The panel on the right plays the frames: plus and minus change how many, the arrows of the speed keys make it faster or slower, P pauses.")
    s.scene("F1 draw ▸ animation", [("+  -", "number of frames"), ("<  >", "speed"), ("p", "pause / play"), ("k", "onion skin")])
    s.key("<", "slower"); s.sec(1.5)
    s.key(">", "faster"); s.key(">", "faster"); s.sec(3)
    s.key("k", "onion skin"); s.sec(3)
    s.key("k", "onion skin off"); s.sec(1)
    s.shot("anim")

    # ---------------------------------------------------------------- shapes
    s.chapter("Shapes: oval, line, rectangle")
    s.scene("F1 draw ▸ shapes", [("O  o", "filled / empty oval"), ("U  u", "filled / empty rectangle"), ("l", "line"),
                               ("Space", "fix a corner, then draw"), ("y", "mirror on / off")])
    s.say("Next sprite, next object: a coin. PgDn moves on, and Y turns the mirror off, because a coin highlight is not symmetric.")
    s.key("pgdn", "next sprite"); s.sec(1)
    s.key("y", "mirror off"); s.sec(1.5)
    s.say("O draws a filled oval: Space fixes one corner, the arrows go to the opposite corner, Space again.")
    p.colour(17, "outline")
    s.key("O", "filled oval")
    p.dab(2, 1, "first corner")
    p.dab(13, 14, "second corner: the oval")
    s.sec(1.5)
    s.say("The same oval one pixel smaller, in yellow, leaves the dark rim.")
    p.colour(11, "yellow")
    s.key("O", "filled oval")
    p.dab(3, 2, "first corner")
    p.dab(12, 13, "second corner")
    s.sec(1.5)
    s.say("L draws a straight line between two points: a white shine.")
    p.colour(8, "white")
    s.key("l", "line")
    p.dab(5, 4, "start")
    p.dab(5, 7, "end")
    s.sec(1)
    s.say("U is a filled rectangle: the slot in the middle of the coin.")
    p.colour(10, "orange")
    s.key("U", "filled rectangle")
    p.dab(7, 5, "first corner")
    p.dab(8, 10, "second corner")
    s.sec(1.5)
    s.say("Four tools, one coin: and the mirror is a single key away when you need it again.")
    s.sec(3)
    s.shot("coin")

    # ----------------------------------------------------------- sheet, palette
    s.chapter("The sheet and the palette")
    s.scene("bm Pixel ▸ F2 sheet", [("F2", "the whole sheet"), ("Arrows", "choose a sprite"), ("Enter", "draw it"),
                                   ("+  -", "zoom"), ("R", "sheet size")])
    s.say("F2 shows the whole sheet: our four frames are side by side. Arrows choose a sprite, Enter draws it.")
    s.key("f2", "the sheet"); s.sec(3)
    s.key("right", "next sprite"); s.sec(1)
    s.key("right", "next sprite"); s.sec(1)
    s.key("left", "back"); s.key("left", "back"); s.sec(1)
    s.key("f3", "the palette")
    s.scene("bm Pixel ▸ F3 palette", [("F3", "the palette"), ("Enter", "draw with it"), ("e", "edit the colour"),
                                     ("a", "add a colour"), ("f", "colours of the sheet"), ("s", "sort by hue")])
    s.say("F3 is the palette: up to 256 colours. The same palette is read by bm Studio and the games.")
    s.sec(2)
    s.key("f", "colours of the sheet"); s.sec(3)
    s.key("f1", "back to drawing"); s.sec(1)

    # ------------------------------------------------------------------ save
    s.chapter("Save as a .bm and try it")
    s.scene("bm Pixel ▸ Save as", [("Esc", "menu"), ("Ctrl+Shift+S", "save as"), ("Ctrl+S", "save"), ("F5", "try the game")])
    s.say("Save as with Ctrl+Shift+S: the sheet becomes a cartridge, skyvale.bm, with a code that shows it.")
    s.raw("\\xee\\x13", "Ctrl+Shift+S", "save as", 10)
    s.sec(2)
    s.type_text("skyvale", "the file name")
    s.sec(1)
    s.key("enter", "save"); s.sec(3)
    s.shot("saved")
    s.scene("bm Pixel ▸ try the game", [("F5", "try the game"), ("Esc / Select+Start", "back")])
    s.say("F5 runs the cartridge: here, its sprite sheet. Next time: bm Studio, tiles and the first level.")
    s.key("f5", "try the game"); s.sec(5)
    s.shot("try")
    s.sec(2)
    s.end()
    return s


INTRO = [("SKYVALE WORLD", 96, "0xffc050", 380),
         ("Episode 1  ·  bm Pixel", 56, "0xe0e4f0", 520),
         ("A 2D platformer made only with the tools of the bm console", 34, "0x9098b0", 640)]
OUTRO = [("Next: Episode 2  ·  bm Studio", 56, "0xffc050", 400),
         ("Tiles, flags and the first level of Skyvale World", 38, "0xe0e4f0", 520),
         ("github.com/f-accomando/bm", 34, "0x9098b0", 660)]
HOOK_AT = "anim"            # the scene whose end is the result shown up front
HOOK = "TODAY: DRAW A HERO WITH bm PIXEL"


def main():
    os.makedirs(OUT, exist_ok=True)
    s = build()
    total = s.t + 30
    hook_s, intro_s, outro_s = 8, 4, 5
    offset = (hook_s + intro_s) * FPS
    s.write_input(os.path.join(OUT, "input.txt"))
    write_ass(s, os.path.join(OUT, "overlay.ass"), TITLE, SUBTITLE, total)
    desc = ("Skyvale World is a 2D platformer built only with the tools inside the bm console. "
            "In this episode we draw its hero, Kip, with bm Pixel: pencil, fill, mirror, animation frames, "
            "palette and saving a .bm.\n\nbm: https://github.com/f-accomando/bm")
    write_storyboard(s, os.path.join(HERE, "storyboard.md"), TITLE, offset)
    write_script(s, os.path.join(HERE, "copione.md"), TITLE, SUBTITLE, offset, desc)
    if "--build-only" in sys.argv:
        return
    shutil.rmtree(SD, ignore_errors=True)       # bm Pixel remembers its page on the SD: always a clean one
    os.makedirs(SD, exist_ok=True)
    shots = os.path.join(OUT, "shots")
    os.makedirs(shots, exist_ok=True)
    raw = os.path.join(OUT, "raw.rgb")
    main_mp4 = os.path.join(OUT, "main.mp4")
    if "--no-record" not in sys.argv:
        call(run_bmhost(BMHOST, PIXEL, os.path.join(OUT, "input.txt"), total / FPS, SD, raw, shots))
        call(encode(raw, os.path.join(OUT, "overlay.ass"), main_mp4))
        os.remove(raw)
    # the result up front: the last seconds of the animation scene
    at = next(f for f, pl, _ in s.scenes if pl == "F1 draw ▸ animation") / FPS + 1
    parts = []
    for name, cmd in (("hook", hook_cmd(main_mp4, os.path.join(OUT, "hook.mp4"), at, hook_s, HOOK)),
                      ("intro", card_cmd(os.path.join(OUT, "intro.mp4"), intro_s, INTRO)),
                      ("outro", card_cmd(os.path.join(OUT, "outro.mp4"), outro_s, OUTRO))):
        call(cmd)
    call(concat_cmd([os.path.join(OUT, n) for n in ("hook.mp4", "intro.mp4", "main.mp4", "outro.mp4")],
                    os.path.join(OUT, "ep01.mp4")))


if __name__ == "__main__":
    main()
