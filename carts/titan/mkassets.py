#!/usr/bin/env python3
"""Titan Clash: every picture of the game, made in code and packed into one
sprite sheet (sheet.png, 2048 wide, at most 4096 tall and 255 colours: the
cartridge stores it as SHEET8), with src/05_sprites.lua listing where each
one is.

  robot     mkrobot.py: every frame of VANGUARD and its layers, in the
            colours of each player
  arena     a ruined city at dusk in parallax layers (far skyline, the
            buildings the robots are as tall as, the street, the rubble in
            front), fires
  bay       a player's half of the hangar, seen three quarters deep: gantries,
            the pad, the maintenance cage, tiny workers, a crane
  fx        sparks, muzzle flashes, booster flames, explosions, dust (the
            sword's trail and the bullets are drawn by the game)
  text      FIGHT!, K.O., the rounds, the logo, big digits

Needs numpy and Pillow; the outputs are in git, so `make` does not.

  python3 carts/titan/mkassets.py [--preview DIR]
"""
import argparse
import hashlib
import math
import os
import random
import re
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, HERE)
import mkrobot  # noqa: E402

SHEET_W = 2048


def hexc(s):
    s = s.lstrip("#")
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16), 255)


def load_font():
    src = open(os.path.join(ROOT, "src", "gfx", "font8x16.c")).read()
    rows = re.findall(r"\{ (0x[0-9a-f]{2}(?:, 0x[0-9a-f]{2}){15}) \}", src)
    return [bytes(int(b, 16) for b in r.split(", ")) for r in rows]


FONT = load_font()

# ---------------------------------------------------------------- packing


class Packer:
    """skyline packing into a sheet SHEET_W wide"""

    def __init__(self):
        self.sky = [(0, 0, SHEET_W)]        # segments (x, y, width)
        self.items = []                     # (name, image, x, y)
        self.seen = {}

    @staticmethod
    def key(img):
        return hashlib.sha1(img.tobytes() + repr(img.shape).encode()).hexdigest()

    def add(self, name, img):
        """img: RGBA numpy array (h, w, 4); returns (x, y)"""
        key = self.key(img)
        if key in self.seen:
            return self.seen[key]
        h, w = img.shape[:2]
        best = None
        for i in range(len(self.sky)):
            x = self.sky[i][0]
            if x + w > SHEET_W:
                break
            # the height needed over [x, x + w)
            y, j, span = 0, i, 0
            while span < w:
                y = max(y, self.sky[j][1])
                span += self.sky[j][2]
                j += 1
                if j == len(self.sky) and span < w:
                    break
            if span < w:
                continue
            if best is None or y + h < best[1] + best[2] or (y + h == best[1] + best[2] and x < best[0]):
                best = (x, y, h, i)
        x, y, _, i = best
        # the skyline: [x, x+w) now at y + h
        new = []
        for sx, sy, sw in self.sky:
            a, b = sx, sx + sw
            if b <= x or a >= x + w:
                new.append((sx, sy, sw))
                continue
            if a < x:
                new.append((a, sy, x - a))
            if b > x + w:
                new.append((x + w, sy, b - x - w))
        new.append((x, y + h + 1, w))
        new.sort()
        merged = []
        for seg in new:
            if merged and merged[-1][1] == seg[1] and merged[-1][0] + merged[-1][2] == seg[0]:
                merged[-1] = (merged[-1][0], seg[1], merged[-1][2] + seg[2])
            else:
                merged.append(seg)
        self.sky = merged
        self.items.append((name, img, x, y))
        self.seen[key] = (x, y)
        return x, y

    def height(self):
        return max(y + img.shape[0] for _, img, _, y in self.items)

    def image(self):
        h = (self.height() + 7) // 8 * 8
        out = np.zeros((h, SHEET_W, 4), np.uint8)
        for _, img, x, y in self.items:
            ih, iw = img.shape[:2]
            region = out[y:y + ih, x:x + iw]
            m = img[:, :, 3] > 0
            region[m] = img[m]
        return out


def crop(img):
    """the smallest box around the opaque pixels: (image, x0, y0) or None"""
    a = img[:, :, 3] > 0
    if not a.any():
        return None
    ys, xs = np.nonzero(a)
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    return img[y0:y1, x0:x1].copy(), int(x0), int(y0)


