#!/usr/bin/env python3
"""Episode 5: the assistant. A slime drawn by F6 in bm Pixel, then bm Code: coins,
slimes that can be stomped, score and lives. The HUD is the assistant's (F6, an
answer inserted in the code, adapted with Ctrl+H).

  python3 video/05-assistant/record.py [--build-only] [--no-record]

Three recordings joined: bm Pixel (bmhost-ai --tool), bm Code (bmhost-ai --tool,
same SD), and the saved cartridge played by game.txt. The `ai` table needs
build/host/bmhost-ai. verify.py reads the saved file and replays the game with a log.
"""
import os
import shutil
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "video", "lib"))

from bmvideo import (Script, call, encode, write_ass, FPS, write_storyboard, write_script,   # noqa: E402
                     card_cmd, hook_cmd, concat_cmd)

OUT = os.path.join(HERE, "out")
BMHOST = os.path.join(ROOT, "build", "host", "bmhost-bin")
BMHOST_AI = os.path.join(ROOT, "build", "host", "bmhost-ai")
PIXEL = os.path.join(ROOT, "build", "pixel.bm")
CODE = os.path.join(ROOT, "build", "code.bm")
SD = os.path.join(OUT, "sd")
SD_GAME = os.path.join(OUT, "sd-game")
TITLE = "SKYVALE WORLD  ·  Episode 5: the assistant"
SUBTITLE = "bm · the bare-metal fantasy console"

BLOCK_A = '''local lib = require "bmlib"

local kip = { x = 24, y = 200, w = 10, h = 14, vx = 0, vy = 0 }
local face, t = 1, 0
local score, lives, hurt_t = 0, 3, 0
local coins, slimes = {}, {}
'''
BLOCK_B = '''function _init()
lib.tiles({ solid = 1, platform = 2, edge = true })
music(0)
for _, x in ipairs({ 136, 152, 168, 240, 420, 440, 460, 520 }) do
coins[#coins + 1] = { x = x, y = 198, w = 10, h = 10 }
end
for _, x in ipairs({ 290, 560 }) do
slimes[#slimes + 1] = { x = x, y = 200, w = 12, h = 10, vx = -0.5, x0 = x }
end
end
'''
BLOCK_C1 = '''function _update()
local dx = 0
if btn("left") then dx = -1 end
if btn("right") then dx = 1 end
kip.vx = dx * 2
if dx ~= 0 then face = dx; t = t + 1 end
if btnp("a") and kip.ground then
kip.vy = -5.5
sfx(0)
end
if kip.vy < -2 and not btn("a") then kip.vy = -2 end
lib.step(kip)
if hurt_t > 0 then hurt_t = hurt_t - 1 end
'''
BLOCK_C2 = '''for _, c in ipairs(coins) do
if lib.hit(kip, c) then
c.dead = true
score = score + 10
sfx(1)
end
end
lib.sweep(coins)
'''
BLOCK_C3 = '''for _, s in ipairs(slimes) do
lib.step(s)
if s.x < s.x0 - 40 then s.vx = 0.5 elseif s.x > s.x0 + 40 then s.vx = -0.5 end
if lib.hit(kip, s) then
if kip.vy > 0 and kip.y + kip.h - s.y < 8 then
s.dead = true
kip.vy = -4
score = score + 100
sfx(2)
elseif hurt_t == 0 then
lives = lives - 1
hurt_t = 90
sfx(2)
end
end
end
lib.sweep(slimes)
'''
BLOCK_C4 = '''if mflags(kip.x, kip.y, kip.w, kip.h) & 16 ~= 0 or kip.y > 400 then
kip.x, kip.y, kip.vy = 24, 200, 0
lives = lives - 1
sfx(2)
end
if lives <= 0 then lives, score = 3, 0 end
end
'''
BLOCK_D = '''function _draw()
cls(0x70A8F0)
camera(0, -88)
for l = 1, #mlayers() do map(0, 0, 0, 0, 80, 34, l) end
for _, c in ipairs(coins) do spr(14, c.x - 3, c.y - 3, 2, 2) end
for _, s in ipairs(slimes) do spr(16, s.x - 2, s.y - 6, 2, 2, s.vx > 0) end
local f = 12
if kip.ground then f = kip.vx ~= 0 and t // 4 % 6 * 2 or 0 end
spr(f, kip.x - 3, kip.y - 2, 2, 2, face < 0)
'''
# the heart of the HUD snippet is replaced by Kip's face; the label by an English one
REPLACES = [("48, SCREEN_W - 12 * i - 4, 8", "0, SCREEN_W - 20 * i, 8, 2, 2"), ("PUNTI", "SCORE")]
OLD_LINES = 37          # the code of episode 4 (lines to cut)

