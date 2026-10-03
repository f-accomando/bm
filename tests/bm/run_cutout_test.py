#!/usr/bin/env python3
"""
The outline maker of the console (src/bm/cutout.c, through
build/host/test_cutout): a figure on a transparent background and the
same on a plain one become a closed cutout with the picture on the front
(bmrender's front view shows its colours) and a closed lathe; the small
specks go, the triangles stay few, the frame is 2 blocks tall.

  run_cutout_test.py build/host/test_cutout build/cutout
"""
import math
import os
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import bmmesh  # noqa: E402
import bmrender  # noqa: E402
import img2mesh  # noqa: E402
import mkbm  # noqa: E402

tool, out_dir = sys.argv[1], sys.argv[2]
os.makedirs(out_dir, exist_ok=True)


def png_rgba(w, h, px):
    raw = b"".join(b"\0" + bytes(px[y * w * 4:(y + 1) * w * 4]) for y in range(h))

    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


def figure(w, h, transparent):
    """a red disc on top of a blue stem (a lollipop), a green speck apart;
    the background transparent, or plain white"""
    px = bytearray()
    for y in range(h):
        for x in range(w):
            disc = (x - w / 2) ** 2 + (y - h * 0.3) ** 2 < (w * 0.28) ** 2
            stem = abs(x - w / 2) < w * 0.08 and h * 0.3 < y < h * 0.92
            speck = (x - w * 0.9) ** 2 + (y - h * 0.9) ** 2 < 4
            if disc:
                px += bytes((220, 40, 40, 255))
            elif stem:
                px += bytes((40, 60, 220, 255))
            elif speck:
                px += bytes((40, 200, 40, 255))
            elif transparent:
                px += bytes((0, 0, 0, 0))
            else:
                px += bytes((255, 255, 255, 255))
    return png_rgba(w, h, bytes(px))


def convert(name, data, *args):
    path = os.path.join(out_dir, name + ".png")
    open(path, "wb").write(data)
    prefix = os.path.join(out_dir, name)
    r = subprocess.run([tool, path, "--out", prefix] + list(args), capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    rec = open(prefix + ".rec", "rb").read()
    model = bmmesh.decode(struct.pack("<HHI", 1, 0, 0) + rec)[0][0]
    rgba = open(prefix + ".rgba", "rb").read()
    # a cartridge with the viewer, rendered: the front view (the first of four)
    cart = os.path.join(out_dir, name + ".bm")
    open(cart, "wb").write(mkbm.pack(img2mesh.viewer_lua(), sheet=(256, 256, rgba), title=name, author="test",
                                     mesh=struct.pack("<HHI", 1, 64, 0) + rec))
    bmrender.render(cart, os.path.join(out_dir, name + "_render.png"), size=120)
    return model, rgba, r.stdout.strip()


def closed(model):
    """every edge on two faces, the other way round"""
    edges = {}
    for a, b, c, *_ in model["faces"]:
        for p, q in ((a, b), (b, c), (c, a)):
            edges[(p, q)] = edges.get((p, q), 0) + 1
    return all(edges.get((q, p), 0) == 1 and n == 1 for (p, q), n in edges.items())


def volume(model):
    """the signed volume: its sign says which way the faces wind"""
    v = model["verts"]
    s = 0
    for a, b, c, *_ in model["faces"]:
        p, q, r = v[a], v[b], v[c]
        s += (p[0] * (q[1] * r[2] - q[2] * r[1]) - p[1] * (q[0] * r[2] - q[2] * r[0]) + p[2] * (q[0] * r[1] - q[1] * r[0])) / 6
    return s


def front_pixels(name):
    """the colours of the front view's pixels (not the background)"""
    data = open(os.path.join(out_dir, name + "_render.png"), "rb").read()
    # the PNG written by bmrender: 8-bit RGB, no filter but 0; read it back plainly
    pos, idat = 8, b""
    w = h = 0
    while pos < len(data):
        n = struct.unpack(">I", data[pos:pos + 4])[0]
        t = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        if t == b"IHDR":
            w, h = struct.unpack(">II", body[:8])
        elif t == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    out = []
    for y in range(h):
        row = raw[y * (w * 3 + 1) + 1:(y + 1) * (w * 3 + 1)]
        for x in range(120):                    # the first view only
            c = row[x * 3:x * 3 + 3]
            if c != b"\x20\x28\x30":
                out.append((x, y, c))
    return out


ref_volume = None
for name, transparent in (("lolli_alpha", True), ("lolli_white", False)):
    m, rgba, info = convert(name, figure(80, 120, transparent))
    assert closed(m), f"{name}: the cutout is closed"
    assert 20 <= len(m["faces"]) <= 400, info
    ys = [v[1] for v in m["verts"]]
    assert abs(min(ys)) < 1e-4 and abs(max(ys) - 2) < 1e-3, (min(ys), max(ys))       # 2 blocks tall, feet at 0
    zs = {round(v[2], 3) for v in m["verts"]}
    assert zs == {-0.2, 0.2}, zs                                                       # the thickness: 0.2 of 2
    xs = [v[0] for v in m["verts"]]
    assert max(xs) - min(xs) < 1.3, "the speck went (no vertices far right)"
    vol = volume(m)
    if ref_volume is None:
        ref_volume = vol
    assert vol * ref_volume > 0, "both cutouts wind the same way"
    assert 0.1 < abs(vol) < 1.5, vol
    # the sheet: the disc's red in the upper half, the stem's blue in the lower, transparent beside
    def count(y0, y1, pick):
        return sum(1 for y in range(y0, y1) for x in range(256) if pick(rgba[(y * 256 + x) * 4:(y * 256 + x) * 4 + 4]))
    assert count(0, 128, lambda c: c[0] > 200 and c[1] < 80 and c[3] == 255) > 2000, "the sheet: the red disc"
    assert count(128, 256, lambda c: c[2] > 200 and c[0] < 80 and c[3] == 255) > 500, "the sheet: the blue stem"
    assert count(0, 256, lambda c: c[3] == 0) > 10000, "the sheet: transparent around the figure"
    px = front_pixels(name)
    reds = sum(1 for _, _, c in px if c[0] > 120 and c[2] < 60)
    blues = sum(1 for _, _, c in px if c[2] > 120 and c[0] < 60)
    assert reds > 100 and blues > 20, (reds, blues)                                   # the front shows the picture
    print(f"cutout ({name}): {info}, closed, 2 tall, the picture on the front")

# the lathe: closed too, round (vertices at 12 angles), the front shows the picture
m, rgba, info = convert("lathe", figure(80, 120, True), "--lathe", "--segments", "12")
assert closed(m), "the lathe is closed"
assert volume(m) * ref_volume > 0, "the lathe winds like the cutouts"
angles = {round(math.degrees(math.atan2(v[0], -v[2])) / 30) for v in m["verts"] if abs(v[0]) + abs(v[2]) > 0.05}
assert len(angles) >= 12, angles
px = front_pixels("lathe")
reds = sum(1 for _, _, c in px if c[0] > 120 and c[2] < 60)
assert reds > 60, reds
print(f"lathe: {info}, closed, round, the picture in front")

# nothing in the picture
r = subprocess.run([tool, os.path.join(out_dir, "empty.png")], capture_output=True, text=True) if \
    open(os.path.join(out_dir, "empty.png"), "wb").write(png_rgba(8, 8, bytes(8 * 8 * 4))) else None
assert r.returncode != 0 and "all background" in r.stdout, r.stdout
print("cutout: an empty picture is refused")
