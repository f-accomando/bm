#!/usr/bin/env python3
"""
render_game_shots.py - Renders high-quality pixel-perfect in-game screenshots
of Titan Clash EX (RGB30) using the real Lua engine and game assets.
"""
import math
import os
import re
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
import lupa
from lupa import LuaRuntime

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SHEET_PATH = os.path.join(ROOT, "carts", "titan", "sheet.png")
MAIN_LUA_PATH = os.path.join(ROOT, "build", "titan_ex", "main.lua")
ARTIFACT_DIR = r"C:\Users\ROCKET\.gemini\antigravity\brain\93278b02-ffbd-42fb-8bcf-52e397034a1b"
os.makedirs(ARTIFACT_DIR, exist_ok=True)

# Load font8x16 from src/gfx/font8x16.c
def load_font():
    c_path = os.path.join(ROOT, "src", "gfx", "font8x16.c")
    if os.path.exists(c_path):
        src = open(c_path).read()
        rows = re.findall(r"\{ (0x[0-9a-f]{2}(?:, 0x[0-9a-f]{2}){15}) \}", src)
        return [bytes(int(b, 16) for b in r.split(", ")) for r in rows]
    return None

FONT_DATA = load_font()

class BMVirtualScreen:
    def __init__(self, w=360, h=360, sheet_img=None):
        self.w = w
        self.h = h
        self.im = Image.new("RGB", (w, h), (0, 0, 0))
        self.draw = ImageDraw.Draw(self.im)
        self.sheet = sheet_img
        self.cam_x = 0
        self.cam_y = 0
        self.clip_box = None

    def cls(self, c=0):
        self.im.paste(self._c(c), (0, 0, self.w, self.h))

    def _c(self, col):
        if isinstance(col, (tuple, list)):
            return tuple(col[:3])
        col = int(col) & 0xFFFFFF
        return ((col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF)

    def pset(self, x, y, c):
        x, y = int(x), int(y)
        if 0 <= x < self.w and 0 <= y < self.h:
            self.im.putpixel((x, y), self._c(c))

    def rectfill(self, x, y, w, h, c):
        x, y, w, h = int(x), int(y), int(w), int(h)
        if w <= 0 or h <= 0: return
        self.draw.rectangle([x, y, x + w - 1, y + h - 1], fill=self._c(c))

    def rect(self, x, y, w, h, c):
        x, y, w, h = int(x), int(y), int(w), int(h)
        if w <= 0 or h <= 0: return
        self.draw.rectangle([x, y, x + w - 1, y + h - 1], outline=self._c(c))

    def circfill(self, cx, cy, r, c):
        cx, cy, r = int(cx), int(cy), int(r)
        self.draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=self._c(c))

    def line(self, x0, y0, x1, y1, c):
        self.draw.line([int(x0), int(y0), int(x1), int(y1)], fill=self._c(c))

    def tri(self, x0, y0, x1, y1, x2, y2, c):
        self.draw.polygon([(int(x0), int(y0)), (int(x1), int(y1)), (int(x2), int(y2))], fill=self._c(c))

    def sspr(self, sx, sy, sw, sh, dx, dy, flip_x=False, flip_y=False):
        if not self.sheet: return
        sx, sy, sw, sh, dx, dy = int(sx), int(sy), int(sw), int(sh), int(dx), int(dy)
        if sw <= 0 or sh <= 0: return
        box = (sx, sy, sx + sw, sy + sh)
        try:
            cropped = self.sheet.crop(box)
        except Exception:
            return
        if flip_x:
            cropped = cropped.transpose(Image.FLIP_LEFT_RIGHT)
        if flip_y:
            cropped = cropped.transpose(Image.FLIP_TOP_BOTTOM)
        self.im.paste(cropped, (dx, dy), cropped)

    def print_text(self, s, x, y, c=0xFFFFFF, scale=1):
        s = str(s)
        x, y, scale = int(x), int(y), int(scale or 1)
        rgb = self._c(c)
        if not FONT_DATA:
            self.draw.text((x, y), s, fill=rgb)
            return x + len(s) * 8 * scale

        for ch in s:
            code = ord(ch)
            if 0 <= code < len(FONT_DATA):
                glyph = FONT_DATA[code]
                for gy in range(16):
                    row_byte = glyph[gy]
                    for gx in range(8):
                        if (row_byte >> (7 - gx)) & 1:
                            if scale == 1:
                                self.pset(x + gx, y + gy, rgb)
                            else:
                                self.rectfill(x + gx * scale, y + gy * scale, scale, scale, rgb)
            x += 8 * scale
        return x

