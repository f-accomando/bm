#!/usr/bin/env python3
"""Episode 6: the last level of Skyvale World. Three tools one after the other on the
same SD, then the game: bm Pixel (the assistant's tree, the flag and its pole), the SDK
(the level carried on to 160 cells, three pits, a layer of trees) and bm Code (a camera
that follows Kip, three layers at three speeds, the flag, a title and a clear screen).

  python3 video/06-level/record.py [--build-only] [--no-record] [--parts]

--parts runs the three editors only (no video) and verifies the cartridge.
Needs build/host/bmhost-bin and bmhost-ai, build/pixel.bm, editor.bm, code.bm.
"""
import importlib.util
import os
import shutil
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "video", "lib"))

from bmvideo import (Script, call, encode, write_ass, FPS, write_storyboard, write_script,   # noqa: E402
                     card_cmd, hook_cmd, concat_cmd)


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


art = _load("art6", os.path.join(HERE, "art.py"))
ep2art = art.ep2

OUT = os.path.join(HERE, "out")
BMHOST = os.path.join(ROOT, "build", "host", "bmhost-bin")
BMHOST_AI = os.path.join(ROOT, "build", "host", "bmhost-ai")
TOOLS = {"pixel": os.path.join(ROOT, "build", "pixel.bm"), "sdk": os.path.join(ROOT, "build", "editor.bm"),
         "code": os.path.join(ROOT, "build", "code.bm")}
SD = os.path.join(OUT, "sd")
SD_GAME = os.path.join(OUT, "sd-game")
TITLE = "SKYVALE WORLD  ·  Episode 6: the last level"
SUBTITLE = "bm · the bare-metal fantasy console"
GAME_SECONDS = 14

COINS = "136, 152, 168, 240, 420, 440, 460, 520, 600, 616, 632, 700, 716, 876, 892, 908, 960, 1040, 1056, 1120"
SLIMES = "200, 560, 680, 960"
OLD_COINS = "136, 152, 168, 240, 420, 440, 460, 520"
OLD_SLIMES = "290, 560"

# the new parts of the code (no indentation: bm Code indents by itself)
LOCALS = 'local cam, state, flag_x = 0, "title", 1200'
UPDATE_TOP = '''if state == "title" then
if btnp("a") then state = "play" end
return
end
if state == "clear" then return end'''
UPDATE_STEP = '''cam = math.max(0, math.min(kip.x - 240, 640))
if kip.x > flag_x then state = "clear"; sfx(1) end'''
DRAW = '''local function layer(l, s)
local ox = math.floor(cam * s)
camera(ox, -88)
map(ox // 8, 0, ox // 8 * 8, 0, 82, 34, l)
end

function _draw()
cls(0x70A8F0)
layer(2, 0.25)
layer(3, 0.5)
layer(1, 1)
camera(cam, -88)
for _, c in ipairs(coins) do spr(14, c.x - 3, c.y - 3, 2, 2) end
for _, s in ipairs(slimes) do spr(16, s.x - 2, s.y - 6, 2, 2, s.vx > 0) end
spr(10, flag_x - 3, 144, 2, 2)
for y = 160, 208, 16 do spr(11, flag_x - 3, y, 2, 2) end
local f = 12
if kip.ground then f = kip.vx ~= 0 and t // 4 % 6 * 2 or 0 end
if hurt_t == 0 or hurt_t % 4 < 2 then spr(f, kip.x - 3, kip.y - 2, 2, 2, face < 0) end
camera()
print("SCORE " .. score, 8, 8, 0xFFFFFF)
for i = 1, lives do
spr(0, SCREEN_W - 20 * i, 8, 2, 2)
end
if state == "title" then
print("SKYVALE WORLD", 164, 120, 0xFFFFFF, 3)
print("PRESS A", 264, 200, 0xFFEC27, 2)
elseif state == "clear" then
print("COURSE CLEAR", 176, 130, 0xFFEC27, 3)
print("SCORE " .. score, 240, 200, 0xFFFFFF, 2)
end
end'''


def start_code():
    sys.path.insert(0, os.path.join(ROOT, "scripts"))
    import bmres
    return bmres.read(os.path.join(HERE, "start.bm")).get(bmres.SEC_LUA).decode()


