#!/usr/bin/env python3
"""Size spikes for bm video (VID-S3, S4 and the container estimate). PC only.

Needs ffmpeg in PATH. Reads a clip, takes SECONDS of it at FPS, scales it to
WxH and prints the bytes per frame of:
  mjpeg  baseline JPEG at some qualities (the size the player reads),
  rle    RLE + delta on a 256-colour palette (lossless after the palette).
Usage: spike_sizes.py CLIP [--w 320 --h 180 --fps 15 --seconds 10 --start 5]
"""
import argparse, glob, os, statistics, subprocess, tempfile


def ff(*a):
    subprocess.run(["ffmpeg", "-v", "error", "-y", *a], check=True)


def mjpeg(clip, a, q):
    with tempfile.TemporaryDirectory() as d:
        ff("-ss", str(a.start), "-t", str(a.seconds), "-i", clip,
           "-vf", f"fps={a.fps},scale={a.w}:{a.h}:flags=lanczos", "-pix_fmt", "yuvj420p",
           "-c:v", "mjpeg", "-q:v", str(q), os.path.join(d, "f%04d.jpg"))
        return [os.path.getsize(p) for p in sorted(glob.glob(os.path.join(d, "*.jpg")))]


def rle_frame(cur, prev):
    """One frame: skip (0..127) = n+1 same pixels; 0x80|n repeat n+1 of next byte (1..64 via n<64);
    0xC0|n literal n+1 bytes. Returns the byte count."""
    n = len(cur)
    i = out = 0
    while i < n:
        if cur[i] == prev[i]:
            j = i
            while j < n and cur[j] == prev[j] and j - i < 128:
                j += 1
            out += 1
            i = j
            continue
        j = i
        while j < n and cur[j] == cur[i] and j - i < 64:
            j += 1
        if j - i >= 3:
            out += 2
            i = j
            continue
        j = i
        while j < n and j - i < 64:
            if cur[j] == prev[j] and j + 2 < n and cur[j + 1] == prev[j + 1] and cur[j + 2] == prev[j + 2]:
                break
            if j + 3 < n and cur[j] == cur[j + 1] == cur[j + 2] == cur[j + 3]:
                break
            j += 1
        if j == i:
            j = i + 1
        out += 1 + (j - i)
        i = j
    return out


def rle(clip, a, key=30):
    with tempfile.TemporaryDirectory() as d:
        raw = os.path.join(d, "p.raw")
        ff("-ss", str(a.start), "-t", str(a.seconds), "-i", clip,
           "-filter_complex",
           f"fps={a.fps},scale={a.w}:{a.h}:flags=lanczos,split[x][y];[x]palettegen=stats_mode=full[p];[y][p]paletteuse=dither=none",
           "-pix_fmt", "pal8", "-f", "rawvideo", raw)
        data = open(raw, "rb").read()
    fs = a.w * a.h
    sizes, prev = [], bytes(fs)
    for k in range(len(data) // fs):
        cur = data[k * fs:(k + 1) * fs]
        sizes.append(rle_frame(cur, bytes(fs) if k % key == 0 else prev))
        prev = cur
    return sizes


def row(name, s, raw):
    s = sorted(s)
    print(f"| {name} | {s[0]} | {int(statistics.median(s))} | {s[-1]} | {int(statistics.mean(s))} | {raw / statistics.mean(s):.1f}:1 |")


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("clip")
    p.add_argument("--w", type=int, default=320)
    p.add_argument("--h", type=int, default=180)
    p.add_argument("--fps", type=int, default=15)
    p.add_argument("--seconds", type=int, default=10)
    p.add_argument("--start", type=int, default=5)
    a = p.parse_args()
    raw16 = a.w * a.h * 2
    print(f"clip {os.path.basename(a.clip)} {a.w}x{a.h} {a.fps} fps {a.seconds} s; raw RGB565 frame {raw16} bytes")
    print("| codec | min | median | max | mean | ratio vs RGB565 |\n|---|---|---|---|---|---|")
    for q in (3, 6, 10, 15):
        row(f"mjpeg q{q}", mjpeg(a.clip, a, q), raw16)
    row("rle+delta pal8", rle(a.clip, a), raw16)
