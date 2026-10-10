#!/usr/bin/env python3
"""Episode 2: the bm SDK, 2D side: tiles with flags, the map with layers.
Builds the first level of Skyvale World on the cartridge of episode 1.

  python3 video/02-sdk/record.py [--build-only] [--no-record]

Run 1: the SDK (carts/editor/main.lua) with bmhost --tool, on a clean SD that
holds start.bm (the cartridge episode 1 saved). Run 2: the saved cartridge, as
F5 would run it (the tool leaves for the game, so it is another recording);
its frames follow the first run's. Needs build/host/bmhost-bin and build/editor.bm
(make bmhost build/editor.bm).  verify.py checks the saved file.
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "video", "lib"))
sys.path.insert(0, HERE)

import art                                   # noqa: E402
from bmvideo import (Script, call, encode, write_ass, FPS, write_storyboard, write_script,   # noqa: E402
                     card_cmd, hook_cmd, concat_cmd)

OUT = os.path.join(HERE, "out")
BMHOST = os.path.join(ROOT, "build", "host", "bmhost-bin")
SDK = os.path.join(ROOT, "build", "editor.bm")
SD = os.path.join(OUT, "sd")
SD_GAME = os.path.join(OUT, "sd-game")
TITLE = "SKYVALE WORLD  ·  Episode 2: bm SDK, tiles and the map"
SUBTITLE = "bm · the bare-metal fantasy console"
GAME_SECONDS = 9

# the code of the game, typed in the SDK's code page (map(mx, my, x, y, mw, mh, layer))
CODE = ["function _draw()",
        "cls(0x70A8F0)",
        "for l = 1, #mlayers() do map(0, 0, 0, 88, 80, 34, l) end",
        "spr(time() * 10 // 1 % 6 * 2, 48, 296, 2, 2)",
        "end"]


class Cursor:
    """The pointer of the sprite page (a pixel of a 8x8 tile) or of the map (a cell)."""

    def __init__(self, s):
        self.s, self.x, self.y, self.ci = s, 0, 0, 1

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
        """The n-th colour of the SDK's palette: "." the next one, "," the one before."""
        if n == self.ci:
            return
        d = n - self.ci
        self.ci = n
        for i in range(abs(d)):
            self.s.key("." if d > 0 else ",", ("colour: " + name) if i == 0 and name else "", hold=min(2, self.s.fast))

    def paint(self, cells, what=""):
        todo, cur, first = list(cells), (self.x, self.y), True
        while todo:
            todo.sort(key=lambda c: abs(c[0] - cur[0]) + abs(c[1] - cur[1]))
            cur = todo.pop(0)
            self.dab(cur[0], cur[1], what if first else "")
            first = False


def draw_tile(s, c, rows, base):
    """A tile of 8x8: the fill for its base colour, then the pencil."""
    s.fast = 3
    if base:
        c.colour(art.COLOURS[base][0], "base colour")
        s.key("f", "fill the tile")
    for ch in sorted({ch for r in rows for ch in r if ch not in (".", base)}, key=lambda ch: art.COLOURS[ch][0]):
        cells = [(x, y) for y, r in enumerate(rows) for x, k in enumerate(r) if k == ch]
        c.colour(art.COLOURS[ch][0], "pencil")
        c.paint(cells, "pencil")


