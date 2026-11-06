#!/usr/bin/env python3
"""Generate font8x16.c, font16x32.c and font24x48.c using Inter (SIL Open Font License 1.1).
Preserves CP437 box-drawing and block-element graphics so terminal frames and boxes remain connected.

Features:
- Dynamic condensation for wide characters (M, W, m, w, @, %) so no glyphs are clipped at cell boundaries.
- 8x16 monitor font uses crisp 1-pixel strokes (Regular weight 400, open counters, no heavy bold blocks).
- 16x32 (720p) and 24x48 (1080p) menu fonts use clean SemiBold/Medium weight with proper margins.
"""
import os
import re
import urllib.request
from PIL import Image, ImageFont, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.join(HERE, "..")
SRC_GFX = os.path.join(REPO_ROOT, "src", "gfx")
TTF_PATH = os.path.join(REPO_ROOT, "scratch", "Inter.ttf")

# Auto-download Inter if not present
if not os.path.exists(TTF_PATH):
    os.makedirs(os.path.dirname(TTF_PATH), exist_ok=True)
    url = "https://raw.githubusercontent.com/google/fonts/main/ofl/inter/Inter%5Bopsz%2Cwght%5D.ttf"
    print(f"Downloading Inter font to {TTF_PATH}...")
    urllib.request.urlretrieve(url, TTF_PATH)

# Parse existing font8x16.c for fallback graphics (box drawing 0xB0..0xDF)
with open(os.path.join(SRC_GFX, "font8x16.c"), "r", encoding="utf-8") as f:
    text = f.read()

rows_raw = re.findall(r"\{\s*(0x[0-9a-fA-F]{2}(?:,\s*0x[0-9a-fA-F]{2}){15})\s*\}", text)
orig_8x16 = []
for r in rows_raw:
    orig_8x16.append([int(b, 16) for b in re.findall(r"0x[0-9a-fA-F]{2}", r)])