def line_of(code, needle, nth=1):
    n = 0
    for i, l in enumerate(code.split("\n"), 1):
        if needle in l:
            n += 1
            if n == nth:
                return i
    raise ValueError(needle)


class Pointer:
    """The pointer of bm Pixel (a pixel) or of the SDK's map (a cell): arrows move it one step."""

    def __init__(self, s):
        self.s, self.x, self.y = s, 0, 0

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

    def snake(self, cells, what=""):
        """Many cells: row by row, back and forth, the pointer going through the gaps."""
        rows = {}
        for (x, y) in cells:
            rows.setdefault(y, []).append(x)
        flip, first = False, True
        for y in sorted(rows):
            xs = sorted(rows[y], reverse=flip)
            for x in xs:
                self.dab(x, y, what if first else "")
                first = False
            flip = not flip

    def near(self, cells, what=""):
        todo, cur, first = list(cells), (self.x, self.y), True
        while todo:
            todo.sort(key=lambda c: abs(c[0] - cur[0]) + abs(c[1] - cur[1]))
            cur = todo.pop(0)
            self.dab(cur[0], cur[1], what if first else "")
            first = False

    def paint(self, cells, what=""):
        (self.snake if len(cells) > 40 else self.near)(cells, what)


def go_to_line(s, n, what="go to a line"):
    s.raw("\\x0c", "Ctrl+L", what, 4)           # Ctrl+L is 0x0c
    s.sec(0.6)
    s.type_text(str(n), "the line number", per_key=3)
    s.key("enter", "go", hold=4)
    s.key("end", "end of the line", hold=3)


def replace_via_menu(s, old, new, what_old="what to find", what_new="with what"):
    s.key("esc", "the menu"); s.sec(0.8)
    for _ in range(12):
        s.key("down", hold=3)
    s.key("enter", "Replace (Ctrl+H)"); s.sec(1)
    for _ in range(40):
        s.key("bksp", hold=1)
    s.type_text(old, what_old, per_key=2); s.key("enter", "next"); s.sec(0.5)
    s.type_text(new, what_new, per_key=2); s.key("enter", "replace all"); s.sec(1.5)


def typed(s, text, per_key=2):
    lines = text.split("\n")
    for i, line in enumerate(lines):
        if line:
            s.type_text(line, "typing code" if i == 0 else "", per_key=per_key)
        if i < len(lines) - 1:
            s.key("enter", hold=3)


def pick(s, cur, target, what):
    """The tile picker of the SDK's map: Tab, arrows (±1, ±32), Enter."""
    s.key("tab", "choose the tile", hold=4)
    d = target - cur
    dy = int(round(d / 32))
    dx = d - 32 * dy
    for _ in range(abs(dy)):
        s.key("down" if dy > 0 else "up", hold=2)
    for _ in range(abs(dx)):
        s.key("right" if dx > 0 else "left", hold=1)
    s.key("enter", what, hold=4)
    return target


