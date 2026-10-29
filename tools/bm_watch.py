#!/usr/bin/env python3
"""The screen of a console in the terminal, a frame every second or so (op F of the transfer port).

  python3 tools/bm_watch.py 192.168.1.108                 # a frame a second, Ctrl-C stops
  python3 tools/bm_watch.py 192.168.1.108 --fps 0.5       # slower
  python3 tools/bm_watch.py 192.168.1.108 --once          # one frame
  python3 tools/bm_watch.py 192.168.1.108 --ppm shot.ppm  # one frame saved as a PPM, no drawing

The password is the console's (BM_PASSWORD, or -p, or asked). The bridge must be on (Settings > System > Bridge: on or
fast). The picture is the page on the screen now: the menu, the monitor or a game. Nothing is written on the SD card.
It needs a terminal with 24-bit colour (Windows Terminal, iTerm2, most Linux terminals). One frame is 460 KB at 640x360
(RGB565), so over WiFi a second or two for each.
"""
import argparse
import getpass
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bm_net  # noqa: E402


fetch_frame, pixel = bm_net.fetch_frame, bm_net.frame_pixel


def draw(frame, cols, rows):
    """the frame in cols x rows terminal cells: each cell is two pixels one above the other (the half block)"""
    w, h = frame[0], frame[1]
    lines = []
    for cy in range(rows):
        out = []
        for cx in range(cols):
            x = min(w - 1, (cx * w + w // 2) // cols)
            ya = min(h - 1, ((2 * cy) * h + h // 2) // (2 * rows))
            yb = min(h - 1, ((2 * cy + 1) * h + h // 2) // (2 * rows))
            ra, ga, ba = pixel(frame, x, ya)
            rb, gb, bb = pixel(frame, x, yb)
            out.append(f"\x1b[38;2;{ra};{ga};{ba};48;2;{rb};{gb};{bb}m▀")
        lines.append("".join(out) + "\x1b[0m")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host")
    ap.add_argument("-p", "--password", default=os.environ.get("BM_PASSWORD"))
    ap.add_argument("--fps", type=float, default=1.0, help="frames a second (default 1; the link sets the limit)")
    ap.add_argument("--once", action="store_true", help="one frame, then stop")
    ap.add_argument("--ppm", metavar="OUT", help="save one frame as a PPM file instead of drawing it")
    ap.add_argument("--cols", type=int, help="width in terminal cells (default: the terminal's)")
    a = ap.parse_args()
    pw = a.password or getpass.getpass("password: ")
    if a.ppm:
        return bm_net.frame_file(argparse.Namespace(host=a.host), a.ppm, pw)
    size = shutil.get_terminal_size((100, 40))
    first = True
    period = 1.0 / a.fps if a.fps > 0 else 1.0
    try:
        while True:
            t0 = time.time()
            try:
                frame = fetch_frame(a.host, pw)
            except (RuntimeError, OSError) as e:
                print(f"\x1b[91m{e}\x1b[0m")
                if a.once:
                    return 1
                time.sleep(2)
                continue
            size = shutil.get_terminal_size((100, 40))
            cols = max(20, min(a.cols or size.columns, size.columns))
            rows = max(5, min(size.lines - 2, int(cols * frame[1] / frame[0] / 2 + 0.5)))
            text = draw(frame, cols, rows)
            sys.stdout.write(("\x1b[2J" if first else "") + "\x1b[H" + text + "\n")
            sys.stdout.write(f"\x1b[0m{frame[0]}x{frame[1]} {time.time() - t0:.1f} s  Ctrl-C stops\x1b[K")
            sys.stdout.flush()
            first = False
            if a.once:
                print()
                return 0
            time.sleep(max(0.0, period - (time.time() - t0)))
    except KeyboardInterrupt:
        print("\x1b[0m")
        return 0


if __name__ == "__main__":
    sys.exit(main())