def pieces(img, block=8):
    """a sparse picture as compact pieces: [(image, x0, y0)]. Blocks of
    block x block pixels with something in them are grouped into connected
    clusters; each cluster becomes a piece (only its own pixels)."""
    h, w = img.shape[:2]
    a = img[:, :, 3] > 0
    if not a.any():
        return []
    bh, bw = (h + block - 1) // block, (w + block - 1) // block
    occ = np.zeros((bh, bw), bool)
    ys, xs = np.nonzero(a)
    occ[ys // block, xs // block] = True
    label = -np.ones((bh, bw), int)
    n = 0
    for by in range(bh):
        for bx in range(bw):
            if occ[by, bx] and label[by, bx] < 0:
                stack = [(by, bx)]
                label[by, bx] = n
                while stack:
                    cy, cx = stack.pop()
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            ny, nx = cy + dy, cx + dx
                            if 0 <= ny < bh and 0 <= nx < bw and occ[ny, nx] and label[ny, nx] < 0:
                                label[ny, nx] = n
                                stack.append((ny, nx))
                n += 1
    whole = crop(img)
    if n == 1:
        return [whole]
    out, area = [], 0
    full = np.kron(label, np.ones((block, block), int))[:h, :w]
    for k in range(n):
        part = img.copy()
        part[full != k] = 0
        c = crop(part)
        if c:
            out.append(c)
            area += c[0].shape[0] * c[0].shape[1]
    # not worth it when the pieces nearly fill the box anyway
    if area > 0.75 * whole[0].shape[0] * whole[0].shape[1]:
        return [whole]
    return out


def pil(img):
    return np.array(img.convert("RGBA"))

# ---------------------------------------------------------------- text

def text_mask(s, scale):
    """the console font, `scale` times bigger: boolean mask"""
    w, h = len(s) * 8, 16
    m = np.zeros((h, w), bool)
    for i, ch in enumerate(s):
        g = FONT[ord(ch)]
        for y in range(16):
            for x in range(8):
                if g[y] & (0x80 >> x):
                    m[y, i * 8 + x] = True
    # trim the font's empty rows
    rows = np.nonzero(m.any(axis=1))[0]
    m = m[rows.min():rows.max() + 1]
    return np.kron(m, np.ones((scale, scale), bool))


def big_text(s, scale, top, bottom, outline, glow=None, outline2=None):
    """letters with a vertical gradient, a dark outline and an optional
    second (coloured) outline"""
    m = text_mask(s, scale)
    pad = 3
    h, w = m.shape
    img = np.zeros((h + pad * 2, w + pad * 2, 4), np.uint8)
    mm = np.zeros(img.shape[:2], bool)
    mm[pad:pad + h, pad:pad + w] = m
    t0, t1 = np.array(top[:3], float), np.array(bottom[:3], float)
    for y in range(img.shape[0]):
        k = min(1.0, max(0.0, (y - pad) / max(1, h - 1)))
        # four bands, the same four colours for every text of a scheme
        c = t0 + (t1 - t0) * (min(3, int(k * 4)) / 3)
        # a bright band a third of the way down, like chrome
        if glow is not None and abs(k - 0.42) < 0.06:
            c = np.array(glow[:3], float)
        img[y, mm[y], :3] = np.round(c).astype(np.uint8)
        img[y, mm[y], 3] = 255

    def grow(mask):
        g = mask.copy()
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0), (1, 1), (-1, -1), (1, -1), (-1, 1)):
            g |= np.roll(np.roll(mask, dy, 0), dx, 1)
        return g
    o1 = grow(mm) & ~mm
    img[o1] = outline
    if outline2 is not None:
        o2 = grow(grow(mm)) & ~grow(mm)
        img[o2] = outline2
    return img

# ---------------------------------------------------------------- the robot

