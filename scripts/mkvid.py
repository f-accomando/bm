#!/usr/bin/env python3
"""Packs a clip into a .bmv file (docs/BM-VIDEO.md). Stdlib; ffmpeg only for --clip.

  mkvid.py -o out.bmv --clip movie.mp4 [--size 320x180] [--fps 15] [--quality 6]
                      [--seconds N] [--start S] [--rate 22050] [--bits 8|16] [--no-audio]
  mkvid.py -o out.bmv --jpegs DIR --fps 15 [--pcm file.raw --rate 22050 --channels 1 --bits 16]

--clip runs ffmpeg for baseline JPEG frames (-c:v mjpeg) and mono PCM audio.
--jpegs takes the *.jpg files of DIR in name order (baseline only).
"""
import argparse
import glob
import os
import struct
import subprocess
import sys
import tempfile

MAGIC = b"BMV1"


def jpeg_size(data):
    """(width, height, progressive) of a JPEG, or raises ValueError."""
    if data[:2] != b"\xff\xd8":
        raise ValueError("not a JPEG")
    i = 2
    while i + 4 <= len(data):
        if data[i] != 0xFF:
            i += 1
            continue
        m = data[i + 1]
        if m in (0xFF, 0x01) or 0xD0 <= m <= 0xD7:
            i += 1 if m == 0xFF else 2
            continue
        n = struct.unpack(">H", data[i + 2:i + 4])[0]
        if m in (0xC0, 0xC1, 0xC2):
            h, w = struct.unpack(">HH", data[i + 5:i + 9])
            return w, h, m == 0xC2
        i += 2 + n
    raise ValueError("no frame header")


def pack(frames, fps, pcm=b"", rate=0, channels=0, bits=0):
    """The bytes of a .bmv: header, frames, index, audio."""
    if not frames:
        raise ValueError("no frames")
    w, h, _ = jpeg_size(frames[0])
    for k, f in enumerate(frames):
        fw, fh, prog = jpeg_size(f)
        if prog:
            raise ValueError(f"frame {k} is progressive: the console reads baseline JPEG only")
        if (fw, fh) != (w, h):
            raise ValueError(f"frame {k} is {fw}x{fh}, the first is {w}x{h}")
    if not 1 <= fps <= 240:
        raise ValueError("fps must be 1..240")
    if pcm:
        if channels not in (1, 2) or bits not in (8, 16) or not 4000 <= rate <= 48000:
            raise ValueError("audio: channels 1|2, bits 8|16, rate 4000..48000")
        if len(pcm) % (channels * bits // 8):
            raise ValueError("audio is not a whole number of samples")
    else:
        rate = channels = bits = 0
    offs, pos = [], 32
    for f in frames:
        offs.append((pos, len(f)))
        pos += len(f)
    index_off = pos
    index = b"".join(struct.pack("<II", o, n) for o, n in offs)
    audio_off = index_off + len(index)
    head = MAGIC + struct.pack("<BBBBHHHHIIII", 1, 1, channels, bits, w, h, fps, rate, len(frames),
                               index_off, audio_off if pcm else 0, len(pcm))
    assert len(head) == 32
    return head + b"".join(frames) + index + pcm


def from_clip(a):
    with tempfile.TemporaryDirectory() as d:
        w, h = (int(x) for x in a.size.split("x"))
        cut = ["-ss", str(a.start)] + (["-t", str(a.seconds)] if a.seconds else [])

        def run(*x):
            return subprocess.run(["ffmpeg", "-v", "error", "-y", *x])

        r = run(*cut, "-i", a.clip, "-vf", f"fps={a.fps},scale={w}:{h}:flags=lanczos", "-pix_fmt", "yuvj420p",
                "-c:v", "mjpeg", "-q:v", str(a.quality), os.path.join(d, "f%05d.jpg"))
        if r.returncode:
            sys.exit("mkvid: ffmpeg failed")
        frames = [open(p, "rb").read() for p in sorted(glob.glob(os.path.join(d, "f*.jpg")))]
        pcm = b""
        if not a.no_audio:
            fmt = "s16le" if a.bits == 16 else "u8"
            pa = os.path.join(d, "a.raw")
            r = run(*cut, "-i", a.clip, "-vn", "-ac", "1", "-ar", str(a.rate), "-f", fmt, pa)
            if r.returncode == 0 and os.path.exists(pa):
                pcm = open(pa, "rb").read()
        return frames, pcm


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("-o", "--out", required=True)
    p.add_argument("--clip")
    p.add_argument("--jpegs")
    p.add_argument("--size", default="320x180")
    p.add_argument("--fps", type=int, default=15)
    p.add_argument("--quality", type=int, default=6, help="ffmpeg -q:v, 2 (best) .. 31")
    p.add_argument("--seconds", type=float, default=0)
    p.add_argument("--start", type=float, default=0)
    p.add_argument("--rate", type=int, default=22050)
    p.add_argument("--bits", type=int, default=16, choices=(8, 16))
    p.add_argument("--channels", type=int, default=1, choices=(1, 2))
    p.add_argument("--pcm")
    p.add_argument("--no-audio", action="store_true")
    a = p.parse_args()
    if bool(a.clip) == bool(a.jpegs):
        p.error("give --clip or --jpegs")
    if a.clip:
        frames, pcm = from_clip(a)
        ch = 1
    else:
        frames = [open(f, "rb").read() for f in sorted(glob.glob(os.path.join(a.jpegs, "*.jpg")))]
        pcm = open(a.pcm, "rb").read() if a.pcm else b""
        ch = a.channels
    try:
        data = pack(frames, a.fps, pcm, a.rate, ch, a.bits)
    except ValueError as e:
        sys.exit(f"mkvid: {e}")
    open(a.out, "wb").write(data)
    print(f"{a.out}: {len(frames)} frames, {len(data)} bytes, audio {len(pcm)} bytes")


if __name__ == "__main__":
    main()
