#!/usr/bin/env python3
"""
render_test.py - the bm NES cartridge in bmhost: it plays the test ROM and checks the pictures.

  python3 tests/nes/render_test.py build/host/bmhost-bin build/nes/nes.bm

Checks: the ROM list shows the file; after A the picture holds the white and green squares and the red sprite;
the sprite moves right between two pictures (the NMI and the sprite DMA work); it moves down while A is held (the pad
reaches the machine); the menu opens with L2 and R2 held.
"""
import os, shutil, struct, subprocess, sys, tempfile, zlib

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


def near(c, want, tol=10):
    return all(abs(a - b) <= tol for a, b in zip(c, want))


def has(p, want):
    return any(near(c, want) for c in set(p.values()))


def main():
    host, cart = sys.argv[1:3]
    tmp = tempfile.mkdtemp(prefix="nes-test-")
    os.makedirs(tmp + "/sd/docs"); os.makedirs(tmp + "/sd/nes"); os.makedirs(tmp + "/shots")
    subprocess.check_call([sys.executable, HERE + "/make_testrom.py", tmp + "/sd/nes/bmtest.nes"])   # the ROM folder
    script = ("5 shot browse\n10 pad 1 a\n12 pad 1 none\n60 shot f60\n200 shot f200\n"
              "210 pad 1 a\n260 pad 1 none\n270 shot held\n"
              "280 pad 1 l2 r2\n340 pad 1 none\n350 shot menu\n")
    open(tmp + "/in.txt", "w").write(script)
    subprocess.check_call([host, cart, "--sd", tmp + "/sd", "--seconds", "8", "--shots", tmp + "/shots",
                           "--input", tmp + "/in.txt", "--quiet"], stdout=subprocess.DEVNULL)
    fails = []
    def check(name, ok):
        if not ok: fails.append(name)
        print(("ok   " if ok else "FAIL ") + name)
    browse = pixels(read_png(tmp + "/shots/browse.png"))
    check("list shows a highlighted row", has(browse, (0x60, 0x30, 0x40)))
    f60 = pixels(read_png(tmp + "/shots/f60.png"))
    f200 = pixels(read_png(tmp + "/shots/f200.png"))
    held = pixels(read_png(tmp + "/shots/held.png"))
    white, green, red = (255, 254, 255), (0x5C, 0xE4, 0x30), (0xB5, 0x31, 0x20)
    check("squares white", has(f200, white))
    check("squares green", has(f200, green))
    check("red sprite", has(f200, red))
    def centre(p, axis):
        v = [xy[axis] for xy, c in p.items() if near(c, red)]
        return sum(v) / len(v) if v else -1
    check("the sprite moves right", centre(f200, 0) > centre(f60, 0) > 0)
    check("the sprite moves down while A is held", centre(held, 1) > centre(f200, 1) + 20)
    menu = pixels(read_png(tmp + "/shots/menu.png"))
    check("the menu opens", has(menu, (0xFF, 0x60, 0x60)))
    # a long list: ten times down, the highlighted row must be on the screen (the list scrolls with the selector)
    sd2 = tmp + "/sd2"
    os.makedirs(sd2 + "/nes"); os.makedirs(sd2 + "/docs"); os.makedirs(tmp + "/shots2")
    for i in range(14):
        shutil.copy(tmp + "/sd/nes/bmtest.nes", sd2 + "/nes/rom%02d.nes" % i)
    nl = chr(10)
    script2 = "5 shot top" + nl + "".join("%d pad 1 down" % (10 + 4 * k) + nl + "%d pad 1 none" % (12 + 4 * k) + nl for k in range(10)) + "80 shot low" + nl
    open(tmp + "/in2.txt", "w").write(script2)
    subprocess.check_call([host, cart, "--sd", sd2, "--seconds", "3", "--shots", tmp + "/shots2",
                           "--input", tmp + "/in2.txt", "--quiet"], stdout=subprocess.DEVNULL)
    hl = (0x60, 0x30, 0x40)
    def hl_rows(path):
        w, h, bpp, rows = read_png(path)
        return [y for y in range(h) if any(near(tuple(rows[y][x * bpp:x * bpp + 3]), hl) for x in range(0, w, 4))], h
    r_top, h_top = hl_rows(tmp + "/shots2/top.png")
    r_low, h_low = hl_rows(tmp + "/shots2/low.png")
    check("long list: the selector shows after ten downs", bool(r_low) and max(r_low) < h_low - 30)
    check("long list: the list moved", bool(r_top) and bool(r_low) and max(r_low) >= min(r_top))
    print("nes render test:", "ok" if not fails else "FAILED " + ", ".join(fails))
    if fails:
        shutil.copy(tmp + "/shots/f200.png", os.environ.get("NES_DBG", "build/nes_fail.png"))
    sys.exit(1 if fails else 0)


main()
