#!/usr/bin/env python3
"""Episode 4: bm Code. Kip runs, jumps and is stopped by the map: lib.tiles and
lib.step of bmlib, variable jump, the sounds of episode 3 and its music.

  python3 video/04-code/record.py [--build-only] [--no-record]

Run 1: bm Code (carts/code/main.lua) with bmhost --tool, on a clean SD that holds
start.bm (the cartridge episode 3 saved); the code is typed. Run 2: the saved
cartridge played by a script of buttons (game.txt), with its sound; F5 leaves
the tool for the game, so it is a second recording whose frames and sound follow
the first. verify.py reads the saved code and the log of the run.
Needs build/host/bmhost-bin, build/host/bmhost-ai and build/code.bm (make bmhost bmhost-ai build/code.bm).
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
BMHOST_AI = os.path.join(ROOT, "build", "host", "bmhost-ai")      # bm Code needs the `ai` table
CODE_TOOL = os.path.join(ROOT, "build", "code.bm")
SD = os.path.join(OUT, "sd")
SD_GAME = os.path.join(OUT, "sd-game")
TITLE = "SKYVALE WORLD  ·  Episode 4: bm Code"
SUBTITLE = "bm · the bare-metal fantasy console"

# the game, in three parts as typed (no indentation: bm Code indents by itself)
PART1 = '''local lib = require "bmlib"

local kip = { x = 24, y = 200, w = 10, h = 14, vx = 0, vy = 0 }
local face, t = 1, 0

function _init()
lib.tiles({ solid = 1, platform = 2, edge = true })
music(0)
end
'''
PART2 = '''
function _update()
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
if mflags(kip.x, kip.y, kip.w, kip.h) & 16 ~= 0 or kip.y > 400 then
kip.x, kip.y, kip.vy = 24, 200, 0
sfx(2)
end
end
'''
PART3 = '''
function _draw()
cls(0x70A8F0)
camera(0, -88)
for l = 1, #mlayers() do map(0, 0, 0, 0, 80, 34, l) end
local f = 12
if kip.ground then f = kip.vx ~= 0 and t // 4 % 6 * 2 or 0 end
spr(f, kip.x - 3, kip.y - 2, 2, 2, face < 0)
end
'''
OLD_LINES = 5          # the five lines of episode 2's code

# the buttons of the game recording (frame, command): see game.txt; one frame = 1/60 s
GAME_SECONDS = 10
GAME_INPUT = """\
40 pad 1 right
98 pad 1 none
106 pad 1 a
142 pad 1 none
150 pad 1 right
222 pad 1 right a
225 pad 1 right
410 pad 1 right a
450 pad 1 right
540 pad 1 none
""".strip().splitlines()


def typed_block(s, text, per_key=3):
    lines = text.strip("\n").split("\n")
    for i, line in enumerate(lines):
        if line:
            s.type_text(line, "typing code" if i == 0 else "", per_key=per_key)
        s.key("enter", hold=3)


def build():
    s = Script()
    s.chapter("What is bm Code")
    s.scene("Dev ▸ bm Code", [("Ctrl+O", "open a .bm"), ("Ctrl+S", "save"), ("F5", "try the game"), ("Ctrl+K", "cut a line"),
                            ("Tab", "write the grey word"), ("F4", "two pages")])
    s.say("bm Code is the code editor of the console: tabs for several cartridges, a small sharp font, and only the code of a .bm changes when you save.")
    s.sec(6)
    s.say("Ctrl+O lists the cartridges on the card: we open Skyvale World, with its tiles, its map and its sounds.")
    s.raw("\\x0f", "Ctrl+O", "open a cartridge", 40)
    s.sec(2)
    s.key("enter", "choose skyvale.bm"); s.sec(2.5)
    s.say("The code of episode 2 only drew the map. Ctrl+K cuts a line: we start again, this time with a game.")
    for _ in range(OLD_LINES + 2):
        s.raw("\\x0b", "Ctrl+K", "cut a line", 4)
    s.sec(1)

    s.chapter("The body of Kip and the tiles")
    s.scene("bm Code ▸ typing", [("Enter", "new line, indented by itself"), ("Tab", "write the grey word"),
                               ("Ctrl+S", "save"), ("F5", "try the game")])
    s.say("bmlib is the game library of the console: require it, and a body is just a table with x, y, w and h, and a speed.")
    typed_block(s, PART1)
    s.key("enter", hold=3)
    s.say("lib.tiles tells bmlib how the map stops bodies: flag 0 is solid, flag 1 a platform. The flags we set in the SDK in episode 2. And music zero is the song from episode 3.")
    s.sec(4)

    s.chapter("Update: run, jump and collide")
    s.say("In update: left and right set the speed, A jumps if Kip is on the ground, with the jump sound from the bank. Let go of A early and the jump is cut short: a variable jump.")
    typed_block(s, PART2)
    s.key("enter", hold=3)
    s.sec(1)
    s.say("lib.step does the rest: gravity, and the collisions against the solid tiles and the platforms. And a tile with flag 4, the spikes, sends Kip back to the start.")
    s.sec(5)

    s.chapter("Draw")
    s.say("Draw: the sky, the camera moved down so the map ends at the bottom of the screen, the layers of the map, and Kip: a frame of the run every four steps, the jump frame in the air, mirrored when he goes left.")
    typed_block(s, PART3)
    s.sec(3)

    s.chapter("Save and play")
    s.scene("bm Code ▸ save and play", [("Ctrl+S", "save"), ("F5", "try the game")])
    s.say("Ctrl+S saves the code into the .bm: the sprites, the map and the sounds stay as they are. F5 runs it.")
    s.chord("s", "save"); s.sec(3)
    s.t_editor_end = s.t
    s.mark("F5", "try the game")
    s.scene("bm Code ▸ F5: the game", [("← →", "run"), ("A", "jump; let go early: a short jump"), ("F5", "try the game")])
    s.say("Kip runs, hops up through a platform that is solid only from above, falls on the spikes, and jumps the pit: a variable jump, and the music of episode 3.")
    s.t += GAME_SECONDS * FPS
    s.end()
    return s


INTRO = [("SKYVALE WORLD", 96, "0xffc050", 380),
         ("Episode 4  ·  bm Code", 56, "0xe0e4f0", 520),
         ("Kip runs and jumps", 34, "0x9098b0", 640)]
OUTRO = [("Next: Episode 5  ·  bm Code and the assistant", 52, "0xffc050", 400),
         ("Enemies, coins and a HUD", 38, "0xe0e4f0", 520),
         ("github.com/f-accomando/bm", 34, "0x9098b0", 660)]
HOOK = "TODAY: KIP RUNS AND JUMPS"


def join_wavs(a, b, out):
    with wave.open(a) as wa, wave.open(b) as wb, wave.open(out, "wb") as wo:
        wo.setparams(wa.getparams())
        wo.writeframes(wa.readframes(wa.getnframes()))
        wo.writeframes(wb.readframes(wb.getnframes()))


def main():
    os.makedirs(OUT, exist_ok=True)
    s = build()
    total = s.t + 30
    hook_s, intro_s, outro_s = 8, 4, 5
    offset = (hook_s + intro_s) * FPS
    s.write_input(os.path.join(OUT, "input.txt"))
    with open(os.path.join(OUT, "game.txt"), "w") as f:
        f.write("\n".join(GAME_INPUT) + "\n")
    write_ass(s, os.path.join(OUT, "overlay.ass"), TITLE, SUBTITLE, total)
    desc = ("Skyvale World is a 2D platformer built only with the tools inside the bm console. "
            "In this episode, bm Code: Kip runs and jumps with bmlib, stopped by the tiles of the map, "
            "a variable jump, spikes, and the sounds of the last episode.\n\nbm: https://github.com/f-accomando/bm")
    write_storyboard(s, os.path.join(HERE, "storyboard.md"), TITLE, offset)
    write_script(s, os.path.join(HERE, "copione.md"), TITLE, SUBTITLE, offset, desc)
    if "--build-only" in sys.argv:
        print("editor part %.0f s, total %.0f s" % (s.t_editor_end / FPS, total / FPS))
        return
    main_mp4 = os.path.join(OUT, "main.mp4")
    if "--no-record" not in sys.argv:
        for d in (SD, SD_GAME):
            shutil.rmtree(d, ignore_errors=True)
            os.makedirs(os.path.join(d, "carts"))
        shutil.copy(os.path.join(HERE, "start.bm"), os.path.join(SD, "carts", "SKYVALE.BM"))
        raw1, raw2 = os.path.join(OUT, "raw1.rgb"), os.path.join(OUT, "raw2.rgb")
        wav1, wav2 = os.path.join(OUT, "ed.wav"), os.path.join(OUT, "game.wav")
        call([BMHOST_AI, CODE_TOOL, "--tool", "--sd", SD, "--seconds", str(s.t_editor_end / FPS + 1), "--input",
              os.path.join(OUT, "input.txt"), "--video", raw1, "--wav", wav1, "--quiet"], cwd=ROOT)
        shutil.copy(os.path.join(SD, "carts", "SKYVALE.BM"), os.path.join(SD_GAME, "carts", "SKYVALE.BM"))
        call([BMHOST, os.path.join(SD_GAME, "carts", "SKYVALE.BM"), "--sd", SD_GAME, "--seconds", str(GAME_SECONDS),
              "--input", os.path.join(OUT, "game.txt"), "--video", raw2, "--wav", wav2, "--quiet"])
        raw = os.path.join(OUT, "raw.rgb")
        with open(raw, "wb") as out:
            for r in (raw1, raw2):
                with open(r, "rb") as f:
                    shutil.copyfileobj(f, out, 1 << 24)
        os.remove(raw1)
        os.remove(raw2)
        wav = os.path.join(OUT, "sound.wav")
        join_wavs(wav1, wav2, wav)
        call(encode(raw, os.path.join(OUT, "overlay.ass"), main_mp4, audio=wav))
        os.remove(raw)
    at = s.t_editor_end / FPS + 4
    for cmd in (hook_cmd(main_mp4, os.path.join(OUT, "hook.mp4"), at, hook_s, HOOK),
                card_cmd(os.path.join(OUT, "intro.mp4"), intro_s, INTRO, silent=True),
                card_cmd(os.path.join(OUT, "outro.mp4"), outro_s, OUTRO, silent=True)):
        call(cmd)
    call(concat_cmd([os.path.join(OUT, n) for n in ("hook.mp4", "intro.mp4", "main.mp4", "outro.mp4")],
                    os.path.join(OUT, "ep04.mp4"), audio=True))


if __name__ == "__main__":
    main()
