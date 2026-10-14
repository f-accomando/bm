#!/usr/bin/env python3
"""Tropic Trail, episode 6: the montage of the final video (docs/video/06-sdk/).

  python3 video/3d/06-sdk/montage.py [--r2-only] [--skip-r2]

The parts, in order: hook (6 s, a cut of R2), intro (4 s card), R1 the editor (112 s, qemu),
R2 the cartridge running by itself (12 s, no keys), R3 the dev kit and Ctrl+B (38 s, qemu), outro (8 s card).
record.py writes the inputs of R2 (out/sd, out/06-sdk.bm, out/r2-input.txt); this script runs bmhost on them
(no --tool: R2 is a game) and encodes the frames twice: out/r2.mp4 with the narrator's lines, out/r2_clean.mp4
without (the hook). The qemu recordings are out/qemu-r1/ep06_qemu_r1.mp4 and out/qemu-r3/ep06_qemu_r3.mp4.
Output: out/ep06.mp4. Standard library only; ffmpeg and ffprobe needed."""
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "video", "3d"))

import lib3d                                    # noqa: E402
from bmvideo import FPS, ass_time, esc, call, card_cmd, concat_cmd, hook_cmd, run_bmhost   # noqa: E402

OUT = os.path.join(HERE, "out")
PARTS = os.path.join(OUT, "parts")
W, H = 480, 270                                 # R2 as bmhost writes it: rgb24 frames
FRAME_BYTES = W * H * 3
R2_SECONDS = 12

R2_LINES = [(0, 4, "Pico runs on his own"), (4, 8, "The lantern lights the path"), (8, 12, "The shadow follows Pico")]

# the style Say of bmvideo.make_ass (narrator's line at the bottom)
ASS_HEAD = """[Script Info]
ScriptType: v4.00+
PlayResX: 1920
PlayResY: 1080
WrapStyle: 0
ScaledBorderAndShadow: yes

[V4+ Styles]
Format: Name,Fontname,Fontsize,PrimaryColour,SecondaryColour,OutlineColour,BackColour,Bold,Italic,Underline,StrikeOut,ScaleX,ScaleY,Spacing,Angle,BorderStyle,Outline,Shadow,Alignment,MarginL,MarginR,MarginV,Encoding
Style: Say,DejaVu Sans,40,&H00FFFFFF,&H00FFFFFF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,3,0,2,60,60,0,1

[Events]
Format: Layer,Start,End,Style,Name,MarginL,MarginR,MarginV,Effect,Text
"""

INTRO = [("TROPIC TRAIL", 96, "0xffc050", 380), ("Episode 6  ·  bm SDK", 56, "0xe0e4f0", 520),
         ("Light, fog and shadow", 34, "0x9098b0", 640)]
OUTRO = [("Next: bm Code", 56, "0xffc050", 400), ("Pico moves, jumps, spins and collects apples", 38, "0xe0e4f0", 520),
         ("github.com/f-accomando/bm", 34, "0x9098b0", 660)]

# the parts in the order of the montage: name, seconds
PART_SPEC = [("hook.mp4", 6), ("intro.mp4", 4), ("r1.mp4", 112), ("r2.mp4", 12), ("r3.mp4", 38), ("outro.mp4", 8)]


def fail(msg):
    print(msg, file=sys.stderr)
    sys.exit(1)


def run(cmd, **kw):
    try:
        call(cmd, **kw)
    except subprocess.CalledProcessError as e:
        fail("failed (exit %d): %s" % (e.returncode, " ".join(cmd[:3])))


def write_r2_ass(path):
    with open(path, "w", encoding="utf-8") as f:
        f.write(ASS_HEAD)
        for a, b, text in R2_LINES:
            f.write("Dialogue: 3,%s,%s,Say,,0,0,0,,{\\q0\\an2\\pos(960,1040)}%s\n"
                    % (ass_time(a * FPS), ass_time(b * FPS), esc(text)))


def encode_r2(raw, vf, name):
    """The raw frames of R2 scaled to 1920x1080 (nearest), with the filter chain vf, as mp4 in OUT."""
    run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24",
         "-s", "%dx%d" % (W, H), "-r", str(FPS), "-i", raw, "-t", str(R2_SECONDS), "-vf", vf,
         "-r", str(FPS), "-c:v", "libx264", "-preset", "medium", "-crf", "19", "-pix_fmt", "yuv420p", "-an",
         os.path.join(OUT, name)], cwd=OUT)