def robot(pk, lua):
    """every frame and layer of the robot: FR (per frame) and ANIM (names)"""
    mkrobot.build_robot()
    frames = mkrobot.frames()
    C, F = mkrobot.CANVAS, mkrobot.FLOOR_Y
    lua.append("-- VANGUARD: per frame the base picture and its layers, each a list of")
    lua.append("-- pieces {sx, sy, w, h, dx, dy} (dx, dy: from the feet; false if empty),")
    lua.append("-- the hurt boxes and the hit box {x0, y0, x1, y1} (x forwards, y up is minus)")
    anims = {}
    total = [0]

    def layers(i, f, alt):
        parts = []
        for key, lay in (("b", "base"), ("hv", "heavy"), ("s", "shoulder"), ("sd", "shoulder_d"),
                         ("sh", "shoulder_h"), ("shd", "shoulder_hd"), ("g", "guns"), ("sw", "sword")):
            src = f["layers"][lay]
            if alt:
                src = mkrobot.recolor(src)
            ps = pieces(src) if lay != "base" else [crop(src)]
            if not ps:
                parts.append(f"{key} = false")
                continue
            rects = []
            for img, x0, y0 in ps:
                if (pk.seen.get(pk.key(img)) is None):
                    total[0] += img.shape[0] * img.shape[1]
                x, y = pk.add(f"fr{i}_{lay}", img)
                rects.append(f"{{{x}, {y}, {img.shape[1]}, {img.shape[0]}, {x0 - C // 2}, {y0 - F}}}")
            parts.append(f"{key} = {{{', '.join(rects)}}}")
        return parts

    lua.append("FR = {")
    for i, f in enumerate(frames):
        anims.setdefault(f["anim"], []).append(i + 1)
        parts = layers(i, f, False)
        hurt = [b for b in f["hurt"] if b]
        hb = ", ".join(f"{{{b[0]}, {b[1]}, {b[2]}, {b[3]}}}" for b in hurt)
        hit = f"{{{f['hit'][0]}, {f['hit'][1]}, {f['hit'][2]}, {f['hit'][3]}}}" if f["hit"] else "false"
        lua.append(f"  {{ {', '.join(parts)}, swf = {'true' if f['sword_front'] else 'false'},")
        lua.append(f"    hurt = {{{hb}}}, hit = {hit} }}, -- {f['anim']} {f['k'] + 1}")
    lua.append("}")
    lua.append("-- the same frames in the second player's colours (the pictures only)")
    lua.append("FR2 = {")
    for i, f in enumerate(frames):
        lua.append(f"  {{ {', '.join(layers(i, f, True))} }}, -- {f['anim']} {f['k'] + 1}")
    lua.append("}")
    lua.append("ANIM = {")
    for name, ids in anims.items():
        lua.append(f"  {name} = {{{', '.join(str(i) for i in ids)}}},")
    lua.append("}")
    print(f"robot: {len(frames)} frames, {total[0]} pixels of layers in two colours")
    return frames

# ---------------------------------------------------------------- output

def sprite_line(name, pk, img, x0=0, y0=0):
    x, y = pk.add(name, img)
    return f"  {name} = {{{x}, {y}, {img.shape[1]}, {img.shape[0]}, {x0}, {y0}}},"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preview", default=None)
    a = ap.parse_args()
    pk = Packer()
    lua = ["-- Titan Clash: made by mkassets.py (do not edit). Sprites: {sx, sy, w, h, dx, dy}", ""]
    robot(pk, lua)
    lua.append("")
    lua.append("SPR = {")
    import art
    for name, img, dx, dy in art.everything():
        c = crop(img)
        if c:
            img, x0, y0 = c
            lua.append(sprite_line(name, pk, img, dx + x0, dy + y0))
    for name, s, scale, top, bottom, out, glow, out2 in art.TEXTS:
        img = big_text(s, scale, top, bottom, out, glow, out2)
        lua.append(sprite_line(name, pk, img, -(img.shape[1] // 2), -(img.shape[0] // 2)))
    lua.append("}")
    sheet = pk.image()
    cols = {tuple(c) for c in sheet.reshape(-1, 4)[sheet.reshape(-1, 4)[:, 3] > 0][:, :3]}
    print(f"sheet: {SHEET_W}x{sheet.shape[0]}, {len(pk.items)} pictures, {len(cols)} colours")
    if len(cols) > 255:
        raise SystemExit("more than 255 colours: the cartridge cannot store the sheet as SHEET8")
    Image.fromarray(sheet, "RGBA").save(os.path.join(HERE, "sheet.png"))
    open(os.path.join(HERE, "src", "05_sprites.lua"), "w").write("\n".join(lua) + "\n")
    art.cover(os.path.join(HERE, "cover.png"))
    if a.preview:
        os.makedirs(a.preview, exist_ok=True)
        art.preview(sheet, lua, a.preview)


if __name__ == "__main__":
    main()