def build():
    art.check()
    s = Script()
    c = Cursor(s)
    lv = art.level()

    # -------------------------------------------------------- the project
    s.chapter("The SDK: the hub of a project")
    s.scene("bm SDK ▸ F1 project", [("F1", "the project"), ("F2", "the code"), ("F3", "sprites, then the map"),
                                  ("Ctrl+O", "open a .bm"), ("Esc", "menu")])
    s.say("The SDK is the hub of a .bm project: the project, the code, the 2D sprites and the map, the 3D. In this series we only use its 2D side.")
    s.sec(6)
    s.say("Ctrl+O opens a cartridge from the card: the one we saved in episode 1, with Kip.")
    s.raw("\\x0f", "Ctrl+O", "open a cartridge", 40)
    s.sec(1.5)
    s.key("enter", "choose skyvale.bm"); s.sec(2)
    s.key("f1", "dev kit")
    s.scene("bm SDK ▸ F1 again: the dev kit", [("F1", "project / dev kit"), ("F5", "try the game")])
    s.say("F1 again is the dev kit: the size of the file, the memory the data takes, the tokens of the code, and the numbers of the last try.")
    s.sec(5)
    s.key("f1", "the project"); s.sec(1.5)

    # ------------------------------------------------------------- tiles
    s.chapter("Tiles: draw them in the sheet")
    s.scene("bm SDK ▸ F3 sprites", [("F3", "sprites (again: the map)"), ("z", "8x8 / 16x16"), ("Tab", "choose on the sheet"),
                                  ("Space", "draw"), ("f", "fill"), (". ,", "next / previous colour")])
    s.say("F3 is the 2D page, first the sprites. A map is made of tiles: little 8 by 8 squares of the sheet. Z switches to 8 by 8.")
    s.key("f3", "2D: sprites"); s.sec(3)
    s.key("z", "8x8"); s.sec(2)
    s.say("Tab chooses the cell on the sheet: two rows down, under Kip's frames, is cell 64.")
    s.key("tab", "the sheet"); s.sec(1)
    s.fast = 8
    s.key("down"); s.key("down")
    s.key("tab", "back to the canvas"); s.sec(1.5)
    first = True
    for cell, (name, flag, rows) in art.TILES.items():
        if cell != 64:
            s.key("tab", "the sheet"); s.key("right"); s.key("tab", "back to the canvas")
        base = "B" if name in ("grass", "dirt") else None
        if cell == 64:
            s.say("The grass: fill the whole tile with brown, then the pencil for the green on top. Dot and comma walk through the palette.")
        elif cell == 65:
            s.say("The dirt under it: a brown fill and a few darker pixels.")
        elif cell == 66:
            s.say("A wooden plank for the platforms: only the top half of the tile is drawn, the rest stays transparent.")
        elif cell == 67:
            s.say("A cloud puff for the sky: three of them side by side make a cloud.")
        elif cell == 68:
            s.say("And spikes for the bottom of a pit.")
        draw_tile(s, c, rows, base)
        s.sec(0.8)
        if flag is not None:
            s.scene("bm SDK ▸ F3 sprites: flags", [("0", "flag 0: solid"), ("1", "flag 1: platform"), ("2", "flag 2: ladder"),
                                                 ("3", "flag 3: water"), ("4", "flag 4: hurts")])
            if cell == 64:
                s.say("The digit keys set the flags of the tile: what it is for the game. Flag 0 means solid: Kip will stand on it.")
            if cell == 66:
                s.say("Flag 1 is a platform: solid from above only, Kip jumps through it from below.")
            if cell == 68:
                s.say("Flag 4 hurts. bmlib, the game library, reads these flags.")
            s.key(str(flag), "flag %d" % flag); s.sec(2)
            s.scene("bm SDK ▸ F3 sprites", [("F3", "sprites (again: the map)"), ("Tab", "choose on the sheet"),
                                          ("Space", "draw"), ("f", "fill"), (". ,", "next / previous colour")])
        s.sec(0.6)
    s.shot("tiles")

    # --------------------------------------------------------------- map
    s.chapter("The map")
    s.scene("bm SDK ▸ F3 again: the map", [("Tab", "choose the tile"), ("Arrows", "move"), ("Space", "place the tile"),
                                         ("u", "undo"), ("l  L", "next / new layer"), ("c", "show the flags")])
    s.say("F3 again is the map. Tab chooses the tile from the sheet: arrows, then Enter. We start with the grass.")
    s.key("f3", "the map"); s.sec(3)
    s.key("tab", "choose the tile"); s.sec(1)
    s.fast = 8
    s.key("down"); s.key("down"); s.key("left")
    s.key("enter", "this tile"); s.sec(1.5)
    m = Cursor(s)
    s.say("Arrows move the cursor, Space places the tile. The ground first: a row of grass, with a gap for the pit.")
    s.fast = 1
    for x in range(art.WIDTH):
        if x in art.PIT:
            if m.y != art.GROUND_Y:
                m.goto(m.x, art.GROUND_Y)
            m.goto(x, art.GROUND_Y)
            continue
        if m.y != art.GROUND_Y:
            m.goto(m.x, art.GROUND_Y)
        m.dab(x, art.GROUND_Y, "place the grass" if x == 0 else "")
    s.fast = 8
    s.sec(1)
    s.shot("grass")

    def pick(delta, what):
        s.key("tab", "choose the tile")
        s.fast = 8
        for _ in range(abs(delta)):
            s.key("right" if delta > 0 else "left")
        s.key("enter", what)
        s.fast = 1

    s.say("The dirt: Tab, one tile to the right, Enter, and two more rows, painted back and forth.")
    pick(1, "dirt")
    for y in (art.GROUND_Y + 1, art.GROUND_Y + 2):
        xs = [x for x in range(art.WIDTH) if (y, x) != (art.GROUND_Y + 1, x) or x not in art.PIT]
        order = list(range(art.WIDTH))
        if (y - art.GROUND_Y) % 2 == 1:
            order.reverse()
        for x in order:
            if y == art.GROUND_Y + 1 and x in art.PIT:
                m.goto(x, y)
                continue
            m.dab(x, y)
    s.fast = 8
    s.sec(1)
    s.say("The spikes at the bottom of the pit.")
    pick(3, "spikes")
    for x in art.PIT:
        m.dab(x, art.GROUND_Y + 1)
    s.sec(1)
    s.say("Planks for the platforms, three of them at different heights.")
    pick(-2, "plank")
    for xs, y in art.PLANKS:
        for x in xs:
            m.dab(x, y)
    s.sec(1.5)
    s.shot("ground")

    # ------------------------------------------------------------ undo
    s.say("A tile in the wrong place? U undoes the last strokes.")
    s.fast = 3
    m.dab(60, 12, "a stray plank")
    s.sec(1)
    s.key("u", "undo"); s.sec(1.5)

    # ---------------------------------------------------------- layers
    s.chapter("Layers and flags")
    s.scene("bm SDK ▸ the map: layers", [("L", "new layer"), ("l", "next layer"), ("o", "this layer only"),
                                       ("c", "show the flags")])
    s.say("A map can have up to eight layers, drawn one over the other. Shift+L adds one: the clouds go on their own layer, behind the ground.")
    s.key("L", "a new layer"); s.sec(2)
    pick(1, "cloud")
    s.fast = 2
    for xs, y in art.CLOUDS:
        for x in xs:
            m.dab(x, y)
    s.sec(1.5)
    s.say("L moves between layers, O shows only the one you are editing.")
    s.key("o", "this layer only"); s.sec(3)
    s.key("o", "every layer"); s.sec(1)
    s.key("l", "next layer"); s.sec(1.5)
    s.say("C shows the flags over the map: a coloured frame on every tile with a flag, red for solid, green for the platforms, light blue for water.")
    s.key("c", "show the flags"); s.sec(5)
    s.key("c", "flags off"); s.sec(1)
    s.shot("layers")

    # ------------------------------------------------------------ save, code
    s.chapter("Save and try the game")
    s.scene("bm SDK ▸ save and code", [("Ctrl+S", "save"), ("F2", "the code"), ("F5", "try the game")])
    s.say("Ctrl+S saves everything in the .bm: sheet, flags, map and layers.")
    s.chord("s", "save"); s.sec(2)
    s.say("To see the level we need a little code. F2 is the code page: Ctrl+K cuts a line, and a game that draws the map is five lines.")
    s.key("f2", "the code"); s.sec(2)
    for _ in range(13):
        s.raw("\\x0b", "Ctrl+K", "cut a line", 3)
    s.sec(1)
    s.say("map draws a part of the map: from cell 0,0 at the bottom of the screen, 80 by 34 cells, layer by layer. spr draws Kip, one frame of the run every tenth of a second.")
    for i, line in enumerate(CODE):
        s.type_text(line, "typing code" if i == 0 else "", per_key=2)
        s.key("enter", hold=3)
    s.sec(2)
    s.say("Ctrl+S, then F5 runs the cartridge.")
    s.chord("s", "save"); s.sec(2)
    s.shot("code")
    s.mark("F5", "try the game")
    s.scene("bm SDK ▸ F5: the game", [("F5", "try the game"), ("Esc", "back to the SDK")])
    s.say("Skyvale World, first level: the ground, the pit with spikes, three platforms, clouds behind, and Kip running on the spot. Next time: sounds.")
    s.t_editor_end = s.t
    s.t += GAME_SECONDS * FPS
    s.end()
    return s