def create_environment(screen):
    lua = LuaRuntime(unpack_returned_tuples=True)

    lua.globals().SCREEN_W = screen.w
    lua.globals().SCREEN_H = screen.h
    lua.globals().cls = lambda c=0: screen.cls(c)
    lua.globals().pset = lambda x, y, c: screen.pset(x, y, c)
    lua.globals().rectfill = lambda x, y, w, h, c: screen.rectfill(x, y, w, h, c)
    lua.globals().rect = lambda x, y, w, h, c: screen.rect(x, y, w, h, c)
    lua.globals().circfill = lambda x, y, r, c: screen.circfill(x, y, r, c)
    lua.globals().line = lambda x0, y0, x1, y1, c: screen.line(x0, y0, x1, y1, c)
    lua.globals().tri = lambda x0, y0, x1, y1, x2, y2, c: screen.tri(x0, y0, x1, y1, x2, y2, c)
    lua.globals().sspr = lambda sx, sy, sw, sh, dx, dy, fx=False, fy=False: screen.sspr(sx, sy, sw, sh, dx, dy, fx, fy)
    lua.globals().print = lambda s, x, y, c=0xFFFFFF, scale=1: screen.print_text(s, x, y, c, scale)

    # dummy sound and hardware functions
    for fn in ['note', 'noteoff', 'freq', 'envelope', 'duty', 'quit', 'stick', 'players', 'log', 'clip', 'camera', 'spr']:
        setattr(lua.globals(), fn, lambda *args: 0)

    lua.globals().btn = lambda b=0, p=None: False
    lua.globals().btnp = lambda b=0, p=None: False
    lua.globals().time = lambda: 1.0
    lua.globals().stat = lambda n=0: 60

    return lua

