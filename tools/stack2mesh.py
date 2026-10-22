#!/usr/bin/env python3
"""
stack2mesh: a sprite stack becomes a MESH model (a .bmm file) for bm 3D.

A sprite stack is N slices of W x H pixels on a sprite sheet (the `stack`
zone of the zones file, or `stack()` / `zstack()` in Lua). Each opaque pixel
of a slice is one voxel. The tool joins voxels of the same colour into
boxes (greedy meshing): first rectangles inside each slice, then equal
rectangles on neighbouring slices become one taller box. Each box has 8
vertices and 12 triangles.

  tools/stack2mesh.py SHEET.png -o OUT.bmm --size W H -n N
                      [--at X Y] [--dir h|v] [--step S] [--name NAME]
                      [--kernel-limits] [--max-tris T]

  --size W H   size of one slice in pixels
  -n N         number of slices (2 to 255, as in a stack zone)
  --at X Y     top left corner of the first slice on the sheet (default 0 0)
  --dir h|v    h: slices side by side, left to right (stack, default);
               v: slices one under the other, top to bottom (stackv layout).
               Slice 1 is the bottom slice in both.
  --step S     height of one slice in model units (default 1, as `step` in stack())
  --name NAME  model name, 15 bytes at most (default: the file name)
  --kernel-limits  allow 65535 vertices (a model for the RGB30; the default
               4096 vertices is the limit of the tools and of the Pi budget)
  --max-tris T fail when the model has more than T triangles

Pixels: a PNG with 8 bits a channel (gray, RGB, palette, with or without
alpha). A pixel is transparent when its alpha is 0, or when the PNG has no
alpha and the pixel is black (colour 0 is transparent in bm). Every other
pixel is a voxel of its own colour.

Model frame (as bm 3D): 1 voxel = 1 unit in x and z, one slice = STEP units
in y. The model stands on y = 0, is centred on x = z = 0 and faces -z:
pixel column i is x from i - W/2, pixel row j is z from H/2 - j - 1
(the top row of the slice is the far side).

Exit: 0 written; 1 the stack is too big for the limits or a rule is broken;
2 usage or file error. Standard library only.
"""
import argparse
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
FORGE = os.path.join(HERE, "..", ".claude", "skills", "bm-forge")
sys.path.insert(0, FORGE)

VERTS_PER_BOX, TRIS_PER_BOX = 8, 12


def read_png(path):
    """PNG file -> (w, h, pixels): pixels[y][x] = (0xRRGGBB, opaque bool)"""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG file")
    pos, idat, hdr, pal, trns = 8, b"", None, None, None
    while pos + 8 <= len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            hdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            pal = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    if hdr is None:
        raise ValueError("PNG without IHDR")
    w, h, depth, ctype, _, _, interlace = hdr
    if depth != 8 or interlace != 0 or ctype not in (0, 2, 3, 4, 6):
        raise ValueError("PNG must have 8 bits a channel and no interlace")
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    if ctype == 3 and pal is None:
        raise ValueError("palette PNG without PLTE")
    raw = zlib.decompress(idat)
    stride = w * chans
    if len(raw) != h * (stride + 1):
        raise ValueError("PNG data has the wrong size")
    rows, prev = [], bytearray(stride)
    for y in range(h):
        base = y * (stride + 1)
        ft, line = raw[base], bytearray(raw[base + 1:base + 1 + stride])
        for i in range(stride):
            a = line[i - chans] if i >= chans else 0
            b = prev[i]
            c = prev[i - chans] if i >= chans else 0
            if ft == 1:
                line[i] = (line[i] + a) & 255
            elif ft == 2:
                line[i] = (line[i] + b) & 255
            elif ft == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
            elif ft != 0:
                raise ValueError("bad PNG filter")
        rows.append(line)
        prev = line
    out = []
    for line in rows:
        row = []
        for x in range(w):
            p = line[x * chans:(x + 1) * chans]
            if ctype == 0:
                r = g = b = p[0]
                opaque = p[0] != 0
            elif ctype == 2:
                r, g, b = p
                opaque = (r | g | b) != 0
            elif ctype == 3:
                r, g, b = pal[p[0]]
                opaque = trns[p[0]] != 0 if trns and p[0] < len(trns) else (r | g | b) != 0
            elif ctype == 4:
                r = g = b = p[0]
                opaque = p[1] != 0
            else:
                r, g, b = p[:3]
                opaque = p[3] != 0
            row.append((r << 16 | g << 8 | b, opaque))
        out.append(row)
    return w, h, out


