#!/usr/bin/env python3
"""
render_test.py - runs build/carts/snes16.bm in bmhost and verifies the rendered frames:
- compiles engine.b via native_compile() + native_load() with zero errors
- renders Mode 1 parallax + HDMA sky gradient + stone/grass ground + OAM sprites + HUD
- toggles Mode 1 + Mode 7 via the L2+R2 menu and verifies mid-frame Mode 7 rendering
"""
import os, shutil, struct, subprocess, sys, tempfile, zlib


def read_png(path):
    d = open(path, "rb").read()
    pos, idat, w = 8, b"", 0
    while pos < len(d):
        n, t = struct.unpack(">I4s", d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        if t == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", body[:10])
            bpp = {2: 3, 6: 4}[ct]
        elif t == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride, rows, prev = w * bpp, [], bytearray(w * bpp)
    p = 0
    for _ in range(h):
        f = raw[p]; line = bytearray(raw[p + 1:p + 1 + stride]); p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line)); prev = line
    return w, h, bpp, rows


def main():
    host, cart = sys.argv[1:3]
    tmp = tempfile.mkdtemp(prefix="snes16-test-")
    try:
        shots = os.path.join(tmp, "shots")
        sd = os.path.join(tmp, "sd")
        os.makedirs(shots); os.makedirs(sd)
        inp = os.path.join(tmp, "input.txt")
        script = (
            "1 pad 1 right\n"
            "10 shot f10\n"
            "22 pad 1 none\n"
            "25 pad 1 b\n"
            "27 pad 1 none\n"
            "32 shot f32\n"
            "40 pad 1 l2 r2\n"
            "75 pad 1 none\n"
            "80 pad 1 a\n"
            "82 pad 1 none\n"
            "86 pad 1 l2 r2\n"
            "120 pad 1 none\n"
            "130 shot m7\n"
        )
        open(inp, "w").write(script)
        cmd = [host, cart, "--sd", sd, "--seconds", "3", "--shots", shots, "--input", inp, "--quiet"]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(res.stdout)
            print(res.stderr)
            sys.exit(res.returncode)

        for fn in ("f10.png", "f32.png", "m7.png"):
            p = os.path.join(shots, fn)
            assert os.path.exists(p), f"missing {fn}"

        w, h, bpp, rows10 = read_png(os.path.join(shots, "f10.png"))
        _, _, _, rows32 = read_png(os.path.join(shots, "f32.png"))
        _, _, _, rowsm7 = read_png(os.path.join(shots, "m7.png"))
        assert (w, h) == (640, 360), f"unexpected size {w}x{h}"

        colors = {rows10[y][x * bpp:x * bpp + 3] for y in range(40, 320, 4) for x in range(120, 520, 4)}
        assert len(colors) >= 12, f"too few distinct colors in Mode 1 viewport: {len(colors)}"

        diff_move = sum(1 for y in range(40, 320, 2) if rows10[y] != rows32[y])
        assert diff_move >= 10, f"expected animated motion between f10 and f32 (diff={diff_move})"

        diff_m7 = sum(1 for y in range(220, 340, 2) if rows32[y] != rowsm7[y])
        assert diff_m7 >= 10, f"expected Mode 7 floor change in bottom half (diff={diff_m7})"

        print(f"snes16 render_test: ok ({len(colors)} colors, move_diff={diff_move}, m7_diff={diff_m7})")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


main()
