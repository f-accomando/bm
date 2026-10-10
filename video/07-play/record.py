#!/usr/bin/env python3
"""Episode 7: Skyvale World, the game. The cartridge made in the six episodes played from
the title to the flag (the buttons of game.txt, made by bot.py, with the keys on the side),
a reel of the six episodes, and the SDK's dev kit on the finished file: what is in the .bm.

  python3 video/07-play/record.py [--build-only] [--no-record]

start.bm is the cartridge episode 6 saved. Needs build/host/bmhost-bin, bmhost-ai,
build/editor.bm, and the episode videos (video/0N-*/ep0N.mp4) for the reel.
"""
import os
import shutil
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "video", "lib"))

from bmvideo import (Script, call, encode, write_ass, FPS, write_storyboard, write_script,   # noqa: E402
                     card_cmd, hook_cmd, concat_cmd, FONT, FONT_R)

OUT = os.path.join(HERE, "out")
BMHOST = os.path.join(ROOT, "build", "host", "bmhost-bin")
BMHOST_AI = os.path.join(ROOT, "build", "host", "bmhost-ai")
SDK = os.path.join(ROOT, "build", "editor.bm")
SD_GAME = os.path.join(OUT, "sd-game")
SD_SDK = os.path.join(OUT, "sd-sdk")
TITLE = "SKYVALE WORLD  ·  Episode 7: the game"
SUBTITLE = "bm · the bare-metal fantasy console"

EPISODES = [("01-pixel", "ep01.mp4", "1 · bm Pixel: Kip and his run"),
            ("02-sdk", "ep02.mp4", "2 · the SDK: tiles, flags and the map"),
            ("03-sound", "ep03.mp4", "3 · bm Sound: effects and music"),
            ("04-code", "ep04.mp4", "4 · bm Code: running and jumping"),
            ("05-assistant", "ep05.mp4", "5 · the assistant, coins and slimes"),
            ("06-level", "ep06.mp4", "6 · the last level")]
CLIP_S = 7


def game_input():
    return [l.strip() for l in open(os.path.join(HERE, "game.txt")) if l.strip()]


def build_play():
    """Part A: the game, with the keys of the script on the side."""
    s = Script()
    s.chapter("Skyvale World, from the title to the flag")
    s.scene("Skyvale World ▸ title", [("A", "start"), ("← →", "run"), ("A", "jump (let go early: a short jump)")])
    s.say("Skyvale World, made with nothing but the tools of the console: bm Pixel, the SDK, bm Sound and bm Code.")
    prev = set()
    for ln in game_input():
        frame, rest = ln.split(" ", 1)
        keys = set(rest.split()[2:]) - {"none"}
        frame = int(frame)
        s.t = frame
        if "a" in keys and "a" not in prev:
            s.mark("A", "start" if frame < 100 else "jump")
        if "right" in keys and "right" not in prev:
            s.mark("→", "run")
        prev = keys
    s.t = 0
    # narrator lines at fixed frames: the run is the same every time
    for frame, text in ((70, "The title, then A: Kip runs to the right, and the camera follows him."),
                        (180, "Hops for the coins, and the clouds and the trees behind move more slowly than the ground: parallax."),
                        (330, "A full jump over the first pit, spikes at its bottom."),
                        (520, "The slimes: jump on them and they are gone, and a hundred points."),
                        (610, "And the flag at the end of the level."),
                        (700, "Course clear: the score, and no life lost. Every sprite, tile, sound and line of code made in six episodes.")):
        s.t = frame
        s.say(text)
    s.t = GAME_SECONDS * FPS
    s.end()
    return s


