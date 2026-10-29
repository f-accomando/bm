#!/usr/bin/env python3
"""The screen of a console in the terminal, live (op V of the transfer port).

  python3 tools/bm_watch.py 192.168.1.108                 # as big as the terminal, up to 10 frames a second
  python3 tools/bm_watch.py 192.168.1.108 --fps 5         # fewer frames, less traffic
  python3 tools/bm_watch.py 192.168.1.108 --cols 80       # a smaller picture (less traffic still)
  python3 tools/bm_watch.py 192.168.1.108 --mode sixel    # real pixels (640x360), the text of the menu is legible
  python3 tools/bm_watch.py 192.168.1.108 --mode sixel --scale 2   # the same, twice as big on the screen
  python3 tools/bm_watch.py 192.168.1.108 --once          # one frame, then stop
  python3 tools/bm_watch.py 192.168.1.108 --ppm shot.ppm  # one full frame (op F) saved as a PPM, no drawing

The password is the console's (BM_PASSWORD, or -p, or asked). The bridge must be on (Settings > System > Bridge: on or
fast). The picture is the page on the screen now: the menu, the monitor or a game. Nothing is written on the SD card.
It needs a terminal with 24-bit colour (Windows Terminal, iTerm2, most Linux terminals).

How it is light for a Pi Zero W: the console averages the screen down to the size of your terminal (one pixel for each
half cell, so 120 columns are 120x68 pixels, about 16 KB), sends only the pixels that changed since the last frame
(skips, runs of one colour, literals: see netxfer.c) and nothing at all when the screen does not change. This tool draws
only the cells that changed. Ctrl-C stops. If the terminal changes size the stream starts again for the new size.
"""
import argparse
import getpass
import os
import shutil
import socket
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bm_net  # noqa: E402


def recv_all(sock, n):
    """exactly n bytes, or b"" when the console closed"""
    data = b""
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            return b""
        data += chunk
    return data


def decode(pixels, w, h, payload):
    """the tokens of a frame applied to pixels (a list of w*h RGB565 values, changed in place)"""
    n, i, pos = w * h, 0, 0
    while i < len(payload):
        if i + 2 > len(payload):
            raise ValueError("a cut token")
        tok = payload[i] | payload[i + 1] << 8
        kind, cnt = tok >> 14, tok & 0x3FFF
        i += 2
        if kind == 0:
            pos += cnt
        elif kind == 1:
            if i + 2 * cnt > len(payload) or pos + cnt > n:
                raise ValueError("literal pixels out of the frame")
            for k in range(cnt):
                pixels[pos + k] = payload[i + 2 * k] | payload[i + 2 * k + 1] << 8
            i += 2 * cnt
            pos += cnt
        elif kind == 2:
            if i + 2 > len(payload) or pos + cnt > n:
                raise ValueError("a run out of the frame")
            v = payload[i] | payload[i + 1] << 8
            i += 2
            pixels[pos:pos + cnt] = [v] * cnt
            pos += cnt
        else:
            raise ValueError("an unknown token")


def stream_frames(host, password, w, h, ms):
    """connect (op V) and yield (pixels, changed): pixels the picture so far (a list of w*h RGB565 values, the same list
    every time), changed False for a "nothing changed" frame. Ends when the console closes."""
    sock, a = bm_net.open_request(argparse.Namespace(host=host), b"V", f"{w}x{h}@{ms}", b"", password)
    if a != b"OK":
        sock.close()
        raise RuntimeError("refused: " + bm_net.ANSWERS.get(a, (a or b"no answer").decode(errors="replace")) +
                           (" (a kernel from before op V answers BH)" if a == b"BH" else ""))
    sock.settimeout(8)
    pixels = [0] * (w * h)
    try:
        while True:
            head = recv_all(sock, 8)
            if not head:
                return
            fw, fh, used = struct.unpack("<HHI", head)
            if (fw, fh) != (w, h):
                raise RuntimeError(f"the console sent {fw}x{fh}, not {w}x{h}")
            payload = recv_all(sock, used) if used else b""
            if used and not payload:
                return
            if used:
                decode(pixels, w, h, payload)
            yield pixels, used > 0, 8 + used
    except socket.timeout:
        raise RuntimeError("the console stopped answering")
    finally:
        sock.close()


