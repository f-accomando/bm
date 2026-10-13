#!/usr/bin/env python3
"""Cover generator for Tropic Trail YouTube video series.

Builds a deterministic 1280x720 PNG cover from a JSON descriptor.
Uses exclusively the Terminus console font from src/gfx/font8x16.c
and Pillow.
"""
import argparse
import json
import math
import os
import re
import sys
from PIL import Image, ImageDraw

WIDTH = 1280
HEIGHT = 720

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FONT_PATH = os.path.join(ROOT, "src", "gfx", "font8x16.c")
SHEET_PATH = os.path.join(ROOT, "art", "brand", "bm-suite.png")


def load_font():
    """Loads 256 8x16 bitmap glyphs from src/gfx/font8x16.c."""
    if not os.path.isfile(FONT_PATH):
        raise FileNotFoundError(f"Font file not found: {FONT_PATH}")
    with open(FONT_PATH, "r", encoding="utf-8") as f:
        text = f.read()

    chunk = text.split("glyphs[256][16] = {")[1].split("};")[0]
    glyphs = []
    for line in chunk.splitlines():
        if "{" in line and "}" in line:
            hex_vals = re.findall(r"0x[0-9a-fA-F]{2}", line.split("}")[0])
            if len(hex_vals) == 16:
                glyphs.append([int(v, 16) for v in hex_vals])

    if len(glyphs) != 256:
        raise ValueError(f"Expected 256 glyphs in font8x16.c, got {len(glyphs)}")
    return glyphs


