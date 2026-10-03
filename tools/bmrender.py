#!/usr/bin/env python3
"""
bmrender: the 3D models of a .bm drawn on the PC, as the console draws them
(flat colours, textured faces from the sprite sheet, the faces that show
clockwise), one row of four views per model: front, three quarters, side,
back. A software rasterizer in plain Python, for a look at what
tools/meshy2mesh.py and tools/img2mesh.py made without the console.

  tools/bmrender.py CART.bm OUT.png [--size 300] [--model NAME]
"""
import argparse
import math
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "scripts"))
import bmmesh  # noqa: E402
import mkbm  # noqa: E402


def sheet_of(secs):
    """(w, h, rgba) of the cartridge's sprite sheet, or None"""
    if mkbm.SEC_SHEET8 in secs:
        return mkbm.sheet8_decode(secs[mkbm.SEC_SHEET8])
    if mkbm.SEC_SHEET in secs:
        w, h = struct.unpack_from("<HH", secs[mkbm.SEC_SHEET], 0)
        return w, h, secs[mkbm.SEC_SHEET][4:]
    return None


def draw_model(px, zbuf, W, m, sheet, x0, y0, size, yaw):
    verts = m["verts"]
    lo = [min(v[k] for v in verts) for k in range(3)]
    hi = [max(v[k] for v in verts) for k in range(3)]
    cx, cy, cz = [(lo[k] + hi[k]) / 2 for k in range(3)]
    extent = max(hi[k] - lo[k] for k in range(3))
    dist = extent * 2.2 + 0.5
    pitch = -0.45
    cyaw, syaw, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    f = size * 1.1
    lx, ly, lz = -0.5, 0.8, -0.6
    ll = math.sqrt(lx * lx + ly * ly + lz * lz)
    lx, ly, lz = lx / ll, ly / ll, lz / ll
    lrx, lrz = lx * cyaw - lz * syaw, lx * syaw + lz * cyaw
    lry, lrz2 = ly * cp - lrz * sp, ly * sp + lrz * cp
    placed = []
    for v in verts:
        wx, wy, wz = v[0] - cx, v[1] - cy, v[2] - cz
        rx, rz = wx * cyaw - wz * syaw, wx * syaw + wz * cyaw
        ry, rz2 = wy * cp - rz * sp, wy * sp + rz * cp + dist
        placed.append((rx, ry, rz2, x0 + size / 2 + rx / rz2 * f, y0 + size / 2 - ry / rz2 * f))
    for a, b, c, colour, uv in m["faces"]:
        pa, pb, pc = placed[a], placed[b], placed[c]
        e1 = (pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2])
        e2 = (pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2])
        n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        nl = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
        if nl < 1e-12:
            continue
        n = (n[0] / nl, n[1] / nl, n[2] / nl)
        if n[0] * pa[0] + n[1] * pa[1] + n[2] * pa[2] > 0:
            continue                                        # shows from the other side
        k = 0.6 + 0.4 * max(0.0, n[0] * lrx + n[1] * lry + n[2] * lrz2)
        textured = colour == bmmesh.TEXTURED and sheet is not None
        if not textured:
            if colour == bmmesh.TEXTURED:
                colour = 0x8A8A9A
            flat = (int((colour >> 16 & 255) * k), int((colour >> 8 & 255) * k), int((colour & 255) * k))
        sx = (pa[3], pb[3], pc[3])
        sy = (pa[4], pb[4], pc[4])
        area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sy[1] - sy[0]) * (sx[2] - sx[0])
        if abs(area) < 1e-9:
            continue
        xmin, xmax = max(x0, int(min(sx))), min(x0 + size - 1, int(max(sx)) + 1)
        ymin, ymax = max(y0, int(min(sy))), min(y0 + size - 1, int(max(sy)) + 1)
        for y in range(ymin, ymax + 1):
            py = y + 0.5
            for x in range(xmin, xmax + 1):
                pxx = x + 0.5
                w0 = ((sx[1] - pxx) * (sy[2] - py) - (sy[1] - py) * (sx[2] - pxx)) / area
                w1 = ((sx[2] - pxx) * (sy[0] - py) - (sy[2] - py) * (sx[0] - pxx)) / area
                w2 = 1 - w0 - w1
                if w0 < 0 or w1 < 0 or w2 < 0:
                    continue
                z = w0 * pa[2] + w1 * pb[2] + w2 * pc[2]
                i = y * W + x
                if z >= zbuf[i]:
                    continue
                zbuf[i] = z
                if textured:
                    u = (w0 * uv[0] + w1 * uv[2] + w2 * uv[4])
                    v = (w0 * uv[1] + w1 * uv[3] + w2 * uv[5])
                    tw, th, rgba = sheet
                    tx = min(tw - 1, max(0, int(u)))
                    ty = min(th - 1, max(0, int(v)))
                    j = (ty * tw + tx) * 4
                    col = (int(rgba[j] * k), int(rgba[j + 1] * k), int(rgba[j + 2] * k))
                else:
                    col = flat
                px[i * 3], px[i * 3 + 1], px[i * 3 + 2] = col


def render(cart, out, size=300, only=None):
    data = open(cart, "rb").read()
    secs = dict(bmmesh.cart_sections(data))
    if bmmesh.SEC_MESH not in secs:
        raise SystemExit(f"{cart}: no 3D models")
    models, _ = bmmesh.decode(secs[bmmesh.SEC_MESH])
    if only:
        models = [m for m in models if m["name"] == only]
        if not models:
            raise SystemExit(f"{cart}: no model {only}")
    sheet = sheet_of(secs)
    W, H = size * 4, size * len(models)
    px = bytearray(W * H * 3)
    for i in range(W * H):
        px[i * 3], px[i * 3 + 1], px[i * 3 + 2] = 0x20, 0x28, 0x30
    zbuf = [1e30] * (W * H)
    for row, m in enumerate(models):
        for col, yaw in enumerate((0, 0.7, math.pi / 2, math.pi)):
            draw_model(px, zbuf, W, m, sheet, col * size, row * size, size, yaw)
    raw = b"".join(b"\0" + bytes(px[y * W * 3:(y + 1) * W * 3]) for y in range(H))

    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    with open(out, "wb") as f:
        f.write(png)
    return [(m["name"], len(m["verts"]), len(m["faces"])) for m in models]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cart")
    ap.add_argument("out")
    ap.add_argument("--size", type=int, default=300)
    ap.add_argument("--model")
    a = ap.parse_args()
    for name, nv, nf in render(a.cart, a.out, a.size, a.model):
        print(f"{name}: {nv} vertices, {nf} triangles")
    print(a.out)


if __name__ == "__main__":
    main()