GAME_SECONDS = 10
GAME_INPUT = """\
30 pad 1 right
""".strip().splitlines()          # tuned below (see game.txt in out/)


def replace_via_menu(s):
    """Ctrl+H is the same byte as Backspace on the serial line: Esc, the menu, Replace."""
    s.key("esc", "the menu"); s.sec(1)
    for _ in range(12):
        s.key("down", hold=4)
    s.key("enter", "Replace (Ctrl+H)"); s.sec(1.5)


def typed_block(s, text, per_key=3):
    for i, line in enumerate(text.strip("\n").split("\n")):
        if line:
            s.type_text(line, "typing code" if i == 0 else "", per_key=per_key)
        s.key("enter", hold=3)


def build():
    s = Script()
    # ------------------------------------------------ the slime, drawn by the assistant
    s.chapter("The assistant draws a sprite")
    s.scene("Dev ▸ bm Pixel", [("Ctrl+O", "open a .bm"), ("PgDn", "next sprite"), ("F6", "the assistant"),
                             ("Enter", "put it down"), ("Ctrl+S", "save")])
    s.say("Episode 5: enemies, coins, and a score. The assistant of the console helps: it knows the API, how-tos and sprite recipes, and runs on the console itself.")
    s.sec(5)
    s.raw("\\x0f", "Ctrl+O", "open a cartridge", 40)
    s.sec(1.5)
    s.key("enter", "choose skyvale.bm"); s.sec(2)
    s.say("First an enemy. In bm Pixel, PgDn goes to the first free sprite, after the frames of Kip and the coin.")
    for _ in range(8):
        s.key("pgdn", "next sprite", hold=4)
    s.sec(1.5)
    s.say("F6 is the assistant: say what you want, a word is enough. A green slime.")
    s.key("f6", "the assistant"); s.sec(2)
    s.type_text("green slime", "what to draw", per_key=5)
    s.sec(1)
    s.key("enter", "draw it", hold=3); s.sec(3)
    s.say("The base of the sprite floats on the canvas, in the colours of the palette: Enter puts it down, and it can be edited like any other.")
    s.key("enter", "put it down"); s.sec(3)
    s.chord("s", "save"); s.sec(3)
    s.t_cut1 = s.t

    # ---------------------------------------------------------------- bm Code
    s.chapter("Coins and slimes in bm Code")
    s.scene("Dev ▸ bm Code", [("Ctrl+O", "open a .bm"), ("Ctrl+K", "cut a line"), ("F6", "the assistant"),
                            ("Ctrl+H", "replace"), ("F5", "try the game")])
    s.say("Now the game, in bm Code. We keep the movement of episode 4 and add a score, lives, a list of coins and a list of slimes.")
    s.sec(3)
    s.raw("\\x0f", "Ctrl+O", "open a cartridge", 40)
    s.sec(1.5)
    s.key("enter", "choose skyvale.bm"); s.sec(2.5)
    for _ in range(OLD_LINES + 4):
        s.raw("\\x0b", "Ctrl+K", "cut a line", 3)
    s.sec(1)
    s.say("The state of the game: score, lives, and a timer for the seconds after a hit. Coins and slimes are lists of tables, the same shape as Kip's body.")
    typed_block(s, BLOCK_A); s.key("enter", hold=3)
    s.say("In init, the coins at their places and two slimes, each with a speed and the point it patrols around.")
    typed_block(s, BLOCK_B); s.key("enter", hold=3)
    s.sec(1)
    s.chapter("Update: coins, stomp and lives")
    s.say("In update, Kip as before, and the hurt timer counting down.")
    typed_block(s, BLOCK_C1)
    s.say("A coin that Kip touches is marked dead and gives ten points, with the coin sound. lib.sweep takes the dead ones out of the list.")
    typed_block(s, BLOCK_C2)
    s.say("The slimes: lib.step gives them gravity too, they turn at the ends of their walk. Coming down on a slime is a stomp: it dies, Kip bounces, a hundred points. Touched any other way, it costs a life, and for one and a half seconds nothing hurts.")
    typed_block(s, BLOCK_C3)
    s.say("The spikes and the pit now cost a life as well, and with no lives left the game starts again.")
    typed_block(s, BLOCK_C4); s.key("enter", hold=3)
    s.sec(1)
    s.chapter("Draw, and the assistant for the HUD")
    s.say("Draw: the map, the coins, the slimes, and Kip, who blinks while he is hurt. Then the score and the lives on the screen: we ask the assistant.")
    typed_block(s, BLOCK_D)
    s.scene("bm Code ▸ F6 the assistant", [("F6", "the assistant"), ("Enter", "insert the code"), ("Up Down", "choose an answer"),
                                        ("Tab", "next mode"), ("Esc", "close")])
    s.key("f6", "the assistant"); s.sec(2)
    s.type_text("show score and lives", "the question", per_key=4)
    s.sec(2.5)
    s.say("It finds the how-to and shows the code. Up and down choose among the answers. Enter inserts the code where the cursor is.")
    s.sec(3)
    s.key("enter", "insert the code"); s.sec(2.5)
    s.say("The code uses score and lives, as we named them, but it draws a heart that is not in our sheet, and the label is in Italian. Replace, in the menu, or Ctrl+H on a keyboard: the heart becomes Kip's face, twice as big.")
    replace_via_menu(s)
    for _ in range(36):
        s.key("bksp", hold=1)
    s.type_text(REPLACES[0][0], "what to find", per_key=3); s.key("enter", "next"); s.sec(1)
    s.type_text(REPLACES[0][1], "with what", per_key=3); s.key("enter", "replace all"); s.sec(2.5)
    replace_via_menu(s)
    for _ in range(36):
        s.key("bksp", hold=1)
    s.type_text(REPLACES[1][0], "what to find", per_key=4); s.key("enter", "next"); s.sec(0.8)
    s.type_text(REPLACES[1][1], "with what", per_key=4); s.key("enter", "replace all"); s.sec(2.5)
    s.say("The end of draw, and the assistant has more: ask it about invincibility after a hit, and the answer is there to read.")
    s.key("enter", hold=3)
    s.type_text("end", "end of draw", per_key=3); s.key("enter", hold=3)
    s.sec(1)
    s.key("f6", "the assistant"); s.sec(1.5)
    s.type_text("invincible after a hit", "the question", per_key=4); s.sec(5)
    s.key("esc", "close"); s.sec(1)

    s.chapter("Save and play")
    s.scene("bm Code ▸ save and play", [("Ctrl+S", "save"), ("F5", "try the game")])
    s.say("Ctrl+S saves the code into the .bm, and F5 runs the game.")
    s.chord("s", "save"); s.sec(3)
    s.t_cut2 = s.t
    s.mark("F5", "try the game")
    s.scene("bm Code ▸ F5: the game", [("← →", "run"), ("A", "jump"), ("F5", "try the game")])
    s.say("Coins to collect, a slime to stomp, the score and the three faces of Kip: his lives. Next time: bm Studio is 3D, so we stay in 2D: a flag, a second layer, and the last level.")
    s.t += GAME_SECONDS * FPS
    s.end()
    return s


