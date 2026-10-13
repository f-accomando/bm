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


def retime(events, min_gap=4):
    """The same key must come at least `min_gap` frames after its last press, or the USB keyboard of QEMU merges the
    release and the next press into one report and the console sees ONE press (bmhost's scripts put a "." every 2 frames).
    Later events are pushed back just enough, and the idle gaps give the time back. Returns (events, delta_at) where delta_at(f)
    is how many frames the original frame f moved."""
    last, delta, marks, out = {}, 0, [(-1, 0)], []
    prev_f = None
    for f, kind, args in events:
        if prev_f is not None and delta and f - prev_f >= 30:    # an idle gap: take back what the fast keys cost
            back = min(delta, f - prev_f - 20)
            delta -= back
            marks.append((f, delta))
        prev_f = f
        nf = f + delta
        if kind == "type":
            keys = ["+".join(c) for c in qemurec.decode(args)]
            need = max([last.get(k, -99) + min_gap for k in keys] + [nf])
            if need > nf:
                delta += need - nf
                nf = need
                marks.append((f, delta))
            for k in keys:
                last[k] = nf
        out.append((nf, kind, args))

    def delta_at(f):
        d = 0
        for fr, dl in marks:
            if fr <= f:
                d = dl
        return d
    return out, delta_at


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


# episodes made of several tools on the same SD card, in the timeline of out/input.txt (frames of the edited video):
# (tool, first frame, last frame). The others are one tool from frame 0.
PARTS = {2: [("PIXEL", 480, 8520), ("STUDIO", 8640, None)]}


def walk(prev, tool):
    """The keys that open `tool` from where the console is: first time from the Games tab (Tab, Right x N, Enter), after
    another tool from its Dev icon (Ctrl+Esc = home, Left/Right to the icon, Enter). Returns ([(rel frame, kind, args)], length)."""
    ev, f = [], 20
    if prev is None:
        ev.append((f, "type", "\\x09"))
        f += 60
        steps = DEV_ICON[tool]
        key = "\\xf3"
    else:
        ev.append((f, "chord", "ctrl+esc"))              # Ctrl+Esc: home
        f += 60
        steps = DEV_ICON[tool] - DEV_ICON[prev]
        key = "\\xf3" if steps > 0 else "\\xf2"
    for _ in range(abs(steps)):
        ev.append((f, "type", key))
        f += 20
    f += 40
    ev.append((f, "type", "\\n"))
    f += 60 * 3                                        # the tool loads (about 1 s of guest time) and shows its list
    return ev, f