def build():
    s = Script()
    code0 = start_code()
    lv, lv2 = art.level(), ep2art.level()

    # ------------------------------------------------------------------ bm Pixel
    s.chapter("The flag and the trees: bm Pixel")
    s.scene("Dev ▸ bm Pixel", [("Ctrl+O", "open a .bm"), ("PgDn", "next sprite"), ("F6", "the assistant"),
                             ("U", "filled rectangle"), ("Space", "fix a corner / draw"), ("Ctrl+S", "save")])
    s.say("The last level. First the pieces it needs, in bm Pixel: trees for the far background, and a flag to end the level.")
    s.sec(4)
    s.raw("\\x0f", "Ctrl+O", "open a cartridge", 40)
    s.sec(1.5)
    s.key("enter", "choose skyvale.bm"); s.sec(2)
    for _ in range(9):
        s.key("pgdn", "next sprite", hold=3)
    s.sec(1)
    s.say("The assistant draws the tree: F6, one word.")
    s.key("f6", "the assistant"); s.sec(1.5)
    s.type_text("tree", "what to draw", per_key=5); s.sec(0.8)
    s.key("enter", "draw it", hold=3); s.sec(2.5)
    s.key("enter", "put it down"); s.sec(2)
    px = Pointer(s)
    s.say("The flag by hand, with the filled rectangle: next sprite, a grey pole, a red cloth.")
    s.key("pgdn", "next sprite"); s.sec(1)
    s.fast = 3
    s.key("7", "colour 7: grey"); s.key("U", "filled rectangle")
    px.dab(3, 0, "first corner"); px.dab(4, 15, "second corner")
    s.key("9", "colour 9: red"); s.key("U", "filled rectangle")
    px.dab(5, 1, "first corner"); px.dab(13, 6, "second corner")
    s.sec(1.5)
    s.say("And the next sprite is only the pole, to stack under the flag.")
    s.key("pgdn", "next sprite"); s.sec(0.8)
    s.key("7", "colour 7: grey"); s.key("U", "filled rectangle")
    px.dab(3, 0, "first corner"); px.dab(4, 15, "second corner")
    s.sec(1)
    s.chord("s", "save"); s.sec(2.5)
    s.t_cut1 = s.t

    # ------------------------------------------------------------------ the SDK
    s.chapter("A longer level: the SDK map")
    s.scene("Dev ▸ bm SDK", [("Ctrl+O", "open a .bm"), ("F3", "2D: sprites, again: the map"), ("Tab", "choose the tile"),
                           ("Space", "place the tile"), ("L", "new layer"), ("Ctrl+S", "save")])
    s.say("Now the map, in the SDK. The level of episode 2 was one screen; this one is twice as long, so the ground carries on, with two more pits, planks and clouds.")
    s.sec(3)
    s.raw("\\x0f", "Ctrl+O", "open a cartridge", 40)
    s.sec(1.5)
    s.key("enter", "choose skyvale.bm"); s.sec(2)
    s.key("f3", "2D: sprites"); s.sec(1.5)
    s.key("f3", "the map"); s.sec(1.5)
    cur_tile = 1
    m = Pointer(s)
    s.fast = 1

    def new(layer):
        old = lv2.get(layer, {})
        return {k: v for k, v in lv[layer].items() if old.get(k) != v}

    main_new = new("main")
    by_tile = {}
    for k, v in main_new.items():
        by_tile.setdefault(v, []).append(k)
    cur_tile = pick(s, cur_tile, 64, "grass")
    s.say("Grass first, carried on to the right: the view scrolls with the cursor.")
    m.paint(sorted(by_tile[64]), "place the grass")
    s.sec(0.8)
    cur_tile = pick(s, cur_tile, 65, "dirt")
    s.say("Then the dirt under it, two rows, leaving the gaps of the pits.")
    m.paint(sorted(by_tile[65]), "place the dirt")
    s.sec(0.8)
    cur_tile = pick(s, cur_tile, 68, "spikes")
    s.say("Spikes at the bottom of each new pit.")
    m.paint(sorted(by_tile[68]), "place the spikes")
    s.sec(0.8)
    cur_tile = pick(s, cur_tile, 66, "plank")
    s.say("More planks, higher and lower.")
    m.paint(sorted(by_tile[66]), "place the planks")
    s.sec(1)
    s.key("l", "next layer"); s.sec(1)
    cur_tile = pick(s, cur_tile, 67, "cloud")
    s.say("Layer two holds the clouds: more of them along the way.")
    m.paint(sorted(new("layer2")), "place the clouds")
    s.sec(1)

    s.chapter("A layer of trees far behind")
    s.say("A third layer, with Shift+L, for the trees. Each tree is four tiles, two by two: so one pass for each of the four tiles.")
    s.key("L", "a new layer"); s.sec(2)
    trees = new("layer3")
    for tile in (18, 19, 50, 51):
        cur_tile = pick(s, cur_tile, tile, "tree tile %d" % tile)
        m.paint(sorted(k for k, v in trees.items() if v == tile), "place the tile")
    s.sec(1)
    s.say("O shows one layer alone: here the trees, behind everything. In the game each layer will move at its own speed.")
    s.key("o", "this layer only"); s.sec(3)
    s.key("o", "every layer"); s.sec(1)
    s.chord("s", "save"); s.sec(2.5)
    s.t_cut2 = s.t

    # ----------------------------------------------------------------- bm Code
    s.chapter("Camera, parallax and the flag: bm Code")
    s.scene("Dev ▸ bm Code", [("Ctrl+O", "open a .bm"), ("Ctrl+L", "go to a line"), ("Ctrl+K", "cut a line"),
                            ("Esc ▸ Replace", "replace"), ("Ctrl+S", "save"), ("F5", "try the game")])
    s.say("Then the code, in bm Code: edits, not a new program. Ctrl+L goes to a line, and we begin from the end of the file.")
    s.sec(3)
    s.raw("\\x0f", "Ctrl+O", "open a cartridge", 40)
    s.sec(1.5)
    s.key("enter", "choose skyvale.bm"); s.sec(2.5)
    draw_line = line_of(code0, "function _draw()")
    n_draw = len(code0.rstrip("\n").split("\n")) - draw_line + 1
    s.say("The drawing changes the most: the whole of draw goes, with Ctrl+K, one line at a time.")
    go_to_line(s, draw_line)
    for _ in range(n_draw):
        s.raw("\\x0b", "Ctrl+K", "cut a line", 3)
    s.sec(0.5)
    s.say("The new draw: a function to draw one layer at a speed, so the clouds go by slowly, the trees at half the speed of Kip, and the ground with him: parallax.")
    typed(s, DRAW)
    s.sec(1)
    s.say("In update, after lib.step: the camera follows Kip, kept inside the level, and reaching the flag ends it.")
    go_to_line(s, line_of(code0, "lib.step(kip)"))
    s.key("enter", hold=3)
    typed(s, UPDATE_STEP)
    s.sec(0.5)
    s.say("At the top of update, the two screens: while on the title nothing moves until A; after the flag, nothing moves.")
    go_to_line(s, line_of(code0, "function _update()"))
    s.key("enter", hold=3)
    typed(s, UPDATE_TOP)
    s.sec(0.5)
    s.say("More coins and more slimes, spread along the whole level: Replace, from the menu.")
    replace_via_menu(s, OLD_SLIMES, SLIMES)
    replace_via_menu(s, OLD_COINS, COINS)
    s.say("And the new state: the camera, the screen we are on, and where the flag is.")
    go_to_line(s, line_of(code0, "local coins, slimes"))
    s.key("enter", hold=3)
    typed(s, LOCALS)
    s.sec(1)
    s.chapter("Save and play")
    s.chord("s", "save"); s.sec(2.5)
    s.t_cut3 = s.t
    s.mark("F5", "try the game")
    s.scene("bm Code ▸ F5: the game", [("A", "start; jump"), ("← →", "run"), ("F5", "try the game")])
    s.say("The title, then the level scrolls with Kip: the clouds slowly, the trees at half speed, the ground at his. The last episode plays it from the start to the flag.")
    s.t += GAME_SECONDS * FPS
    s.end()
    return s


