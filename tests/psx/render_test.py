#!/usr/bin/env python3
"""
render_test.py - the bm PSX cartridge in bmhost: it plays the test PS-EXE (no BIOS) and checks the picture.

  python3 tests/psx/render_test.py build/host/bmhost-bin build/bm/psx.bm

Checks: the game list shows the file; after A the picture holds the blue background, the Gouraud triangle (red,
green and blue corners with mixed colours between), and the white square.
"""
import os, struct, subprocess, sys, tempfile, zlib

HERE = os.path.dirname(os.path.abspath(__file__))


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


def pixels(png):
    w, h, bpp, rows = png
    return {(x, y): tuple(rows[y][x * bpp:x * bpp + 3]) for y in range(h) for x in range(w)}


def near(c, want, tol=12):
    return all(abs(a - b) <= tol for a, b in zip(c, want))


def count(p, want, tol=12):
    return sum(1 for c in p.values() if near(c, want, tol))


def main():
    host, cart = sys.argv[1:3]
    tmp = tempfile.mkdtemp(prefix="psx-test-")
    os.makedirs(tmp + "/sd/docs"); os.makedirs(tmp + "/sd/psx"); os.makedirs(tmp + "/shots")
    subprocess.check_call([sys.executable, HERE + "/make_testexe.py", tmp + "/sd/psx/bmtest.exe"])
    script = "5 shot browse\n10 pad 1 a\n12 pad 1 none\n90 shot run\n"
    open(tmp + "/in.txt", "w").write(script)
    subprocess.check_call([host, cart, "--sd", tmp + "/sd", "--seconds", "6", "--shots", tmp + "/shots",
                           "--input", tmp + "/in.txt", "--quiet"], stdout=subprocess.DEVNULL)
    fails = []
    def check(name, ok):
        if not ok: fails.append(name)
        print(("ok   " if ok else "FAIL ") + name)
    browse = pixels(read_png(tmp + "/shots/browse.png"))
    check("list shows a highlighted row", count(browse, (0x30, 0x48, 0x60), 4) > 100)
    run = pixels(read_png(tmp + "/shots/run.png"))
    check("blue background", count(run, (0x10, 0x20, 0x40)) > 20000)
    check("white square", count(run, (255, 255, 255), 8) > 100)
    check("red corner", count(run, (255, 0, 0), 60) > 50)
    check("green corner", count(run, (0, 255, 0), 60) > 50)
    check("blue corner", count(run, (0, 0, 255), 60) > 50)
    # between the red and the blue corner the colour is a mix: red and blue both high enough
    mix = [c for c in run.values() if c[0] > 80 and c[2] > 80 and c[1] < 60]
    check("gouraud mix", len(mix) > 50)
    print("psx render test:", "ok" if not fails else "FAILED " + ", ".join(fails))
    sys.exit(1 if fails else 0)


main()