def build_sdk():
    """Part C: what is in the .bm, in the SDK's project page and dev kit."""
    s = Script()
    s.chapter("What is inside the .bm")
    s.scene("bm SDK ▸ F1 project", [("Ctrl+O", "open a .bm"), ("F1", "project; again: the dev kit"), ("F3", "2D: sprites; again: the map"),
                                  ("F5", "try the game")])
    s.say("One file holds all of it. The SDK shows what is in it: open Skyvale World.")
    s.sec(3)
    s.raw("\\x0f", "Ctrl+O", "open a cartridge", 40)
    s.sec(1.5)
    s.key("enter", "choose skyvale.bm"); s.sec(3)
    s.say("The project page: the lines of code and its tokens, the drawn cells of the sheet, the cells of the map and its layers, the sound bank.")
    s.sec(6)
    s.key("f1", "the dev kit")
    s.scene("bm SDK ▸ F1 again: the dev kit", [("F1", "project / dev kit"), ("F5", "try the game")])
    s.say("F1 again, the dev kit: the memory the data takes while the game runs, and the size of the file against the eight megabytes of a .b16, the format of the RGB30.")
    s.sec(7)
    s.key("f3", "2D: sprites")
    s.scene("bm SDK ▸ F3 sprites and map", [("F3", "sprites; again: the map"), ("c", "show the flags")])
    s.say("The sheet and the map, as we left them: the tiles, the three layers, the level a hundred and sixty cells long.")
    s.sec(3)
    s.key("f3", "the map"); s.sec(5)
    s.say("Thanks for watching. Skyvale World is a game made only with bm: the console, its editors and its assistant.")
    s.sec(4)
    s.end()
    return s


GAME_SECONDS = 15


INTRO = [("SKYVALE WORLD", 96, "0xffc050", 380),
         ("Episode 7  ·  the game", 56, "0xe0e4f0", 520),
         ("Made with nothing but the tools of bm", 34, "0x9098b0", 640)]
RECAP = [("HOW IT WAS MADE", 80, "0xffc050", 420),
         ("Six episodes, one tool at a time", 40, "0xe0e4f0", 540)]
OUTRO = [("SKYVALE WORLD", 96, "0xffc050", 330),
         ("bm: the bare-metal fantasy console", 44, "0xe0e4f0", 470),
         ("github.com/f-accomando/bm", 40, "0x9098b0", 560),
         ("A game made with bm Pixel, the SDK, bm Sound, bm Code and the assistant", 28, "0x9098b0", 640)]


def clip_cmd(src, out, label, seconds=CLIP_S):
    """A cut of an episode, brought to the same format, with a line naming it, and sound
    (silent when the episode has none)."""
    t = label.replace(":", "\\:").replace("'", "’")
    vf = ("drawbox=x=0:y=ih-120:w=iw:h=120:color=0x0b0d13@0.85:t=fill,"
          "drawtext=fontfile=%s:text='EPISODE %s':fontsize=52:fontcolor=0xffc050:x=(w-text_w)/2:y=h-92,"
          "fade=t=in:st=0:d=0.3,fade=t=out:st=%.2f:d=0.3" % (FONT, t, seconds - 0.3))
    return ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-t", str(seconds), "-i", src,
            "-f", "lavfi", "-t", str(seconds), "-i", "anullsrc=r=48000:cl=stereo",
            "-filter_complex", "[0:v]%s,fps=60[v];[0:a][1:a]amix=inputs=2:duration=first:normalize=0[a]" % vf
            if has_audio(src) else "[0:v]%s,fps=60[v];[1:a]anull[a]" % vf,
            "-map", "[v]", "-map", "[a]", "-c:v", "libx264", "-crf", "19", "-pix_fmt", "yuv420p",
            "-c:a", "aac", "-ar", "48000", "-ac", "2", out]


def has_audio(path):
    import subprocess
    r = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a", "-show_entries", "stream=codec_type", "-of", "csv=p=0", path],
                       capture_output=True, text=True)
    return "audio" in r.stdout


def stereo_fix(src, out):
    """Make a part stereo 48 kHz so every part joins with the same audio format."""
    return ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", src, "-c:v", "copy", "-c:a", "aac", "-ar", "48000", "-ac", "2", out]