INTRO = [("SKYVALE WORLD", 96, "0xffc050", 380),
         ("Episode 6  ·  the last level", 56, "0xe0e4f0", 520),
         ("Flag, trees, a long map and a moving camera", 34, "0x9098b0", 640)]
OUTRO = [("Next: Episode 7  ·  the game", 56, "0xffc050", 400),
         ("Skyvale World, from the title to the flag", 38, "0xe0e4f0", 520),
         ("github.com/f-accomando/bm", 34, "0x9098b0", 660)]
HOOK = "TODAY: THE LAST LEVEL"


def split_input(lines, a, b):
    out = []
    for ln in lines:
        f, rest = ln.split(" ", 1)
        if a <= int(f) < b:
            out.append("%d %s" % (int(f) - a, rest))
    return out


def join_wavs(paths, out):
    with wave.open(paths[0]) as w0:
        params = w0.getparams()
    with wave.open(out, "wb") as wo:
        wo.setparams(params)
        for p in paths:
            with wave.open(p) as w:
                wo.writeframes(w.readframes(w.getnframes()))


def run_editors(s, video=False):
    """The three editors on a clean SD, one after the other; returns the raw video and wav lists."""
    shutil.rmtree(SD, ignore_errors=True)
    os.makedirs(os.path.join(SD, "carts"))
    shutil.copy(os.path.join(HERE, "start.bm"), os.path.join(SD, "carts", "SKYVALE.BM"))
    raws, wavs = [], []
    cuts = [0, s.t_cut1, s.t_cut2, s.t_cut3]
    for i, name in enumerate(("pixel", "sdk", "code")):
        a, b = cuts[i], cuts[i + 1]
        with open(os.path.join(OUT, "input-%s.txt" % name), "w") as f:
            f.write("\n".join(split_input(s.lines, a, b)) + "\n")
        cmd = [BMHOST_AI, TOOLS[name], "--tool", "--sd", SD, "--seconds", str((b - a) / FPS + (1 if i == 2 else 0)),
               "--input", os.path.join(OUT, "input-%s.txt" % name), "--quiet"]
        if video:
            raw, wv = os.path.join(OUT, "raw-%s.rgb" % name), os.path.join(OUT, "%s.wav" % name)
            cmd += ["--video", raw, "--wav", wv]
            raws.append(raw)
            wavs.append(wv)
        call(cmd, cwd=ROOT)
    return raws, wavs