def run(n, shift=2, intro=True, limit_s=None, out_sub="qemu"):
    ep, v = episode_dir(n)
    out = os.path.join(ep, "out")
    work = os.path.join(out, out_sub)
    os.makedirs(work, exist_ok=True)
    events = qemurec.parse_input(os.path.join(out, "input.txt"))
    ov = json.load(open(os.path.join(out, "overlay.json"), encoding="utf-8"))
    sd_dir = os.path.join(out, "sd", "carts")
    files = [(os.path.join(sd_dir, f), "carts/" + f) for f in sorted(os.listdir(sd_dir))] if os.path.isdir(sd_dir) else []
    start = os.path.join(out, "start.bme")             # record.py's start project (out/sd holds what its bmhost run SAVED)
    if os.path.exists(start):
        files = [(src, name) for src, name in files if not name.upper().endswith(".BME")] + [(start, "carts/TROPIC.BME")]
    orig_events = events
    events, delta_at = retime(events)
    print("retime: the last event moved %d frames" % delta_at(10 ** 9), flush=True)
    parts = PARTS.get(int(n), [(v["tool"], 0, None)])
    parts = [(t, a, b if b is not None else 10 ** 9) for t, a, b in parts]

    # the timeline: each part is shifted by `extra` frames so that its tool is up when its first key comes
    plan, extra, prev, prev_end = [], 0, None, 0
    for tool, a, b in parts:
        w_ev, w_len = walk(prev, tool)
        gap = a - prev_end
        extra += max(0, w_len - gap) if (intro or prev is not None) else 0
        start = a + extra - w_len
        plan.append({"tool": tool, "a": a, "b": b, "extra": extra, "walk": [(start + f, k, x) for f, k, x in w_ev],
                     "walk_start": start, "walk_end": a + extra})
        prev, prev_end = tool, b

    def shifted(f):
        e = plan[0]["extra"]
        for p_ in plan:
            if f >= p_["a"]:
                e = p_["extra"]
        return f + e + delta_at(f)

    total = shifted(int(ov["frames"]))
    if limit_s:
        total = min(total, plan[0]["walk_end"] + int(limit_s * FPS))

    r = qemurec.Recorder(work, shift=shift)
    r.boot(sd_files=files, timeout=240)
    settle = r.vtime_us() + 3e6                       # the menu is up (the first seconds are a test pattern)
    while r.vtime_us() < settle:
        time.sleep(0.05)

    held, ev = set(), []
    for p_ in plan:
        ev += p_["walk"]
    for (fr, _, _), (nf, kind, args) in zip(orig_events, events):
        part = next((p_ for p_ in plan if p_["a"] <= fr < p_["b"]), None)
        if part is None:
            continue                                  # hook and cut frames: not in the recording
        f = nf + part["extra"]
        if kind == "type":
            ev.append((f, "type", args))
        elif kind == "pad":
            down, up = pad_to_keys(args, held)
            ev += [(f, "keyup", k) for k in up] + [(f, "keydown", k) for k in down]
        elif kind == "shot":
            ev.append((f, "shot", args))
    ev.sort(key=lambda e: e[0])

    shots = r.record(ev, total, os.path.join(work, "console.mkv"))
    print("recorded:", r.stats, flush=True)

    # overlay: the episode's data shifted like the keys, plus the walks to the tools
    s = types.SimpleNamespace(
        caps=[(shifted(k["frame"]), k["cap"], k["what"]) for k in ov["keys"]],
        scenes=[(shifted(sc["frame"]), sc["place"], [(c["cap"], c["what"]) for c in sc["keys"]]) for sc in ov["scenes"]],
        lines_say=[(shifted(a["from"]), None if a["to"] is None else shifted(a["to"]), a["text"]) for a in ov["say"]],
        cards=[(shifted(c["frame"]), c["title"]) for c in ov["chapters"]])
    for p_ in plan:
        w = p_["walk_start"]
        s.scenes.append((w, "Home ▸ Dev", [("Tab / ← →", "pick the tool"), ("Enter", "open")]))
        s.caps.append((w + 20, "Tab" if p_ is plan[0] else "Ctrl+Esc", "Dev tab" if p_ is plan[0] else "home"))
        s.caps.append((p_["walk_end"] - 60 * 3 - 40, "Enter", "open " + p_["tool"].title()))
    s.scenes.sort(key=lambda x: x[0])
    s.caps.sort(key=lambda x: x[0])
    if limit_s:
        s.scenes = [x for x in s.scenes if x[0] < total]
    ass = os.path.join(work, "overlay.ass")
    bmvideo.write_ass(s, ass, ov.get("title") or "Tropic Trail", v["tool"].title(), total)
    qemurec.write_srt(s, os.path.join(work, "subs.srt"))
    mp4 = os.path.join(work, "ep%02d_qemu.mp4" % int(n))
    qemurec.assemble(os.path.join(work, "console.mkv"), ass, mp4)

    r.close()
    qemurec.extract_sd(os.path.join(work, "sd.img"), os.path.join(work, "sd"))
    json.dump({"frames": total, "plan": [{k: v_ for k, v_ in p_.items() if k != "walk"} for p_ in plan], "shift": shift,
               "stats": r.stats, "shots": shots}, open(os.path.join(work, "rec.json"), "w"), indent=1)
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