def render_inter_glyph(char, cw, ch, font_size, y_offset, weight=400, threshold=80, max_w_margin=1):
    ss = 8
    font = ImageFont.truetype(TTF_PATH, font_size * ss)
    try:
        font.set_variation_by_axes([font_size, weight])
    except Exception:
        pass

    bbox = font.getbbox(char)
    if not bbox or (bbox[2] - bbox[0]) == 0:
        return [[0] * ((cw + 7) // 8) for _ in range(ch)]

    gw = bbox[2] - bbox[0]
    gh = bbox[3] - bbox[1]

    # Maximum allowed glyph dimensions in subpixels
    max_w = (cw - max_w_margin * 2) * ss
    scale_x = min(1.0, max_w / gw) if gw > max_w else 1.0

    max_h = (ch - 2) * ss
    scale_y = min(1.0, max_h / gh) if gh > max_h else 1.0

    pad = 4 * ss
    temp = Image.new("L", (gw + 2 * pad, gh + 2 * pad), 0)
    td = ImageDraw.Draw(temp)
    td.text((pad - bbox[0], pad - bbox[1]), char, fill=255, font=font)

    if scale_x < 1.0 or scale_y < 1.0:
        new_w = max(1, int(round((gw + 2 * pad) * scale_x)))
        new_h = max(1, int(round((gh + 2 * pad) * scale_y)))
        temp = temp.resize((new_w, new_h), Image.Resampling.LANCZOS)
        final_gw = int(round(gw * scale_x))
        final_gh = int(round(gh * scale_y))
        final_pad_x = int(round(pad * scale_x))
        final_pad_y = int(round(pad * scale_y))
    else:
        final_gw = gw
        final_gh = gh
        final_pad_x = pad
        final_pad_y = pad

    cell_img = Image.new("L", (cw * ss, ch * ss), 0)
    px = (cw * ss - final_gw) // 2 - final_pad_x
    py = y_offset * ss - final_pad_y

    # Prevent top/bottom clipping for tall or accented characters
    if py + final_pad_y < 0:
        py = -final_pad_y
    if py + final_pad_y + final_gh > ch * ss:
        py = ch * ss - final_gh - final_pad_y

    cell_img.paste(temp, (px, py))
    c_down = cell_img.resize((cw, ch), Image.Resampling.LANCZOS)
    thresh = c_down.point(lambda p: 255 if p > threshold else 0, mode="1")

    # Extract row bits
    rows = []
    bpr = (cw + 7) // 8
    for y in range(ch):
        row_bytes = []
        for b in range(bpr):
            val = 0
            for bit in range(8):
                x = b * 8 + bit
                if x < cw:
                    pixel = thresh.getpixel((x, y))
                    val = (val << 1) | (1 if pixel else 0)
                else:
                    val = val << 1
            row_bytes.append(val)
        rows.append(row_bytes)
    return rows

def scale_orig_glyph(code, cw, ch):
    orig = orig_8x16[code] # 16 rows of 1 byte
    scale_y = ch // 16
    scale_x = cw // 8
    bpr = (cw + 7) // 8
    rows = []
    for y in range(ch):
        orig_y = y // scale_y
        orig_byte = orig[orig_y]
        row_bits = []
        for bit in range(8):
            val = (orig_byte >> (7 - bit)) & 1
            row_bits.extend([val] * scale_x)
        row_bytes = []
        for b in range(bpr):
            v = 0
            for bit in range(8):
                idx = b * 8 + bit
                v = (v << 1) | (row_bits[idx] if idx < len(row_bits) else 0)
            row_bytes.append(v)
        rows.append(row_bytes)
    return rows

def should_use_inter(code):
    # ASCII printable
    if 0x21 <= code <= 0x7E:
        return True
    # Accented letters in CP437
    if 0x80 <= code <= 0xA5:
        return True
    return False

def generate_font_c(name, cw, ch, font_sz, y_off, weight, threshold, max_w_margin):
    bpr = (cw + 7) // 8
    lines = [
        "/*",
        f" * bm console font {name}: Inter (SIL Open Font License 1.1).",
        " * Generated by scripts/gen_inter_font.py - do not edit.",
        f" * Glyphs are in code page 437 order; {bpr} byte(s) per row, MSB = left.",
        " */",
        '#include "font.h"',
        "",
        f"static const uint8_t glyphs[256][{bpr * ch}] = {{"
    ]

    for code in range(256):
        if should_use_inter(code):
            try:
                char = bytes([code]).decode("cp437")
                rows = render_inter_glyph(char, cw, ch, font_sz, y_off, weight, threshold, max_w_margin)
            except Exception:
                rows = scale_orig_glyph(code, cw, ch)
        elif code == 0x20:
            rows = [[0] * bpr for _ in range(ch)]
        else:
            rows = scale_orig_glyph(code, cw, ch)

        flat_bytes = []
        for r in rows:
            flat_bytes.extend(r)

        byte_strs = [f"0x{b:02x}" for b in flat_bytes]
        lines.append(f"    {{ {', '.join(byte_strs)} }}, /* 0x{code:02x} */")

    lines.append("};")
    lines.append("")
    lines.append(f"const font_t font_console_{name} = {{")
    lines.append(f"    .width = {cw},")
    lines.append(f"    .height = {ch},")
    lines.append("    .glyphs = &glyphs[0][0],")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)

def main():
    print("Generating font8x16.c (crisp 1px stroke, weight 400)...")
    c8 = generate_font_c("8x16", 8, 16, 11, 2, weight=400, threshold=95, max_w_margin=1)
    with open(os.path.join(SRC_GFX, "font8x16.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write(c8)

    print("Generating font16x32.c (no clipping, fitted wide chars)...")
    c16 = generate_font_c("16x32", 16, 32, 21, 5, weight=500, threshold=80, max_w_margin=1)
    with open(os.path.join(SRC_GFX, "font16x32.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write(c16)

    print("Generating font24x48.c (no clipping, fitted wide chars)...")
    c24 = generate_font_c("24x48", 24, 48, 32, 7, weight=500, threshold=80, max_w_margin=2)
    with open(os.path.join(SRC_GFX, "font24x48.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write(c24)

    print("Done generating fonts!")

if __name__ == "__main__":
    main()