def main():
    os.makedirs(OUT, exist_ok=True)
    s = build()
    total = s.t + 30
    hook_s, intro_s, outro_s = 8, 4, 5
    offset = (hook_s + intro_s) * FPS
    s.write_input(os.path.join(OUT, "input.txt"))
    write_ass(s, os.path.join(OUT, "overlay.ass"), TITLE, SUBTITLE, total)
    desc = ("Skyvale World is a 2D platformer built only with the tools inside the bm console. "
            "In this episode the last level: a flag and trees in bm Pixel, a map twice as long in the SDK, "
            "and in bm Code a camera that follows Kip, parallax layers, a title screen and the end of the level."
            "\n\nbm: https://github.com/f-accomando/bm")
    write_storyboard(s, os.path.join(HERE, "storyboard.md"), TITLE, offset)
    write_script(s, os.path.join(HERE, "copione.md"), TITLE, SUBTITLE, offset, desc)
    c = (s.t_cut1, s.t_cut2 - s.t_cut1, s.t_cut3 - s.t_cut2)
    if "--build-only" in sys.argv:
        print("pixel %.0f s, sdk %.0f s, code %.0f s, total %.0f s" % (c[0] / FPS, c[1] / FPS, c[2] / FPS, total / FPS))
        return
    if "--parts" in sys.argv:
        run_editors(s)
        return
    main_mp4 = os.path.join(OUT, "main.mp4")
    if "--no-record" not in sys.argv:
        raws, wavs = run_editors(s, video=True)
        shutil.rmtree(SD_GAME, ignore_errors=True)
        os.makedirs(os.path.join(SD_GAME, "carts"))
        shutil.copy(os.path.join(SD, "carts", "SKYVALE.BM"), os.path.join(SD_GAME, "carts", "SKYVALE.BM"))
        raw, wv = os.path.join(OUT, "raw-game.rgb"), os.path.join(OUT, "game.wav")
        call([BMHOST, os.path.join(SD_GAME, "carts", "SKYVALE.BM"), "--sd", SD_GAME, "--seconds", str(GAME_SECONDS),
              "--input", os.path.join(HERE, "game.txt"), "--video", raw, "--wav", wv, "--quiet"])
        raws.append(raw)
        wavs.append(wv)
        allraw = os.path.join(OUT, "raw.rgb")
        with open(allraw, "wb") as out:
            for r in raws:
                with open(r, "rb") as f:
                    shutil.copyfileobj(f, out, 1 << 24)
                os.remove(r)
        wav = os.path.join(OUT, "sound.wav")
        join_wavs(wavs, wav)
        call(encode(allraw, os.path.join(OUT, "overlay.ass"), main_mp4, audio=wav))
        os.remove(allraw)
    at = s.t_cut3 / FPS + 3
    for cmd in (hook_cmd(main_mp4, os.path.join(OUT, "hook.mp4"), at, hook_s, HOOK),
                card_cmd(os.path.join(OUT, "intro.mp4"), intro_s, INTRO, silent=True),
                card_cmd(os.path.join(OUT, "outro.mp4"), outro_s, OUTRO, silent=True)):
        call(cmd)
    call(concat_cmd([os.path.join(OUT, n) for n in ("hook.mp4", "intro.mp4", "main.mp4", "outro.mp4")],
                    os.path.join(OUT, "ep06.mp4"), audio=True))


if __name__ == "__main__":
    main()