INTRO = [("SKYVALE WORLD", 96, "0xffc050", 380),
         ("Episode 2  ·  bm SDK", 56, "0xe0e4f0", 520),
         ("Tiles, flags and the first level", 34, "0x9098b0", 640)]
OUTRO = [("Next: Episode 3  ·  Sound", 56, "0xffc050", 400),
         ("Effects and music for Skyvale World", 38, "0xe0e4f0", 520),
         ("github.com/f-accomando/bm", 34, "0x9098b0", 660)]
HOOK = "TODAY: TILES, FLAGS AND THE FIRST LEVEL"


def main():
    os.makedirs(OUT, exist_ok=True)
    s = build()
    total = s.t + 30
    hook_s, intro_s, outro_s = 8, 4, 5
    offset = (hook_s + intro_s) * FPS
    s.write_input(os.path.join(OUT, "input.txt"))
    write_ass(s, os.path.join(OUT, "overlay.ass"), TITLE, SUBTITLE, total)
    desc = ("Skyvale World is a 2D platformer built only with the tools inside the bm console. "
            "In this episode we use the bm SDK to draw the tiles, set their flags, build the first level in the map "
            "with layers, and try it.\n\nbm: https://github.com/f-accomando/bm")
    write_storyboard(s, os.path.join(HERE, "storyboard.md"), TITLE, offset)
    write_script(s, os.path.join(HERE, "copione.md"), TITLE, SUBTITLE, offset, desc)
    if "--build-only" in sys.argv:
        print("editor part: %.0f s, total %.0f s" % (s.t_editor_end / FPS, total / FPS))
        return
    main_mp4 = os.path.join(OUT, "main.mp4")
    if "--no-record" not in sys.argv:
        for d in (SD, SD_GAME):
            shutil.rmtree(d, ignore_errors=True)
            os.makedirs(os.path.join(d, "carts"))
        shutil.copy(os.path.join(HERE, "start.bm"), os.path.join(SD, "carts", "SKYVALE.BM"))
        raw1, raw2 = os.path.join(OUT, "raw1.rgb"), os.path.join(OUT, "raw2.rgb")
        call([BMHOST, SDK, "--tool", "--sd", SD, "--seconds", str(s.t_editor_end / FPS), "--input",
              os.path.join(OUT, "input.txt"), "--video", raw1, "--quiet"])
        shutil.copy(os.path.join(SD, "carts", "SKYVALE.BM"), os.path.join(SD_GAME, "carts", "SKYVALE.BM"))
        call([BMHOST, os.path.join(SD_GAME, "carts", "SKYVALE.BM"), "--sd", SD_GAME, "--seconds", str(GAME_SECONDS),
              "--video", raw2, "--quiet"])
        raw = os.path.join(OUT, "raw.rgb")
        with open(raw, "wb") as out:
            for r in (raw1, raw2):
                with open(r, "rb") as f:
                    shutil.copyfileobj(f, out, 1 << 24)
        os.remove(raw1)
        os.remove(raw2)
        call(encode(raw, os.path.join(OUT, "overlay.ass"), main_mp4))
        os.remove(raw)
    # the result up front: the game of the last seconds
    at = s.t_editor_end / FPS + 1
    for cmd in (hook_cmd(main_mp4, os.path.join(OUT, "hook.mp4"), at, hook_s, HOOK),
                card_cmd(os.path.join(OUT, "intro.mp4"), intro_s, INTRO),
                card_cmd(os.path.join(OUT, "outro.mp4"), outro_s, OUTRO)):
        call(cmd)
    call(concat_cmd([os.path.join(OUT, n) for n in ("hook.mp4", "intro.mp4", "main.mp4", "outro.mp4")],
                    os.path.join(OUT, "ep02.mp4")))


if __name__ == "__main__":
    main()
