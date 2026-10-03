#!/usr/bin/env python3
"""
The reels of Overbit as videos: bmhost plays the reel mode of the game
(frames and sound), ffmpeg encodes them; the log of the reel says where each
shot starts, so a GIF can gather the shots chosen.

  reel.py BUILD REEL.bm SECONDS OUT.mp4 [--gif OUT.gif] [--gif-shots WORD,...]
          [--gif-from S --gif-len S] [--size WxH] [--crf N]

--gif-shots: the shots whose subtitle or caption contains one of the words
(e.g. ULTIMATE), each in full; or --gif-from/--gif-len: one stretch.
"""
import argparse
import os
import re
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("reel")
    ap.add_argument("seconds", type=float)
    ap.add_argument("mp4")
    ap.add_argument("--gif")
    ap.add_argument("--gif-shots")
    ap.add_argument("--gif-from", type=float, default=0)
    ap.add_argument("--gif-len", type=float, default=32)
    ap.add_argument("--size", default="960x540")
    ap.add_argument("--crf", type=int, default=28)
    a = ap.parse_args()
    rgb = os.path.join(a.build, "overbit", "reel.rgb")
    wav = os.path.join(a.build, "overbit", "reel.wav")
    r = subprocess.run([os.path.join(a.build, "host", "bmhost-bin"), a.reel, "--seconds", str(a.seconds), "--video", rgb,
                        "--wav", wav], capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-2000:])
        return 1
    # the shots: (start, caption)
    shots = [(float(m.group(3)), m.group(2) + " " + m.group(4))
             for m in re.finditer(r"^reel (\d+) (.*?) @([0-9.]+) ?(.*)$", r.stdout + r.stderr, re.M)]
    w, h = a.size.split("x")
    raw = ["-f", "rawvideo", "-pixel_format", "rgb24", "-video_size", "320x180", "-framerate", "60", "-i", rgb]
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error"] + raw + ["-i", wav, "-vf", f"scale={w}:{h}:flags=neighbor",
                    "-c:v", "libx264", "-preset", "slow", "-crf", str(a.crf), "-pix_fmt", "yuv420p", "-c:a", "aac",
                    "-b:a", "96k", "-shortest", a.mp4], check=True)
    if a.gif:
        pal = "fps=15,scale=480:270:flags=neighbor,split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=none"
        if a.gif_shots:
            words = a.gif_shots.split(",")
            # the shots chosen: from their start to the next shot's
            spans = []
            for i, (t0, cap) in enumerate(shots):
                if any(wd in cap for wd in words):
                    t1 = shots[i + 1][0] if i + 1 < len(shots) else a.seconds
                    spans.append((t0, t1))
            sel = "+".join(f"between(t,{t0:.2f},{t1:.2f})" for t0, t1 in spans)
            vf = f"select='{sel}',setpts=N/FRAME_RATE/TB," + pal
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error"] + raw + ["-vf", vf, a.gif], check=True)
        else:
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error"] + raw + ["-ss", str(a.gif_from), "-t", str(a.gif_len),
                            "-vf", pal, a.gif], check=True)
    os.remove(rgb)
    print(f"{a.mp4}: {len(shots)} shots, {os.path.getsize(a.mp4) // 1024} KiB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