INTRO = [("SKYVALE WORLD", 96, "0xffc050", 380),
         ("Episode 5  ·  the assistant", 56, "0xe0e4f0", 520),
         ("Enemies, coins, score and lives", 34, "0x9098b0", 640)]
OUTRO = [("Next: Episode 6  ·  the last level", 56, "0xffc050", 400),
         ("Zones, collision boxes, parallax and a flag", 38, "0xe0e4f0", 520),
         ("github.com/f-accomando/bm", 34, "0x9098b0", 660)]
HOOK = "TODAY: ENEMIES, COINS AND THE ASSISTANT"


def split_input(lines, a, b):
    """The lines of the script with a <= frame < b, shifted to start at 0."""
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


def main():
    os.makedirs(OUT, exist_ok=True)
    s = build()
    total = s.t + 30
    hook_s, intro_s, outro_s = 8, 4, 5
    offset = (hook_s + intro_s) * FPS
    c1, c2 = s.t_cut1, s.t_cut2
    for name, a, b in (("pixel", 0, c1), ("code", c1, c2)):
        with open(os.path.join(OUT, "input-%s.txt" % name), "w") as f:
            f.write("\n".join(split_input(s.lines, a, b)) + "\n")
    s.write_input(os.path.join(OUT, "input.txt"))
    gi = os.path.join(HERE, "game.txt")
    shutil.copy(gi, os.path.join(OUT, "game.txt"))
    write_ass(s, os.path.join(OUT, "overlay.ass"), TITLE, SUBTITLE, total)
    desc = ("Skyvale World is a 2D platformer built only with the tools inside the bm console. "
            "In this episode the assistant draws the slime in bm Pixel and gives the HUD in bm Code, "
            "and we add coins, stompable enemies, a score and lives.\n\nbm: https://github.com/f-accomando/bm")
    write_storyboard(s, os.path.join(HERE, "storyboard.md"), TITLE, offset)
    write_script(s, os.path.join(HERE, "copione.md"), TITLE, SUBTITLE, offset, desc)
    if "--build-only" in sys.argv:
        print("pixel %.0f s, code %.0f s, total %.0f s" % (c1 / FPS, (c2 - c1) / FPS, total / FPS))
        return
    main_mp4 = os.path.join(OUT, "main.mp4")
    if "--no-record" not in sys.argv:
        for d in (SD, SD_GAME):
            shutil.rmtree(d, ignore_errors=True)
            os.makedirs(os.path.join(d, "carts"))
        shutil.copy(os.path.join(HERE, "start.bm"), os.path.join(SD, "carts", "SKYVALE.BM"))
        raws, wavs = [], []
        for name, tool, cart, secs in (("pixel", BMHOST_AI, PIXEL, c1 / FPS), ("code", BMHOST_AI, CODE, (c2 - c1) / FPS + 1)):
            raw, wv = os.path.join(OUT, "raw-%s.rgb" % name), os.path.join(OUT, "%s.wav" % name)
            call([tool, cart, "--tool", "--sd", SD, "--seconds", str(secs), "--input",
                  os.path.join(OUT, "input-%s.txt" % name), "--video", raw, "--wav", wv, "--quiet"], cwd=ROOT)
            raws.append(raw)
            wavs.append(wv)
        shutil.copy(os.path.join(SD, "carts", "SKYVALE.BM"), os.path.join(SD_GAME, "carts", "SKYVALE.BM"))
        raw, wv = os.path.join(OUT, "raw-game.rgb"), os.path.join(OUT, "game.wav")
        call([BMHOST, os.path.join(SD_GAME, "carts", "SKYVALE.BM"), "--sd", SD_GAME, "--seconds", str(GAME_SECONDS),
              "--input", os.path.join(OUT, "game.txt"), "--video", raw, "--wav", wv, "--quiet"])
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
    at = c2 / FPS + 6
    for cmd in (hook_cmd(main_mp4, os.path.join(OUT, "hook.mp4"), at, hook_s, HOOK),
                card_cmd(os.path.join(OUT, "intro.mp4"), intro_s, INTRO, silent=True),
                card_cmd(os.path.join(OUT, "outro.mp4"), outro_s, OUTRO, silent=True)):
        call(cmd)
    call(concat_cmd([os.path.join(OUT, n) for n in ("hook.mp4", "intro.mp4", "main.mp4", "outro.mp4")],
                    os.path.join(OUT, "ep05.mp4"), audio=True))


if __name__ == "__main__":
    main()