def main():
    os.makedirs(OUT, exist_ok=True)
    a, c = build_play(), build_sdk()
    ta, tc = a.t + 30, c.t + 30
    write_ass(a, os.path.join(OUT, "overlay-play.ass"), TITLE, SUBTITLE, ta)
    write_ass(c, os.path.join(OUT, "overlay-sdk.ass"), TITLE, SUBTITLE, tc)
    write_storyboard(a, os.path.join(HERE, "storyboard.md"), TITLE, 12 * FPS)
    write_script(a, os.path.join(HERE, "copione.md"), TITLE, SUBTITLE, 12 * FPS,
                 "Skyvale World is a 2D platformer built only with the tools inside the bm console. In this last episode "
                 "the game, played from the title to the flag, a look back at the six episodes, and what is inside the .bm."
                 "\n\nbm: https://github.com/f-accomando/bm")
    c.write_input(os.path.join(OUT, "input-sdk.txt"))
    if "--build-only" in sys.argv:
        print("play %.0f s, sdk %.0f s" % (ta / FPS, tc / FPS))
        return
    if "--no-record" not in sys.argv:
        for d in (SD_GAME, SD_SDK):
            shutil.rmtree(d, ignore_errors=True)
            os.makedirs(os.path.join(d, "carts"))
            shutil.copy(os.path.join(HERE, "start.bm"), os.path.join(d, "carts", "SKYVALE.BM"))
        call([BMHOST, os.path.join(SD_GAME, "carts", "SKYVALE.BM"), "--sd", SD_GAME, "--seconds", str(GAME_SECONDS),
              "--input", os.path.join(HERE, "game.txt"), "--video", os.path.join(OUT, "raw-play.rgb"),
              "--wav", os.path.join(OUT, "play.wav"), "--quiet"])
        call(encode(os.path.join(OUT, "raw-play.rgb"), os.path.join(OUT, "overlay-play.ass"), os.path.join(OUT, "play.mp4"),
                    audio=os.path.join(OUT, "play.wav")))
        os.remove(os.path.join(OUT, "raw-play.rgb"))
        call([BMHOST_AI, SDK, "--tool", "--sd", SD_SDK, "--seconds", str(c.t / FPS + 1), "--input",
              os.path.join(OUT, "input-sdk.txt"), "--video", os.path.join(OUT, "raw-sdk.rgb"),
              "--wav", os.path.join(OUT, "sdk.wav"), "--quiet"], cwd=ROOT)
        call(encode(os.path.join(OUT, "raw-sdk.rgb"), os.path.join(OUT, "overlay-sdk.ass"), os.path.join(OUT, "sdk.mp4"),
                    audio=os.path.join(OUT, "sdk.wav")))
        os.remove(os.path.join(OUT, "raw-sdk.rgb"))
    parts = []
    # the reel of the six episodes
    call(card_cmd(os.path.join(OUT, "recap.mp4"), 3, RECAP, silent=True))
    clips = []
    for d, name, label in EPISODES:
        src = os.path.join(ROOT, "video", d, name)
        out = os.path.join(OUT, "clip-%s.mp4" % d)
        call(clip_cmd(src, out, label))
        clips.append(out)
    for p in ("play", "sdk"):
        call(stereo_fix(os.path.join(OUT, p + ".mp4"), os.path.join(OUT, p + "-st.mp4")))
    call(hook_cmd(os.path.join(OUT, "play.mp4"), os.path.join(OUT, "hook.mp4"), 4.5, 7, "SKYVALE WORLD: THE GAME"))
    call(stereo_fix(os.path.join(OUT, "hook.mp4"), os.path.join(OUT, "hook-st.mp4")))
    call(card_cmd(os.path.join(OUT, "intro.mp4"), 4, INTRO, silent=True))
    call(card_cmd(os.path.join(OUT, "outro.mp4"), 7, OUTRO, silent=True))
    for n in ("intro", "outro", "recap"):
        call(stereo_fix(os.path.join(OUT, n + ".mp4"), os.path.join(OUT, n + "-st.mp4")))
    order = ["hook-st", "intro-st", "play-st", "recap-st"] + [os.path.basename(c)[:-4] for c in clips] + ["sdk-st", "outro-st"]
    call(concat_cmd([os.path.join(OUT, n + ".mp4") for n in order], os.path.join(OUT, "ep07.mp4"), audio=True))


if __name__ == "__main__":
    main()
