#!/usr/bin/env python3
"""
The showreel, put together: the scenes recorded by console.py (the SDK, bm
Code, the game in QEMU), sped up where it is work and at its own speed where
it moves, with the cards of cards.js, as an MP4 and a GIF.

  python3 tools/showreel/assemble.py SHOWREEL_DIR OUT.mp4 OUT.gif

SHOWREEL_DIR has console/ and cards/. Needs ffmpeg.
"""
import bisect
import json
import os
import shutil
import subprocess
import sys

FPS = 30
W, H = 1280, 720

# (source, from mark, to mark or seconds after it, seconds in the reel or
# None = real time, caption, caption place)
SCENES = [
    ("card", "title.png", 1.3),
    ("console", "map", "map-end", 2.4, "step1", "-tr"),
    ("console", "code", "code-edit", 2.2, "step2", "-tr"),
    ("console", "code-edit", "code-ai", 2.4, "step2", "-tr"),
    ("console", "code-ai", "run", 1.8, "step2", "-tr"),
    ("console", "play", "play-end", 4.4, "step3", "-tr"),
    ("card", "end.png", 2.0),
]


def ffmpeg(*args):
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", *args], check=True)


def load(d):
    with open(os.path.join(d, "frames.json")) as f:
        j = json.load(f)
    frames = j["frames"]
    return {"dir": os.path.join(d, "frames"), "frames": frames, "times": [f["t"] for f in frames],
            "marks": {m["name"]: m["t"] for m in j["marks"]}, "end": j["end"]}


def scene_frames(src, a, b, secs):
    """The source frame for each frame of the reel: uniform in time."""
    t0 = src["marks"][a]
    t1 = t0 + b if isinstance(b, (int, float)) else src["marks"][b]
    secs = secs or (t1 - t0)
    n = max(1, round(secs * FPS))
    out = []
    for k in range(n):
        t = t0 + (t1 - t0) * k / n
        i = max(0, bisect.bisect_right(src["times"], t) - 1)
        out.append(os.path.join(src["dir"], src["frames"][i]["f"]))
    return out, n / FPS


def main():
    base, mp4, gif = sys.argv[1:4]
    tmp = os.path.join(base, "segments")
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    srcs = {"console": load(os.path.join(base, "console"))}
    cards = os.path.join(base, "cards")
    parts, total = [], 0.0
    for i, sc in enumerate(SCENES):
        seg = os.path.join(tmp, "%02d.mp4" % i)
        enc = ["-c:v", "libx264", "-preset", "medium", "-crf", "12", "-pix_fmt", "yuv420p", "-r", str(FPS)]
        if sc[0] == "card":
            secs = sc[2]
            fade = f"fade=in:st=0:d=0.35,fade=out:st={secs - 0.35:.2f}:d=0.35" if i else f"fade=out:st={secs - 0.3:.2f}:d=0.3"
            ffmpeg("-loop", "1", "-framerate", str(FPS), "-t", f"{secs:.3f}", "-i", os.path.join(cards, sc[1]),
                   "-vf", f"format=rgb24,{fade}", *enc, seg)
        else:
            src, a, b, secs, cap, place = sc
            files, secs = scene_frames(srcs[src], a, b, secs)
            d = os.path.join(tmp, "%02d" % i)
            os.makedirs(d)
            for k, f in enumerate(files):
                os.symlink(os.path.abspath(f), os.path.join(d, "%06d.png" % k))
            scale = f"scale={W}:{H}:flags=neighbor"           # the console's pixels stay sharp
            ffmpeg("-framerate", str(FPS), "-i", os.path.join(d, "%06d.png"), "-i", os.path.join(cards, cap + place + ".png"),
                   "-filter_complex", f"[0:v]{scale},format=rgb24[v];[v][1:v]overlay=0:0:format=auto", *enc, seg)
        parts.append(seg)
        total += secs
        print(f"{i:2d} {sc[0]:8s} {sc[1]:14s} {secs:5.2f} s", flush=True)
    lst = os.path.join(tmp, "list.txt")
    with open(lst, "w") as f:
        f.writelines(f"file '{os.path.abspath(p)}'\n" for p in parts)
    os.makedirs(os.path.dirname(os.path.abspath(mp4)), exist_ok=True)
    ffmpeg("-f", "concat", "-safe", "0", "-i", lst, "-c:v", "libx264", "-preset", "slow", "-crf", "21",
           "-pix_fmt", "yuv420p", "-movflags", "+faststart", mp4)
    # the GIF for the README: smaller, fewer frames, one palette
    ffmpeg("-i", mp4, "-vf", "fps=12,scale=720:-1:flags=lanczos,split[a][b];"
           "[a]palettegen=max_colors=192:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle",
           "-loop", "0", gif)
    print(f"reel: {total:.1f} s; {mp4} {os.path.getsize(mp4) // 1024} KiB, {gif} {os.path.getsize(gif) // 1024} KiB")


if __name__ == "__main__":
    main()