def cut_slices(pixels, sheet_w, sheet_h, sw, sh, n, at=(0, 0), direction="h"):
    """sheet pixels -> slices[k][j][i] = 0xRRGGBB or None (transparent), k = 0 the bottom slice"""
    if not 2 <= n <= 255:
        raise ValueError(f"{n} slices: 2 to 255")
    if direction not in ("h", "v"):
        raise ValueError("direction must be h or v")
    if sw < 1 or sh < 1:
        raise ValueError("slice size must be at least 1x1")
    ax, ay = at
    need_w = ax + (sw * n if direction == "h" else sw)
    need_h = ay + (sh if direction == "h" else sh * n)
    if ax < 0 or ay < 0 or need_w > sheet_w or need_h > sheet_h:
        raise ValueError(f"the stack needs {need_w}x{need_h} pixels, the sheet is {sheet_w}x{sheet_h}")
    slices = []
    for k in range(n):
        ox = ax + (k * sw if direction == "h" else 0)
        oy = ay + (k * sh if direction == "v" else 0)
        sl = []
        for j in range(sh):
            sl.append([pixels[oy + j][ox + i][0] if pixels[oy + j][ox + i][1] else None for i in range(sw)])
        slices.append(sl)
    return slices


def merge_boxes(slices):
    """greedy meshing -> boxes (i, j, w, d, k0, k1, colour): columns i..i+w, rows j..j+d,
    slices k0..k1 (k1 exclusive). Same-coloured voxels that touch are joined: first
    rectangles inside a slice, then equal rectangles on neighbouring slices."""
    open_boxes, boxes = {}, []
    for k, sl in enumerate(slices):
        h, w = len(sl), len(sl[0])
        seen = [[False] * w for _ in range(h)]
        rects = []
        for j in range(h):
            for i in range(w):
                c = sl[j][i]
                if c is None or seen[j][i]:
                    continue
                rw = 1
                while i + rw < w and sl[j][i + rw] == c and not seen[j][i + rw]:
                    rw += 1
                rd = 1
                while j + rd < h and all(sl[j + rd][x] == c and not seen[j + rd][x] for x in range(i, i + rw)):
                    rd += 1
                for y in range(j, j + rd):
                    for x in range(i, i + rw):
                        seen[y][x] = True
                rects.append((i, j, rw, rd, c))
        now = set(rects)
        for key in [r for r in open_boxes if r not in now]:
            boxes.append(key[:4] + (open_boxes.pop(key), k, key[4]))
        for r in rects:
            open_boxes.setdefault(r, k)
    for key, k0 in open_boxes.items():
        boxes.append(key[:4] + (k0, len(slices), key[4]))
    boxes.sort(key=lambda b: (b[4], b[1], b[0], b[5]))
    return boxes


def boxes_json(name, boxes, sw, sh, step=1.0):
    """boxes -> the box JSON of bm-forge ({"model", "boxes": [{lo, hi, colour}]}), y up, facing -z"""
    out = []
    for i, j, w, d, k0, k1, c in boxes:
        out.append({"lo": [i - sw / 2, k0 * step, sh / 2 - j - d],
                    "hi": [i + w - sw / 2, k1 * step, sh / 2 - j],
                    "colour": c})
    return {"model": name, "boxes": out}


def count_voxels(slices):
    return sum(1 for sl in slices for row in sl for c in row if c is not None)


