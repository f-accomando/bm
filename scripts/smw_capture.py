#!/usr/bin/env python3
"""Runs your own Super Mario World ROM in an emulator and dumps what the game holds in memory.

    python3 scripts/smw_capture.py ROM.sfc OUTDIR [frames_after_level_start]

The level is the first one after Yoshi's House.

Needs `pip install stable-retro numpy` (the snes9x core; use a venv). Writes to OUTDIR:
    vram.bin (64 KB)  cgram.bin (512 B)  oam.bin (544 B)  wram.bin (128 KB)  screen.png
The ROM and the dumps are personal files: they stay out of git (.gitignore) and the Market.
The same loop is the reference for the physics: wram offsets $7B (x speed), $7D (y speed),
$94/$96 (x, y position), read once per frame."""
import os, re, struct, sys, zlib, warnings
warnings.filterwarnings("ignore")
import numpy as np
import retro

B, Y, SEL, START, UP, DOWN, LEFT, RIGHT, A, X, L, R = range(12)   # libretro SNES mask order


class Game:
    def __init__(self, rom):
        self.e = retro.RetroEmulator(rom)
        self.gd = retro.data.GameData()
        self.e.configure_data(self.gd)

    def wram(self):
        self.gd.update_ram()
        return self.gd.memory.blocks[0x7E0000]

    def step(self, buttons=(), n=1):
        m = np.zeros(12, dtype=np.uint8)
        for b in buttons:
            m[b] = 1
        for _ in range(n):
            self.e.set_button_mask(m, 0)
            self.e.step()

    def to_level(self):
        """Taps Start and A through the title screen until game mode $14 (a level)."""
        for f in range(3000):
            if f and self.wram()[0x100] == 0x14:
                return f
            self.step([START] if f % 90 < 5 else ([A] if 40 <= f % 90 < 45 else []))
        sys.exit("the level did not start")

    def to_first_level(self):
        """Intro, then the overworld right of Yoshi's House, into the first real level."""
        self.to_level()
        for _ in range(40):               # the intro message has several pages
            self.step([A], 3); self.step(n=17)
        self.step(n=120)
        self.step([RIGHT], 90); self.step(n=60)
        self.step([A], 3); self.step(n=200)
        if self.wram()[0x100] != 0x14:
            sys.exit("the level did not start")
        self.step(n=60)

    def blocks(self):
        """VRAM and the PPU's tables, read from the savestate's named blocks."""
        st = self.e.get_state()
        out = {}
        for m in re.finditer(rb"([A-Z]{3}):(\d{6}):", st):
            out[m.group(1).decode()] = st[m.end():m.end() + int(m.group(2))]
        return out


def save_png(path, a):
    h, w, _ = a.shape
    raw = b"".join(b"\0" + a[y].tobytes() for y in range(h))
    ch = lambda t, c: struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c))
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + ch(b"IDAT", zlib.compress(raw)) + ch(b"IEND", b""))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    g = Game(sys.argv[1])
    g.to_first_level()
    g.step(n=int(sys.argv[3]) if len(sys.argv) > 3 else 60)
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    w, b = g.wram(), g.blocks()
    open(out + "/vram.bin", "wb").write(b["VRA"])
    open(out + "/cgram.bin", "wb").write(bytes(w[0x703:0x703 + 512]))   # the game's palette mirror
    open(out + "/oam.bin", "wb").write(bytes(w[0x300:0x300 + 544]))     # the game's sprite table mirror
    open(out + "/wram.bin", "wb").write(bytes(w[:0x20000]))
    save_png(out + "/screen.png", g.e.get_screen())
    print("level %02X, Mario at %d,%d" % (w[0x13BF], w[0x94] | w[0x95] << 8, w[0x96] | w[0x97] << 8))


if __name__ == "__main__":
    main()