def rgb(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2


def render(pixels, prev, w, h):
    """the cells whose two pixels changed since prev (None: all), as terminal text; cell (x, y) is at row y + 1, column
    x + 1 and shows the pixel 2y above the pixel 2y + 1 (the half block: ink on top, paper below)"""
    out, here, last = [], None, None
    for cy in range(h // 2):
        ia, ib = 2 * cy * w, (2 * cy + 1) * w
        for x in range(w):
            a, b = pixels[ia + x], pixels[ib + x]
            if prev is not None and prev[ia + x] == a and prev[ib + x] == b:
                continue
            if here != (cy, x):
                out.append(f"\x1b[{cy + 1};{x + 1}H")
            if last != (a, b):
                ra, ga, ba = rgb(a)
                rb, gb, bb = rgb(b)
                out.append(f"\x1b[38;2;{ra};{ga};{ba};48;2;{rb};{gb};{bb}m")
                last = (a, b)
            out.append("▀")
            here = (cy, x + 1)
    return "".join(out)


def sixel_index(v):
    """the entry of a fixed palette of 256 colours (3 bits red, 3 green, 2 blue) for an RGB565 pixel"""
    return ((v >> 13) & 7) << 5 | ((v >> 8) & 7) << 2 | ((v >> 3) & 3)


def sixel_color(i):
    return f"#{i};2;{round((i >> 5) * 100 / 7)};{round(((i >> 2) & 7) * 100 / 7)};{round((i & 3) * 100 / 3)}"


def render_sixel(pixels, prev, w, h, scale):
    """the picture as one Sixel image (real pixels, scale times bigger) at the top left of the terminal. A Sixel image
    is made of bands of 6 pixel rows: the bands whose pixels did not change since prev are left empty (the image is
    drawn with a transparent background: P2 = 1), so only what moved is sent to the terminal and decoded by it."""
    big_w, big_h = w * scale, h * scale
    rows = {}

    def row(sy):
        r = rows.get(sy)
        if r is None:
            r = rows[sy] = [sixel_index(v) for v in pixels[sy * w:(sy + 1) * w]]
        return r

    bands, used = [], set()
    for y0 in range(0, big_h, 6):
        lo, hi = y0 // scale, (min(y0 + 5, big_h - 1)) // scale
        if prev is not None and pixels[lo * w:(hi + 1) * w] == prev[lo * w:(hi + 1) * w]:
            bands.append("")
            continue
        per = {}
        for k in range(6):
            if y0 + k >= big_h:
                break
            r = row((y0 + k) // scale)
            for sx in range(w):
                bits = per.get(r[sx])
                if bits is None:
                    bits = per[r[sx]] = [0] * w
                bits[sx] |= 1 << k
        parts = []
        for c, bits in per.items():
            used.add(c)
            seg, sx = [f"#{c}"], 0
            while sx < w:
                b, n = bits[sx], 1
                while sx + n < w and bits[sx + n] == b:
                    n += 1
                sx += n
                n *= scale
                seg.append(f"!{n}{chr(63 + b)}" if n > 3 else chr(63 + b) * n)
            parts.append("".join(seg))
        bands.append("$".join(parts))
    head = f"\x1b[1;1H\x1bP0;1;0q\"1;1;{big_w};{big_h}" + "".join(sixel_color(i) for i in sorted(used))
    return head + "-".join(bands) + "\x1b\\"


def pick_size_sixel(cols_arg):
    """the picture asked of the console in Sixel mode: its own 640x360 unless --cols says less (16:9, even height)"""
    w = max(64, min(cols_arg or 640, 640))
    return w, max(4, w * 9 // 16 // 2 * 2)


def pick_size(cols_arg, fps_limit=None):
    """the picture for this terminal: width in cells, height in pixels (two for each row), 16:9"""
    size = shutil.get_terminal_size((100, 40))
    cols = min(cols_arg or size.columns, size.columns, 320)
    rows = min(size.lines - 2, 90)
    cols = max(16, min(cols, int(rows * 2 * 16 / 9)))
    h = max(4, int(cols * 9 / 16) // 2 * 2)
    return cols, h


def watch(a, pw):
    out = sys.stdout
    out.write("\x1b[?1049h\x1b[?25l\x1b[2J")
    try:
        while True:
            sixel = a.mode == "sixel"
            w, h = pick_size_sixel(a.cols) if sixel else pick_size(a.cols)
            size = shutil.get_terminal_size((100, 40))
            ms = int(1000 / a.fps) if a.fps > 0 else (250 if sixel else 100)
            prev, frames, bytes_in, t0 = None, 0, 0, time.time()
            fps_text = ""
            out.write("\x1b[0m\x1b[2J")
            for pixels, changed, nbytes in stream_frames(a.host, pw, w, h, ms):
                bytes_in += nbytes
                if changed or prev is None:
                    out.write(render_sixel(pixels, prev, w, h, a.scale) if sixel else render(pixels, prev, w, h))
                    prev = list(pixels)
                    frames += 1
                now = time.time()
                if now - t0 >= 1.0:
                    fps_text = f"{w}x{h}  {frames / (now - t0):.1f} frames/s  {bytes_in / 1024 / (now - t0):.1f} KB/s  Ctrl-C stops"
                    frames, bytes_in, t0 = 0, 0, now
                    out.write(f"\x1b[0m\x1b[{size.lines if sixel else h // 2 + 1};1H{fps_text}\x1b[K")
                out.flush()
                if a.once:
                    return 0
                if shutil.get_terminal_size((100, 40)) != size:
                    break                       # the terminal changed size: a new picture for it
            else:
                return 0                        # the console closed the stream
    except KeyboardInterrupt:
        return 0
    except (RuntimeError, OSError, ValueError) as e:
        out.write(f"\x1b[0m\x1b[?1049l\x1b[?25h{e}\n")
        return 1
    finally:
        out.write("\x1b[0m\x1b[?25h\x1b[?1049l")
        out.flush()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host")
    ap.add_argument("-p", "--password", default=os.environ.get("BM_PASSWORD"))
    ap.add_argument("--fps", type=float, default=10.0, help="the most frames a second (default 10; the link may give fewer)")
    ap.add_argument("--once", action="store_true", help="one frame, then stop")
    ap.add_argument("--ppm", metavar="OUT", help="save one full frame (op F) as a PPM file instead of drawing")
    ap.add_argument("--cols", type=int, help="width in terminal cells (default: the terminal's); in sixel mode the width in pixels (default 640)")
    ap.add_argument("--mode", choices=("blocks", "sixel"), default="blocks",
                    help="blocks: half-block cells, legible only for big shapes; sixel: real pixels, 640x360 (a terminal "
                         "with Sixel: Windows Terminal 1.22 or later, WezTerm, foot, mlterm)")
    ap.add_argument("--scale", type=int, choices=(1, 2, 3), default=1,
                    help="sixel mode: how many times bigger each pixel is drawn (1: 640x360 on the screen)")
    a = ap.parse_args()
    pw = a.password or getpass.getpass("password: ")
    if a.ppm:
        return bm_net.frame_file(argparse.Namespace(host=a.host), a.ppm, pw)
    return watch(a, pw)


if __name__ == "__main__":
    sys.exit(main())