def mesh_to_slices(model, sw, sh, n, step=1.0):
    """a model made by this tool -> slices (as cut_slices): every group of 8 vertices is
    one box (its colour is that of its faces); used to check the round trip"""
    slices = [[[None] * sw for _ in range(sh)] for _ in range(n)]
    verts, faces = model.verts, model.faces
    if len(verts) % VERTS_PER_BOX or len(faces) != len(verts) // VERTS_PER_BOX * TRIS_PER_BOX:
        raise ValueError("not a model of boxes")
    for b in range(len(verts) // VERTS_PER_BOX):
        vs = verts[b * 8:b * 8 + 8]
        lo = [min(v[a] for v in vs) for a in range(3)]
        hi = [max(v[a] for v in vs) for a in range(3)]
        colour = faces[b * 12].colour
        i0, i1 = round(lo[0] + sw / 2), round(hi[0] + sw / 2)
        j0, j1 = round(sh / 2 - hi[2]), round(sh / 2 - lo[2])
        k0, k1 = round(lo[1] / step), round(hi[1] / step)
        for k in range(k0, k1):
            for j in range(j0, j1):
                for i in range(i0, i1):
                    if slices[k][j][i] is not None:
                        raise ValueError("boxes overlap")
                    slices[k][j][i] = colour
    return slices


def build(slices, name, step=1.0, kernel_limits=False, max_tris=None):
    """slices -> (Model, boxes); raises ValueError with the reason when a limit is passed"""
    from bm_core import mesh as meshmod, shapes
    sh, sw = len(slices[0]), len(slices[0][0])
    boxes = merge_boxes(slices)
    if not boxes:
        raise ValueError("the stack has no opaque pixel")
    mv, mf = meshmod.limits(kernel_limits)
    tris, verts = len(boxes) * TRIS_PER_BOX, len(boxes) * VERTS_PER_BOX
    if verts > mv or tris > mf:
        most = min(mv // VERTS_PER_BOX, mf // TRIS_PER_BOX)
        raise ValueError(f"{count_voxels(slices)} voxels join into {len(boxes)} boxes "
                         f"({verts} vertices, {tris} triangles); the limit is {most} boxes "
                         f"({mv} vertices, {mf} triangles). Use fewer colours, a smaller stack"
                         + ("" if kernel_limits else " or --kernel-limits"))
    if max_tris is not None and tris > max_tris:
        raise ValueError(f"{tris} triangles, over --max-tris {max_tris}")
    verts, faces = [], []
    for b in boxes_json(name, boxes, sw, sh, step)["boxes"]:
        vs, fs = shapes.box(b["lo"], b["hi"], b["colour"], len(verts))
        verts += vs
        faces += fs
    model = meshmod.Model(name, verts, faces, 0)
    meshmod.validate_model(model, kernel_limits)
    return model, boxes


def write_bmm(path, model, kernel_limits=False):
    from bm_core import export, mesh as meshmod
    export.export_bmm(path, meshmod.Mesh([model]), None, model.name, "stack2mesh", kernel_limits=kernel_limits)


def main(argv=None):
    ap = argparse.ArgumentParser(description="sprite stack PNG -> MESH model (.bmm)")
    ap.add_argument("sheet", help="sprite sheet PNG")
    ap.add_argument("-o", "--out", required=True, help="output .bmm")
    ap.add_argument("--size", nargs=2, type=int, required=True, metavar=("W", "H"))
    ap.add_argument("-n", type=int, required=True, help="slices (2 to 255)")
    ap.add_argument("--at", nargs=2, type=int, default=[0, 0], metavar=("X", "Y"))
    ap.add_argument("--dir", choices=["h", "v"], default="h")
    ap.add_argument("--step", type=float, default=1.0)
    ap.add_argument("--name")
    ap.add_argument("--kernel-limits", action="store_true")
    ap.add_argument("--max-tris", type=int)
    a = ap.parse_args(argv)
    if not a.out.lower().endswith(".bmm"):
        print("error: output must end .bmm", file=sys.stderr)
        return 2
    if not a.step > 0:
        print("error: --step must be above 0", file=sys.stderr)
        return 2
    name = (a.name or os.path.splitext(os.path.basename(a.sheet))[0])[:15]
    try:
        sheet_w, sheet_h, px = read_png(a.sheet)
        slices = cut_slices(px, sheet_w, sheet_h, a.size[0], a.size[1], a.n, tuple(a.at), a.dir)
    except (OSError, ValueError, zlib.error) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    try:
        model, boxes = build(slices, name, a.step, a.kernel_limits, a.max_tris)
        write_bmm(a.out, model, a.kernel_limits)
    except Exception as e:  # ValueError, bm_core ValidationError
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"wrote {a.out}: {count_voxels(slices)} voxels, {len(boxes)} boxes, "
          f"{len(model.verts)} vertices, {len(model.faces)} triangles")
    return 0


if __name__ == "__main__":
    sys.exit(main())