def record_r2():
    """R2: bmhost runs the cartridge by itself for 12 s, then the two encodes (with and without the lines)."""
    need = [os.path.join(OUT, "sd", "carts", "TROPIC.BM"), os.path.join(OUT, "06-sdk.bm"),
            os.path.join(OUT, "r2-input.txt")]
    if not all(os.path.isfile(p) for p in need):
        fail("run record.py first")
    if not os.path.isfile(lib3d.BMHOST):
        fail("no bmhost: %s" % lib3d.BMHOST)
    shots = os.path.join(OUT, "shots")
    os.makedirs(shots, exist_ok=True)
    raw = os.path.join(OUT, "r2.rgb")
    run(run_bmhost(lib3d.BMHOST, os.path.join(OUT, "06-sdk.bm"), os.path.join(OUT, "r2-input.txt"), R2_SECONDS,
                   os.path.join(OUT, "sd"), raw, shots=shots), cwd=ROOT)

    size = os.path.getsize(raw) if os.path.isfile(raw) else 0
    frames = size // FRAME_BYTES
    if size % FRAME_BYTES or frames < R2_SECONDS * FPS:
        fail("r2.rgb: %d bytes = %d frames, want a whole number of %dx%d rgb24 frames, at least %d"
             % (size, frames, W, H, R2_SECONDS * FPS))
    print("r2.rgb: %d frames" % frames)

    write_r2_ass(os.path.join(OUT, "r2.ass"))
    encode_r2(raw, "scale=1920:1080:flags=neighbor,ass=r2.ass", "r2.mp4")
    encode_r2(raw, "scale=1920:1080:flags=neighbor", "r2_clean.mp4")
    os.remove(raw)


def cut_cmd(src, out, seconds, ss=0):
    """A cut of a qemu recording at 60 fps, without sound (ss: where the cut starts)."""
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y"]
    if ss:
        cmd += ["-ss", str(ss)]
    return cmd + ["-i", src, "-t", str(seconds), "-r", str(FPS), "-c:v", "libx264", "-crf", "19",
                  "-pix_fmt", "yuv420p", "-an", out]


def build_parts():
    """The parts of the montage in PARTS."""
    os.makedirs(PARTS, exist_ok=True)
    qemu_r1 = os.path.join(OUT, "qemu-r1", "ep06_qemu_r1.mp4")
    qemu_r3 = os.path.join(OUT, "qemu-r3", "ep06_qemu_r3.mp4")
    for path in (qemu_r1, qemu_r3, os.path.join(OUT, "r2.mp4"), os.path.join(OUT, "r2_clean.mp4")):
        if not os.path.isfile(path):
            fail("missing input: %s" % path)

    def part(name):
        return os.path.join(PARTS, name)

    run(hook_cmd(os.path.join(OUT, "r2_clean.mp4"), part("hook.mp4"), 4.0, 6, "TODAY: LIGHT, FOG AND SHADOW"))
    run(card_cmd(part("intro.mp4"), 4, INTRO))
    run(cut_cmd(qemu_r1, part("r1.mp4"), 112))
    shutil.copy(os.path.join(OUT, "r2.mp4"), part("r2.mp4"))
    run(cut_cmd(qemu_r3, part("r3.mp4"), 38, ss=8))
    run(card_cmd(part("outro.mp4"), 8, OUTRO))


def probe(path):
    """(width, height, frame rate, duration) of the first video stream, read with ffprobe; None if unreadable."""
    res = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                          "stream=width,height,r_frame_rate:format=duration", "-of", "json", path],
                         capture_output=True, text=True)
    if res.returncode:
        return None
    try:
        d = json.loads(res.stdout)
        s = d["streams"][0]
        return int(s["width"]), int(s["height"]), s["r_frame_rate"], float(d["format"]["duration"])
    except (KeyError, IndexError, ValueError):
        return None


def verify_parts():
    for name, secs in PART_SPEC:
        path = os.path.join(PARTS, name)
        info = probe(path) if os.path.isfile(path) else None
        if info is None:
            fail("%s: missing or unreadable" % name)
        w, h, rate, dur = info
        if (w, h) != (1920, 1080) or rate != "%d/1" % FPS or abs(dur - secs) > 0.1:
            fail("%s: %dx%d %s %.2f s, want 1920x1080 %d/1 %d s" % (name, w, h, rate, dur, FPS, secs))
        print("%s: ok, %.2f s" % (name, dur))


def main():
    args = sys.argv[1:]
    os.makedirs(OUT, exist_ok=True)
    if "--skip-r2" not in args:
        record_r2()
    if "--r2-only" in args:
        return 0
    build_parts()
    verify_parts()
    parts = [os.path.join(PARTS, name) for name, _ in PART_SPEC]
    out = os.path.join(OUT, "ep06.mp4")
    run(concat_cmd(parts, out, audio=False))
    print("written: %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