def main():
    print("Loading sprite sheet...")
    sheet_img = Image.open(SHEET_PATH).convert("RGBA")

    screen = BMVirtualScreen(360, 360, sheet_img)
    lua = create_environment(screen)

    print("Executing game script...")
    with open(MAIN_LUA_PATH, "r", encoding="utf-8") as f:
        code = f.read()
    lua.execute(code)
    lua.execute("_init()")

    # 1. Render TITLE SCREEN
    print("Rendering 1: Title Screen...")
    lua.execute('TITAN.Scr.go("title")')
    for _ in range(30):
        lua.execute("_update()")
    screen.cls(0)
    lua.execute("_draw()")
    out_title = os.path.join(ARTIFACT_DIR, "titan_title_screen.png")
    screen.im.resize((720, 720), Image.NEAREST).save(out_title)
    print(f"Saved: {out_title}")

    # 2. Render HANGAR CUSTOMIZATION SCREEN
    print("Rendering 2: Hangar Screen...")
    lua.execute('TITAN.Scr.go("hangar")')
    for _ in range(25):
        lua.execute("_update()")
    screen.cls(0)
    lua.execute("_draw()")
    out_hangar = os.path.join(ARTIFACT_DIR, "titan_hangar_customization.png")
    screen.im.resize((720, 720), Image.NEAREST).save(out_hangar)
    print(f"Saved: {out_hangar}")

    # 3. Render COMBAT: VANGUARD VS VALKYRIE (Hong Kong Harbor)
    print("Rendering 3: Combat Match (Hong Kong Harbor)...")
    lua.execute('''
    TITAN.Scr.setup.mode = "tag"
    TITAN.Scr.setup.stage = 1
    TITAN.Scr.setup.team1.point = { chassis = "vanguard", armor = "medium", weapon = "sword" }
    TITAN.Scr.setup.team1.partner = { chassis = "valkyrie", armor = "light", weapon = "guns" }
    TITAN.Scr.setup.team2.point = { chassis = "valkyrie", armor = "light", weapon = "guns" }
    TITAN.Scr.setup.team2.partner = { chassis = "behemoth", armor = "heavy", weapon = "cannon" }
    TITAN.Scr.go("fight")
    ''')
    for _ in range(85):
        lua.execute("_update()")
    screen.cls(0)
    lua.execute("_draw()")
    out_fight = os.path.join(ARTIFACT_DIR, "titan_fight_hongkong_harbor.png")
    screen.im.resize((720, 720), Image.NEAREST).save(out_fight)
    print(f"Saved: {out_fight}")

    # 4. Render INTENSE ACTION: BEHEMOTH IMPACT & OVERHEAT
    print("Rendering 4: Action Impact (Behemoth Overdrive & Overheat)...")
    lua.execute('''
    TITAN.Fx.clear()
    TITAN.Scr.setup.stage = 1
    TITAN.G.camx = 180
    local f1 = TITAN.G.fighters[1]
    local f2 = TITAN.G.fighters[2]
    f1.C = TITAN.Data.CHASSIS.behemoth
    f1.cfg.weapon = "cannon"
    f1.cfg.armor = "heavy"
    f1.life = 780
    f1.armor = 210
    f1.heat = 100
    f1.overheat = 120
    f1.state = "attack"
    f1.anim = "hp"
    f1.fi = 2
    f1.face = 1
    f1.x = 280
    f1.y = 0

    f2.C = TITAN.Data.CHASSIS.valkyrie
    f2.cfg.weapon = "guns"
    f2.cfg.armor = "light"
    f2.state = "hit"
    f2.anim = "hit"
    f2.fi = 1
    f2.face = -1
    f2.x = 410
    f2.y = 30
    f2.life = 420
    f2.armor = 0
    f2.shoulder = "gone"

    TITAN.Fx.spark(400, 80, "bigspark")
    TITAN.Fx.spark(415, 105, "spark")
    TITAN.Fx.boom(405, 85)
    TITAN.Fx.flames(f1)
    TITAN.Fx.text_alert("CRITICAL HIT!", 360, 165, 0xFF4A28, 2)
    TITAN.Fx.text_alert("ARMOR BROKEN!", 415, 125, 0xFF9A80, 1)
    TITAN.Fx.text_alert("OVER HEAT!", 265, 125, 0xFF5A20, 1)
    f1.combo_show = 5
    f1.combo_t = 60
    ''')
    screen.cls(0)
    lua.execute("_draw()")
    out_action = os.path.join(ARTIFACT_DIR, "titan_action_overheat_impact.png")
    screen.im.resize((720, 720), Image.NEAREST).save(out_action)
    print(f"Saved: {out_action}")

    # 5. Render HYPER SUPER MOVE: CINEMATIC CUT-IN & AURA
    print("Rendering 5: Hyper Super Move Cut-in...")
    lua.execute('''
    TITAN.Fx.clear()
    TITAN.Scr.setup.stage = 1
    TITAN.G.camx = 180
    local f1 = TITAN.G.fighters[1]
    local f2 = TITAN.G.fighters[2]
    f1.C = TITAN.Data.CHASSIS.vanguard
    f1.cfg.weapon = "sword"
    f1.cfg.armor = "medium"
    f1.state = "attack"
    f1.anim = "hp"
    f1.fi = 2
    f1.face = 1
    f1.x = 280
    f1.y = 0
    f1.super_meter = 300

    f2.C = TITAN.Data.CHASSIS.behemoth
    f2.cfg.weapon = "cannon"
    f2.cfg.armor = "heavy"
    f2.state = "hit"
    f2.anim = "hit"
    f2.fi = 1
    f2.face = -1
    f2.x = 420
    f2.y = 20

    TITAN.Fx.super_flash(f1)
    TITAN.Fx.get_super_fx().t = 15
    TITAN.G.freeze = 20
    for _ = 1, 12 do
      TITAN.Fx.burst(f1, 0x58B4FF)
    end
    TITAN.Fx.trail(f1, 0)
    TITAN.Fx.trail(f1, 1)
    ''')
    screen.cls(0)
    lua.execute("_draw()")
    out_super = os.path.join(ARTIFACT_DIR, "titan_hyper_super_tag_team.png")
    screen.im.resize((720, 720), Image.NEAREST).save(out_super)
    print(f"Saved: {out_super}")

    print("ALL 5 SCREENSHOTS RENDERED AND SAVED SUCCESSFULLY!")

if __name__ == "__main__":
    main()
