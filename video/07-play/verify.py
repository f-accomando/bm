#!/usr/bin/env python3
"""Checks episode 7 from the files: the button script (game.txt) plays the cartridge
from the title to the flag, no life lost, with coins and a stomp (replayed with a log,
as bot.py saw it); the final video has video and sound and the length the parts add up to.

  python3 video/07-play/verify.py
"""
import importlib.util
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
spec = importlib.util.spec_from_file_location("rp", os.path.join(HERE, "replay.py"))
rp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rp)
bad = 0


def report(name, ok, detail=""):
    global bad
    print("%-46s %s %s" % (name, "ok" if ok else "WRONG", "" if ok else detail))
    bad += 0 if ok else 1


i = rp.replay(os.path.join(HERE, "start.bm"), os.path.join(HERE, "game.txt"), os.path.join(HERE, "out", "sd-verify"), 30)
report("the game runs without error", i["ok"], i["text"][-200:])
report("title to flag in under 15 s, score kept", i["clear"] is not None and i["clear"][0] < 15 * 60, i["clear"])
report("no life lost", not i["lives"] and i["clear"][2] == 3, i["lives"])
report("coins and a stomp", i["coins"] >= 8 and i["stomps"] >= 1, (i["coins"], i["stomps"]))

mp4 = os.path.join(HERE, "out", "ep07.mp4")
r = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration:stream=codec_type", "-of", "csv=p=0", mp4],
                   capture_output=True, text=True)
report("video and sound streams", "video" in r.stdout and "audio" in r.stdout, r.stdout)
dur = float([l for l in r.stdout.split() if l.replace(".", "").isdigit()][-1])
# hook 7 + intro 4 + play 16 + recap 3 + 6 clips of 7 + sdk 34 + outro 7
report("length = the parts added up (113 s)", abs(dur - 113) < 2, dur)
sys.exit(1 if bad else 0)
