#!/usr/bin/env python3
"""
render_test.py - the bm SNES cartridge in bmhost: it plays the test ROM and checks the pictures.

  python3 tests/snes/render_test.py build/host/bmhost-bin build/snes/snes.bm

Checks: the ROM list shows the file; after A the picture holds the checkerboard colours, the red
border and the orange sprite; the sprite moves to the right between two frames (NMI and OAM work);
the right border is a staircase (HDMA changes the scroll in bands).
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


def near(c, want, tol=4):
    return all(abs(a - b) <= tol for a, b in zip(c, want))


def has(p, want):
    return any(near(c, want) for c in set(p.values()))


def main():
    host, cart = sys.argv[1:3]
    tmp = tempfile.mkdtemp(prefix="snes-test-")
    os.makedirs(tmp + "/sd/docs"); os.makedirs(tmp + "/sd/sfc"); os.makedirs(tmp + "/shots")
    subprocess.check_call([sys.executable, HERE + "/make_testrom.py", tmp + "/sd/sfc/bmtest.sfc"],   # the ROM folder
                          stdout=subprocess.DEVNULL)
    script = ("5 shot browse\n10 pad 1 a\n12 pad 1 none\n60 shot f60\n200 shot f200\n"
              "210 pad 1 l2 r2\n260 pad 1 none\n330 shot menu\n")
    open(tmp + "/in.txt", "w").write(script)
    subprocess.check_call([host, cart, "--sd", tmp + "/sd", "--seconds", "6", "--shots", tmp + "/shots",
                           "--input", tmp + "/in.txt", "--quiet"], stdout=subprocess.DEVNULL)
    fails = []
    def check(name, ok):
        if not ok: fails.append(name)
        print(("ok   " if ok else "FAIL ") + name)
    browse = pixels(read_png(tmp + "/shots/browse.png"))
    check("list shows a highlighted row", has(browse, (0x30, 0x48, 0x60)))
    f60 = pixels(read_png(tmp + "/shots/f60.png"))
    f200 = pixels(read_png(tmp + "/shots/f200.png"))
    white, green, red, orange = (231, 231, 231), (48, 165, 66), (247, 49, 49), (255, 198, 0)
    check("checkerboard white", has(f200, white))
    check("checkerboard green", has(f200, green))
    check("red border", has(f200, red))
    check("orange sprite", has(f200, orange))
    def cx(p):
        xs = [x for (x, y), c in p.items() if near(c, orange)]
        return sum(xs) / len(xs) if xs else -1
    check("the sprite moves right", cx(f200) > cx(f60) > 0)
    # HDMA bands: the left edge of the red band on the right moves by whole steps as the rows go down
    h, w = 360, 640
    def red_left(p, y):
        for x in range(w // 2, w):
            if near(p.get((x, y), (0, 0, 0)), red): return x
        return None
    a, b = red_left(f200, 100), red_left(f200, 300)
    check("HDMA bands change the scroll", a is not None and b is not None and a != b)
    print("snes render test:", "ok" if not fails else "FAILED " + ", ".join(fails))
    sys.exit(1 if fails else 0)


main()