def draw_char(draw, glyphs, ch_code, x, y, scale, color, bold=False):
    """Draws a single 8x16 character at scale."""
    if ch_code < 0 or ch_code >= 256:
        ch_code = ord("?")
    rows = glyphs[ch_code]
    for r in range(16):
        byte = rows[r]
        for c in range(8):
            if (byte >> (7 - c)) & 1:
                x0 = x + c * scale
                y0 = y + r * scale
                x1 = x0 + scale - 1
                y1 = y0 + scale - 1
                if bold:
                    x1 += max(1, scale // 4)
                draw.rectangle([x0, y0, x1, y1], fill=color)


def text_to_bytes(text):
    """Encodes unicode string to CP437 glyph byte codes.
    Maps middle dot (· / U+00B7) explicitly to 0xFA in font8x16.
    """
    res = bytearray()
    for ch in text:
        if ch in ("·", "\u00b7") or ord(ch) == 0xFA:
            res.append(0xFA)
        else:
            try:
                b = ch.encode("cp437")
                res.extend(b)
            except UnicodeEncodeError:
                res.append(ord("?"))
    return bytes(res)


def draw_text(draw, glyphs, text, x, y, scale, color, bold=False, spacing=1):
    """Renders text string using code page 437 encoding."""
    raw_bytes = text_to_bytes(text)
    cur_x = x
    char_w = 8 * scale + spacing
    for b in raw_bytes:
        draw_char(draw, glyphs, b, cur_x, y, scale, color, bold=bold)
        cur_x += char_w
    return cur_x


def text_width(text, scale, spacing=1, bold=False):
    return len(text_to_bytes(text)) * (8 * scale + spacing) + (max(1, scale // 4) if bold else 0)


def wrap_text(text, max_w, scale, spacing=1, bold=False):
    words = text.split()
    if not words:
        return []
    lines = []
    cur_line = []
    for w in words:
        cand = " ".join(cur_line + [w])
        if text_width(cand, scale, spacing, bold) <= max_w:
            cur_line.append(w)
        else:
            if cur_line:
                lines.append(" ".join(cur_line))
                cur_line = [w]
            else:
                lines.append(w)
                cur_line = []
    if cur_line:
        lines.append(" ".join(cur_line))
    return lines


def hex_to_rgb(hex_str, default=(255, 200, 32)):
    if not hex_str:
        return default
    hex_str = hex_str.strip().lstrip("#")
    if len(hex_str) == 6:
        try:
            return tuple(int(hex_str[i : i + 2], 16) for i in (0, 2, 4))
        except ValueError:
            pass
    return default


def make_gradient():
    """Generates a smooth deterministic jungle green -> deep blue gradient."""
    im = Image.new("RGB", (WIDTH, HEIGHT))
    px = im.load()
    # Jungle green to deep oceanic blue
    c1 = (18, 78, 48)   # Top-left jungle green
    c2 = (10, 32, 68)   # Center-right oceanic blue
    c3 = (6, 18, 42)    # Bottom-right midnight blue

    for y in range(HEIGHT):
        ty = y / (HEIGHT - 1.0)
        for x in range(WIDTH):
            tx = x / (WIDTH - 1.0)
            t = 0.45 * tx + 0.55 * ty
            if t < 0.6:
                k = t / 0.6
                r = round(c1[0] + (c2[0] - c1[0]) * k)
                g = round(c1[1] + (c2[1] - c1[1]) * k)
                b = round(c1[2] + (c2[2] - c1[2]) * k)
            else:
                k = (t - 0.6) / 0.4
                r = round(c2[0] + (c3[0] - c2[0]) * k)
                g = round(c2[1] + (c3[1] - c2[1]) * k)
                b = round(c2[2] + (c3[2] - c2[2]) * k)
            px[x, y] = (r, g, b)
    return im


def draw_pico_silhouette(draw, box):
    """Draws a stylized flat-shaded polygonal silhouette of Pico the gecko."""
    x0, y0, x1, y1 = box
    cx = (x0 + x1) // 2
    cy = (y0 + y1) // 2 + 10

    # Colors
    c_dark_grid = (15, 45, 60)
    c_platform = (24, 60, 75)
    c_plat_top = (35, 80, 95)
    mint_light = (110, 230, 165)
    mint_mid = (72, 199, 142)
    mint_dark = (42, 150, 102)
    mint_shadow = (28, 110, 75)
    orange_bright = (255, 120, 48)
    orange_dark = (210, 80, 25)
    white = (255, 255, 255)
    pupil = (20, 40, 30)

    # Perspective pedestal / grid in viewport
    ped_pts = [
        (cx - 160, cy + 120),
        (cx + 160, cy + 120),
        (cx + 210, cy + 180),
        (cx - 210, cy + 180),
    ]
    draw.polygon(ped_pts, fill=c_platform)
    draw.line(ped_pts + [ped_pts[0]], fill=c_plat_top, width=2)

    # Grid lines on pedestal
    for i in range(-3, 4):
        bx = cx + i * 45
        tx_p = cx + i * 32
        draw.line([(tx_p, cy + 120), (bx, cy + 180)], fill=c_dark_grid, width=1)

    # Tail polygons (curling behind)
    tail_pts_1 = [(cx - 50, cy + 40), (cx - 100, cy + 60), (cx - 120, cy + 30), (cx - 80, cy + 20)]
    draw.polygon(tail_pts_1, fill=mint_shadow)
    tail_pts_2 = [(cx - 120, cy + 30), (cx - 140, cy - 10), (cx - 110, cy - 20), (cx - 100, cy + 10)]
    draw.polygon(tail_pts_2, fill=mint_dark)
    tail_tip = [(cx - 110, cy - 20), (cx - 80, cy - 35), (cx - 75, cy - 15), (cx - 100, cy + 5)]
    draw.polygon(tail_tip, fill=mint_mid)

    # Body torso polygons
    body_back = [(cx - 40, cy + 45), (cx + 25, cy + 45), (cx + 15, cy - 25), (cx - 30, cy - 20)]
    draw.polygon(body_back, fill=mint_dark)
    body_front = [(cx - 30, cy - 20), (cx + 15, cy - 25), (cx + 35, cy + 20), (cx - 15, cy + 45)]
    draw.polygon(body_front, fill=mint_mid)
    belly = [(cx - 15, cy + 45), (cx + 35, cy + 20), (cx + 42, cy + 48), (cx + 10, cy + 65)]
    draw.polygon(belly, fill=mint_light)

    # Legs & Orange Shoes
    # Left leg & shoe (front perspective)
    l_leg = [(cx - 25, cy + 40), (cx - 45, cy + 85), (cx - 25, cy + 90), (cx - 10, cy + 45)]
    draw.polygon(l_leg, fill=mint_dark)
    l_shoe_body = [(cx - 55, cy + 85), (cx - 15, cy + 85), (cx - 10, cy + 115), (cx - 65, cy + 115)]
    draw.polygon(l_shoe_body, fill=orange_dark)
    l_shoe_top = [(cx - 50, cy + 82), (cx - 20, cy + 82), (cx - 15, cy + 95), (cx - 55, cy + 95)]
    draw.polygon(l_shoe_top, fill=orange_bright)
    l_sole = [(cx - 67, cy + 113), (cx - 8, cy + 113), (cx - 8, cy + 120), (cx - 67, cy + 120)]
    draw.polygon(l_sole, fill=white)

    # Right leg & shoe
    r_leg = [(cx + 15, cy + 42), (cx + 45, cy + 85), (cx + 62, cy + 85), (cx + 30, cy + 45)]
    draw.polygon(r_leg, fill=mint_mid)
    r_shoe_body = [(cx + 35, cy + 85), (cx + 80, cy + 85), (cx + 88, cy + 115), (cx + 30, cy + 115)]
    draw.polygon(r_shoe_body, fill=orange_bright)
    r_shoe_top = [(cx + 40, cy + 82), (cx + 75, cy + 82), (cx + 80, cy + 95), (cx + 35, cy + 95)]
    draw.polygon(r_shoe_top, fill=(255, 160, 80))
    r_sole = [(cx + 28, cy + 113), (cx + 90, cy + 113), (cx + 90, cy + 120), (cx + 28, cy + 120)]
    draw.polygon(r_sole, fill=white)

    # Head facets (low poly gecko snout and cranium)
    head_back = [(cx - 30, cy - 20), (cx + 10, cy - 25), (cx + 5, cy - 65), (cx - 35, cy - 50)]
    draw.polygon(head_back, fill=mint_dark)
    head_top = [(cx - 35, cy - 50), (cx + 5, cy - 65), (cx + 35, cy - 70), (cx + 10, cy - 45)]
    draw.polygon(head_top, fill=mint_mid)
    head_snout = [(cx + 10, cy - 45), (cx + 35, cy - 70), (cx + 75, cy - 45), (cx + 40, cy - 25)]
    draw.polygon(head_snout, fill=mint_light)
    head_jaw = [(cx + 5, cy - 25), (cx + 40, cy - 25), (cx + 70, cy - 40), (cx + 35, cy - 15)]
    draw.polygon(head_jaw, fill=mint_mid)

    # Big friendly eye
    eye_pts = [(cx - 5, cy - 72), (cx + 25, cy - 75), (cx + 32, cy - 50), (cx + 5, cy - 48)]
    draw.polygon(eye_pts, fill=white)
    pupil_pts = [(cx + 8, cy - 68), (cx + 22, cy - 70), (cx + 25, cy - 54), (cx + 12, cy - 53)]
    draw.polygon(pupil_pts, fill=pupil)
    # Highlight in eye
    draw.rectangle([cx + 12, cy - 66, cx + 16, cy - 61], fill=white)


def get_bm_logo():
    """Extracts or builds the iconic bm logo."""
    if os.path.isfile(SHEET_PATH):
        try:
            sheet = Image.open(SHEET_PATH).convert("RGB")
            # Logo box from scripts/mklogo.py
            logo_crop = sheet.crop((316, 72, 566, 222))
            # Transparent background by thresholding darkest corner
            px = logo_crop.load()
            w, h = logo_crop.size
            corners = [px[0, 0], px[w - 1, 0], px[0, h - 1], px[w - 1, h - 1]]
            bg = min(corners, key=sum)
            rgba = Image.new("RGBA", (w, h))
            rpx = rgba.load()
            for y in range(h):
                for x in range(w):
                    p = px[x, y]
                    d = max(abs(p[i] - bg[i]) for i in range(3))
                    a = int(min(max((d - 14) / 30, 0.0), 1.0) * 255)
                    rpx[x, y] = (p[0], p[1], p[2], a)
            bbox = rgba.getbbox()
            if bbox:
                rgba = rgba.crop(bbox)
            # Scale down cleanly to target height ~42px
            target_h = 42
            target_w = round(rgba.width * target_h / rgba.height)
            return rgba.resize((target_w, target_h), Image.LANCZOS)
        except Exception:
            pass
    return None


def render_cover(data):
    """Renders the 1280x720 cover image from dictionary data."""
    glyphs = load_font()

    im = make_gradient()
    draw = ImageDraw.Draw(im)

    number = data.get("number", 1)
    title = data.get("title", "Untitled Episode")
    subtitle = data.get("subtitle", "")
    tool = data.get("tool", "")
    screenshot_path = data.get("screenshot")
    accent_rgb = hex_to_rgb(data.get("accent"), default=(255, 180, 32))

    # 1. Top Banner: "TROPIC TRAIL · EP n"
    banner_y = 42
    banner_x = 64
    # Accent indicator pill
    draw.rectangle([banner_x, banner_y + 2, banner_x + 6, banner_y + 24], fill=accent_rgb)

    banner_text = f"TROPIC TRAIL · EP {number}"
    draw_text(draw, glyphs, banner_text, banner_x + 18, banner_y + 4, scale=2, color=accent_rgb, bold=True)

    # Subtle horizontal divider line
    draw.line([(banner_x, banner_y + 36), (WIDTH - 64, banner_y + 36)], fill=(30, 75, 95), width=2)

    # 2. Right box: Screenshot or Pico stylized silhouette
    box_x0, box_y0 = 660, 120
    box_x1, box_y1 = 1216, 560
    box_w = box_x1 - box_x0
    box_h = box_y1 - box_y0

    # Draw box background & outer shadow
    draw.rectangle([box_x0 + 6, box_y0 + 6, box_x1 + 6, box_y1 + 6], fill=(5, 15, 25))
    draw.rectangle([box_x0, box_y0, box_x1, box_y1], fill=(12, 32, 48))

    # Box frame
    draw.rectangle([box_x0, box_y0, box_x1, box_y1], outline=accent_rgb, width=3)

    has_screenshot = False
    if screenshot_path and os.path.isfile(screenshot_path):
        try:
            shot_im = Image.open(screenshot_path).convert("RGB")
            # Fit inside box with margin 3
            inner_w = box_w - 6
            inner_h = box_h - 6
            shot_scaled = shot_im.resize((inner_w, inner_h), Image.LANCZOS)
            im.paste(shot_scaled, (box_x0 + 3, box_y0 + 3))
            has_screenshot = True
        except Exception:
            pass

    if not has_screenshot:
        draw_pico_silhouette(draw, (box_x0 + 4, box_y0 + 4, box_x1 - 4, box_y1 - 4))
        # Viewport watermark tag
        tag = "[ VIEWPORT: 3D MODEL ]"
        draw_text(draw, glyphs, tag, box_x0 + 16, box_y1 - 28, scale=1, color=(80, 140, 160))

    # 3. Left Content Area
    max_col_w = box_x0 - banner_x - 30  # ~566 px

    # Large Number on left
    num_str = f"{number:02d}" if number < 100 else str(number)
    num_scale = 10
    draw_text(draw, glyphs, num_str, banner_x, 130, scale=num_scale, color=accent_rgb, bold=True)

    # Episode label under/beside number
    draw_text(draw, glyphs, "EPISODE", banner_x, 130 + 16 * num_scale + 10, scale=2, color=(140, 190, 210), bold=True)

    # Title white, bold, max 2 lines (scale reduces until fits, no truncation)
    title_scale = 1
    title_lines = [title]
    for test_s in (4, 3, 2, 1):
        lines = wrap_text(title, max_col_w, test_s, bold=True)
        if len(lines) <= 2:
            title_scale = test_s
            title_lines = lines
            break
    if not title_lines:
        title_scale = 1
        title_lines = wrap_text(title, max_col_w, 1, bold=True)

    title_y = 350
    for line in title_lines:
        draw_text(draw, glyphs, line, banner_x, title_y, scale=title_scale, color=(255, 255, 255), bold=True)
        title_y += 16 * title_scale + 8

    # Subtitle with tool tag (contained strictly within max_col_w)
    title_y += 14
    if tool and subtitle:
        tool_tag = f"[{tool}]"
        combined = f"{tool_tag}  {subtitle}"
        if text_width(combined, scale=2, bold=False) <= max_col_w:
            draw_text(draw, glyphs, tool_tag, banner_x, title_y, scale=2, color=accent_rgb, bold=True)
            badge_w = text_width(tool_tag, scale=2, bold=True) + 16
            draw_text(draw, glyphs, subtitle, banner_x + badge_w, title_y, scale=2, color=(185, 220, 235))
            title_y += 16 * 2 + 8
        else:
            draw_text(draw, glyphs, tool_tag, banner_x, title_y, scale=2, color=accent_rgb, bold=True)
            title_y += 16 * 2 + 8
            sub_lines = wrap_text(subtitle, max_col_w, scale=2)
            for s_line in sub_lines:
                draw_text(draw, glyphs, s_line, banner_x, title_y, scale=2, color=(185, 220, 235))
                title_y += 16 * 2 + 6
    elif tool:
        tool_tag = f"[{tool}]"
        draw_text(draw, glyphs, tool_tag, banner_x, title_y, scale=2, color=accent_rgb, bold=True)
        title_y += 16 * 2 + 8
    elif subtitle:
        sub_lines = wrap_text(subtitle, max_col_w, scale=2)
        for s_line in sub_lines:
            draw_text(draw, glyphs, s_line, banner_x, title_y, scale=2, color=(185, 220, 235))
            title_y += 16 * 2 + 6

    # 4. Bottom bar: bm logo at bottom right
    bm_logo = get_bm_logo()
    if bm_logo:
        lx = WIDTH - 64 - bm_logo.width
        ly = HEIGHT - 40 - bm_logo.height
        im.paste(bm_logo, (lx, ly), bm_logo)
    else:
        # Fallback drawn retro bm logo
        draw_text(draw, glyphs, "bm", WIDTH - 120, HEIGHT - 55, scale=3, color=(255, 255, 255), bold=True)

    return im


def main():
    parser = argparse.ArgumentParser(description="Generate Tropic Trail episode cover PNG.")
    parser.add_argument("json_file", help="Path to cover JSON descriptor")
    parser.add_argument("-o", "--output", required=True, help="Path to output PNG image")
    args = parser.parse_args()

    with open(args.json_file, "r", encoding="utf-8") as f:
        data = json.load(f)

    im = render_cover(data)

    out_dir = os.path.dirname(os.path.abspath(args.output))
    if out_dir and not os.path.exists(out_dir):
        os.makedirs(out_dir, exist_ok=True)

    im.save(args.output, format="PNG")
    print(f"Cover written: {args.output} ({WIDTH}x{HEIGHT})")


if __name__ == "__main__":
    main()
