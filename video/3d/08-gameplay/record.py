#!/usr/bin/env python3
"""Tropic Trail, episode 8: the game, played by a bot (docs/video/08-gameplay/).

  python3 video/3d/08-gameplay/record.py [--rebot] [--video]      (run in WSL/Linux: bmhost is a Linux binary)

  1. out/sd/carts/TROPIC.BM = the game of video 7 (chain/07-code.bm, or video/3d/07-code/out/07-code.bm);
  2. bot.py plays it -> replay.txt (the button script; only if it is missing or with --rebot);
  3. bmhost-bin runs the game with replay.txt, no --tool: shots out/shots/p*.png and out/play.wav;
  4. with --video (needs ffmpeg): out/game.mp4, 480x270 x4 = 1920x1080 nearest, a small box with the buttons, the game's own sound.
The level of video 4 is short (18 units, 7 apples, 1 crate): the played part is about 9 s, the clear screen stays 5 s.
The montage of the episode (hook, cards, the clips of videos 1-7) needs their mp4s: see NOTES.md."""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, HERE)
import bot  # noqa: E402

OUT = os.path.join(HERE, "out")
SD = os.path.join(OUT, "sd")
START = 240                                     # frames of the title screen before A
SECONDS = 15                                    # title 4 s, run about 5 s, the clear screen the rest
BOX = "left  right  up  run     A  jump     B  spin"


def find_cart():
    for p in (os.path.join(ROOT, "video", "3d", "chain", "07-code.bm"), os.path.join(ROOT, "video", "3d", "chain-delivered", "07-code.bm"),
              os.path.join(ROOT, "video", "3d", "07-code", "out", "07-code.bm")):
        if os.path.exists(p):
            return p
    raise SystemExit("no 07-code.bm: run video/3d/07-code/record.py first")


def main():
    args = sys.argv[1:]
    os.makedirs(OUT, exist_ok=True)
    cart = find_cart()
    shutil.rmtree(SD, ignore_errors=True)
    os.makedirs(os.path.join(SD, "carts"))
    shutil.copy(cart, os.path.join(SD, "carts", "TROPIC.BM"))
    replay = os.path.join(HERE, "replay.txt")
    if "--rebot" in args or not os.path.exists(replay):
        params = dict(bot.DEFAULTS)
        params["start"] = START
        info = bot.run(cart, os.path.join(OUT, "bot-sd"), params, SECONDS, replay)
        print("bot:", "ok" if info["ok"] else info["text"][-300:], "apples", info["apples"], "crates", info["crates"], "hits", info["hits"],
              "falls", info["falls"], "clear", info["clear"].groups() if info["clear"] else None)
    exe = os.path.join(ROOT, "build", "host", "bmhost-bin")
    shots = os.path.join(OUT, "shots")
    shutil.rmtree(shots, ignore_errors=True)
    os.makedirs(shots)
    with open(replay) as f:                     # the same script plus shots
        lines = [l for l in f.read().splitlines() if l.strip()]
    lines += ["%d shot p%04d" % (fr, fr) for fr in (120, 300, 420, 540, 700, 840)]
    lines.sort(key=lambda l: int(l.split()[0]))
    inp = os.path.join(OUT, "play-input.txt")
    with open(inp, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    cmd = [exe, os.path.join(SD, "carts", "TROPIC.BM"), "--sd", SD, "--seconds", str(SECONDS), "--input", inp, "--shots", shots,
           "--wav", os.path.join(OUT, "play.wav"), "--quiet"]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    print("play", (r.stderr or r.stdout)[-200:].strip())
    if "--video" in args:
        if not shutil.which("ffmpeg"):
            print("no ffmpeg here: run with --video where ffmpeg is installed (CLD)")
            return 0
        raw = os.path.join(OUT, "play.rgb")
        subprocess.run(cmd + ["--video", raw], cwd=ROOT, capture_output=True, text=True)
        font = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
        vf = ("scale=1920:1080:flags=neighbor,drawbox=x=24:y=ih-72:w=760:h=48:color=black@0.5:t=fill,"
              "drawtext=fontfile=%s:text='%s':x=40:y=h-62:fontsize=26:fontcolor=white" % (font, BOX))
        subprocess.check_call(["ffmpeg", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "480x270", "-r", "60", "-i", raw,
                               "-i", os.path.join(OUT, "play.wav"), "-vf", vf, "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac",
                               "-shortest", os.path.join(OUT, "game.mp4")])
        os.remove(raw)
        print("out/game.mp4")
    return 0


if __name__ == "__main__":
    sys.exit(main())
