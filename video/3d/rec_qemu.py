#!/usr/bin/env python3
"""Records an episode of the series in QEMU (the kernel of the Pi) from what its record.py wrote.

  python3 video/3d/rec_qemu.py 03 [--shift 2] [--no-intro] [--limit-s SEC]

Reads  video/3d/NN-name/out/input.txt     (bmhost `FRAME type CODES` / `pad` / `shot`; frame 0 = the tool just opened)
       video/3d/NN-name/out/overlay.json  (keys, scenes, narration, chapters; lib3d.write_overlay)
       video/3d/NN-name/out/sd/carts/*    (the files of the clean SD that record.py prepared: the start project)
Makes  out/qemu/console.mkv  (640x360, lossless, 60 fps of guest time)
       out/qemu/overlay.ass, subs.srt, epNN_qemu.mp4   (1920x1080)
       out/qemu/sd/                        (the files the tool saved on the card: for verify.py and the chain)

The tool is opened the way a player does it: Dev tab (Tab), the tool's icon (Right x N), Enter. That walk is the
first seconds of the video (the shot list's "Dev tab" rows) unless --no-intro.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time
import types

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "video", "lib"))
import bmvideo  # noqa: E402
import qemurec  # noqa: E402

FPS = 60
# the Dev tab: icons in this order (checked on the screen); the number of Right presses from the first
DEV_ICON = {"SDK": 0, "SOUND": 1, "STUDIO": 2, "ANIMATOR": 3, "MESH": 4, "PIXEL": 5, "CODE": 6, "ASSISTANT": 7}
# a pad line of bmhost on the USB keyboard (docs/api/input.md)
PAD_KEY = {"left": "left", "right": "right", "up": "up", "down": "down", "a": "spc", "b": "x", "x": "c", "y": "v",
           "start": "ret", "select": "tab", "l1": "q", "r1": "e"}


def episode_dir(n):
    s = json.load(open(os.path.join(HERE, "series.json"), encoding="utf-8"))
    v = next(x for x in s["videos"] if x["n"] == int(n))
    return os.path.join(HERE, v["dir"]), v


def pad_to_keys(args, held):
    """`pad 1 a right` / `pad 1 none` -> (keys to press down, keys to release); `held` is the set kept between lines."""
    parts = args.split()[1:]
    want = set() if parts == ["none"] else {PAD_KEY[p] for p in parts if p in PAD_KEY}
    down, up = sorted(want - held), sorted(held - want)
    held.clear()
    held.update(want)
    return down, up


def run(n, shift=2, intro=True, limit_s=None, out_sub="qemu"):
    ep, v = episode_dir(n)
    out = os.path.join(ep, "out")
    work = os.path.join(out, out_sub)
    os.makedirs(work, exist_ok=True)
    events = qemurec.parse_input(os.path.join(out, "input.txt"))
    ov = json.load(open(os.path.join(out, "overlay.json"), encoding="utf-8"))
    sd_dir = os.path.join(out, "sd", "carts")
    files = [(os.path.join(sd_dir, f), "carts/" + f) for f in sorted(os.listdir(sd_dir))] if os.path.isdir(sd_dir) else []
    nx = DEV_ICON[v["tool"]]

    r = qemurec.Recorder(work, shift=shift)
    r.boot(sd_files=files, timeout=240)
    settle = r.vtime_us() + 3e6                       # the menu is up (the first seconds are a test pattern)
    while r.vtime_us() < settle:
        time.sleep(0.05)

    # the walk to the tool: frames of the video before the episode's frame 0
    pre, f = [], 30
    pre.append((f, "type", "\\x09"))                  # Tab: Games -> Dev
    f += 90
    for _ in range(nx):
        pre.append((f, "type", "\\xf3"))              # Right: next icon
        f += 30
    f += 60
    pre.append((f, "type", "\\n"))                    # Enter: open the tool
    f += 60 * 6                                       # loading
    offset = f if intro else 0
    if not intro:
        # open the tool but do not show it: the recording starts when it is up
        for fr, kind, args in pre:
            while r.vtime_us() < settle + fr * qemurec.US_PER_FRAME:
                time.sleep(0.005)
            for chord in qemurec.decode(args):
                r.press(chord)
        t_end = r.vtime_us() + 6e6
        while r.vtime_us() < t_end:
            time.sleep(0.05)
        pre = []

    total = offset + int(ov["frames"])
    if limit_s:
        total = min(total, offset + int(limit_s * FPS))
    held, ev = set(), list(pre)
    for fr, kind, args in events:
        if kind == "type":
            ev.append((offset + fr, "type", args))
        elif kind == "pad":
            down, up = pad_to_keys(args, held)
            for k in up:
                ev.append((offset + fr, "keyup", k))
            for k in down:
                ev.append((offset + fr, "keydown", k))
        elif kind == "shot":
            ev.append((offset + fr, "shot", args))
    ev.sort(key=lambda e: e[0])

    shots = r.record(ev, total, os.path.join(work, "console.mkv"))
    print("recorded:", r.stats, flush=True)

    # overlay: the episode's data shifted by `offset`, plus the walk to the tool
    s = types.SimpleNamespace(
        caps=[(offset + k["frame"], k["cap"], k["what"]) for k in ov["keys"]],
        scenes=[(offset + sc["frame"], sc["place"], [(c["cap"], c["what"]) for c in sc["keys"]]) for sc in ov["scenes"]],
        lines_say=[(offset + a["from"], None if a["to"] is None else offset + a["to"], a["text"]) for a in ov["say"]],
        cards=[(offset + c["frame"], c["title"]) for c in ov["chapters"]])
    if intro:
        s.scenes.insert(0, (0, "Home ▸ Dev", [("Tab", "next tab"), ("→", "next icon"), ("Enter", "open")]))
        s.caps[0:0] = [(30, "Tab", "Dev tab")] + [(30 + 90 + 30 * i, "→", "next icon") for i in range(nx)] + \
                      [(30 + 90 + 30 * nx + 60, "Enter", "open " + v["tool"].title())]
    if limit_s:
        s.scenes = [x for x in s.scenes if x[0] < total]
    ass = os.path.join(work, "overlay.ass")
    bmvideo.write_ass(s, ass, ov.get("title") or "Tropic Trail", v["tool"].title(), total)
    qemurec.write_srt(s, os.path.join(work, "subs.srt"))
    mp4 = os.path.join(work, "ep%02d_qemu.mp4" % int(n))
    qemurec.assemble(os.path.join(work, "console.mkv"), ass, mp4)

    r.close()
    qemurec.extract_sd(os.path.join(work, "sd.img"), os.path.join(work, "sd"))
    json.dump({"frames": total, "offset": offset, "shift": shift, "stats": r.stats, "shots": shots},
              open(os.path.join(work, "rec.json"), "w"), indent=1)
    print("video:", mp4)
    return mp4


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("n")
    ap.add_argument("--shift", type=int, default=2)
    ap.add_argument("--no-intro", action="store_true")
    ap.add_argument("--limit-s", type=float)
    a = ap.parse_args()
    run(a.n, a.shift, not a.no_intro, a.limit_s)
