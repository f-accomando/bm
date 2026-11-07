#!/usr/bin/env python3
"""
make_testrom.py - writes a small original LoROM test ROM for the bm SNES emulator.

  python3 tests/snes/make_testrom.py out.sfc

What the ROM does: mode 0 with a 2 bpp checkerboard on BG1, a sprite that walks along a line, a
horizontal scroll that HDMA changes in bands (a test of the band logic), DMA to VRAM, CGRAM and OAM,
NMI every frame. It holds no game code or data from any console game.
"""
import struct, sys

CODE, NMI, DATA = 0x8000, 0x8200, 0x8400


class Asm:
    def __init__(self, org):
        self.org, self.b = org, bytearray()
    def pc(self): return self.org + len(self.b)
    def emit(self, *x): self.b.extend(x)
    def w16(self, v): self.emit(v & 255, v >> 8)
    # a few instructions (A is 8 bits, X and Y are 16 bits after init)
    def sei(self): self.emit(0x78)
    def clc(self): self.emit(0x18)
    def xce(self): self.emit(0xFB)
    def rep(self, v): self.emit(0xC2, v)
    def sep(self, v): self.emit(0xE2, v)
    def lda_i(self, v): self.emit(0xA9, v)
    def sta(self, a): self.emit(0x8D); self.w16(a)
    def stz(self, a): self.emit(0x9C); self.w16(a)
    def stx(self, a): self.emit(0x8E); self.w16(a)
    def lda(self, a): self.emit(0xAD); self.w16(a)
    def inc(self, a): self.emit(0xEE); self.w16(a)
    def ldx_i(self, v): self.emit(0xA2); self.w16(v)
    def wai(self): self.emit(0xCB)
    def jmp(self, a): self.emit(0x4C); self.w16(a)
    def rti(self): self.emit(0x40)
    def pha(self): self.emit(0x48)
    def pla(self): self.emit(0x68)
    def rep_x(self): self.rep(0x10)

    def dma(self, ch, mode, breg, src, size):
        base = 0x4300 + ch * 16
        self.lda_i(mode); self.sta(base)
        self.lda_i(breg); self.sta(base + 1)
        self.ldx_i(src & 0xFFFF); self.stx(base + 2)
        self.lda_i(0x00); self.sta(base + 4)
        self.ldx_i(size); self.stx(base + 5)
        self.lda_i(1 << ch); self.sta(0x420B)


def rgb(r, g, b):
    return (r & 31) | ((g & 31) << 5) | ((b & 31) << 10)


def build():
    # ---- data tables
    pal = [0] * 256
    pal[0] = rgb(2, 4, 12)                      # backdrop
    pal[1], pal[2], pal[3] = rgb(28, 28, 28), rgb(6, 20, 8), rgb(30, 6, 6)
    pal[128 + 1] = rgb(31, 24, 0)               # sprite palette 0 colour 1
    pal[128 + 2] = rgb(31, 31, 31)
    pal_b = b"".join(struct.pack("<H", c) for c in pal)

    tiles = bytearray(16)                       # tile 0: empty
    tiles += bytes([0xFF, 0x00] * 8)            # tile 1: colour 1
    tiles += bytes([0x00, 0xFF] * 8)            # tile 2: colour 2
    tiles += bytes([0xFF, 0xFF] * 8)            # tile 3: colour 3
    rows = [0x00, 0x3C, 0x42, 0x5A, 0x5A, 0x42, 0x3C, 0x00]
    for r in rows: tiles += bytes([r, 0x00])    # tile 4: a ring of colour 1
    tmap = bytearray()
    for y in range(32):
        for x in range(32):
            t = 0
            if x < 32 and y < 28:
                t = 1 + ((x + y) & 1)
                if y in (0, 27) or x in (0, 31): t = 3
                if (x, y) in ((8, 8), (9, 8), (10, 8), (8, 9)): t = 4
            tmap += struct.pack("<H", t)
    spr = bytearray()                           # one 4 bpp sprite tile: a diamond
    shape = [0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x7E, 0x3C, 0x18]
    for r in shape: spr += bytes([r, 0x00])
    for r in shape: spr += bytes([0x00, 0x00])
    oam = bytearray()
    for i in range(128):
        oam += bytes([0, 0xF0, 0, 0]) if i else bytes([60, 100, 0, 0x30])
    oam += bytes(32)
    # HDMA table for BG1 scroll: bands of 16 lines, scroll grows by 4 pixels a band (2 bytes: low, high)
    hdma = bytearray()
    for i in range(14):
        hdma += bytes([16, (i * 4) & 255, 0])
    hdma += bytes([0])

    pos = DATA
    def place(name, blob):
        nonlocal pos
        addr = pos; pos += len(blob)
        return name, addr
    blobs = [("pal", pal_b), ("tiles", bytes(tiles)), ("tmap", bytes(tmap)), ("spr", bytes(spr)),
             ("oam", bytes(oam)), ("hdma", bytes(hdma))]
    where = {}
    img = bytearray(0x8000)
    for name, b in blobs:
        _, a = place(name, b)
        where[name] = a
        img[a - 0x8000:a - 0x8000 + len(b)] = b

    # ---- main program
    c = Asm(CODE)
    c.sei(); c.clc(); c.xce(); c.rep(0x10); c.sep(0x20)
    c.lda_i(0x8F); c.sta(0x2100)                 # forced blank
    c.stz(0x4200); c.stz(0x420B); c.stz(0x420C)
    c.lda_i(0x00); c.sta(0x2121)
    c.dma(0, 0x00, 0x22, where["pal"], 512)      # CGRAM
    c.lda_i(0x80); c.sta(0x2115)                 # VMAIN: step after the high byte
    c.ldx_i(0x0000); c.stx(0x2116)
    c.dma(0, 0x01, 0x18, where["tiles"], len(tiles))     # BG chars at VRAM word 0
    c.ldx_i(0x0400); c.stx(0x2116)
    c.dma(0, 0x01, 0x18, where["tmap"], len(tmap))       # BG1 map at word $0400
    c.ldx_i(0x4000); c.stx(0x2116)
    c.dma(0, 0x01, 0x18, where["spr"], len(spr))         # sprite chars at word $4000
    c.stz(0x2102); c.stz(0x2103)
    c.dma(0, 0x00, 0x04, where["oam"], 544)              # OAM
    c.lda_i(0x00); c.sta(0x2105)                 # mode 0
    c.lda_i(0x04); c.sta(0x2107)                 # BG1 map at word $0400
    c.lda_i(0x00); c.sta(0x210B)                 # BG1 chars at 0
    c.lda_i(0x02); c.sta(0x2101)                 # sprites: 8x8/16x16, base word $4000
    c.lda_i(0x11); c.sta(0x212C)                 # BG1 + sprites
    # HDMA channel 1: BG1HOFS, mode 2 (low, high to the same register)
    c.lda_i(0x02); c.sta(0x4310)
    c.lda_i(0x0D); c.sta(0x4311)
    c.ldx_i(where["hdma"]); c.stx(0x4312)
    c.lda_i(0x00); c.sta(0x4314)
    c.lda_i(0x02); c.sta(0x420C)
    c.lda_i(0x0F); c.sta(0x2100)                 # screen on
    c.lda_i(0x81); c.sta(0x4200)                 # NMI on, auto joypad
    loop = c.pc()
    c.wai(); c.jmp(loop)
    assert c.pc() < NMI
    img[0:len(c.b)] = c.b

    # ---- NMI: move the sprite along x, count frames
    n = Asm(NMI)
    n.pha()
    n.lda(0x4210)
    n.inc(0x0010)                                # frame counter at $0010 (WRAM)
    n.lda(0x0010)
    n.stz(0x2102); n.stz(0x2103)
    n.sta(0x2104)                                # sprite 0 x = frame counter (the pair is written at the second byte)
    n.lda_i(100); n.sta(0x2104)                  # y = 100
    n.pla()
    n.rti()
    img[NMI - 0x8000:NMI - 0x8000 + len(n.b)] = n.b

    # ---- header (LoROM, at file offset $7FC0)
    h = 0x7FC0
    title = b"BM SNES TEST ROM".ljust(21)
    img[h:h + 21] = title
    img[h + 0x15] = 0x20                         # LoROM
    img[h + 0x16] = 0x00
    img[h + 0x17] = 0x05                         # 32 KB
    img[h + 0x18] = 0x00
    img[h + 0x19] = 0x01
    img[h + 0x1A] = 0x33
    vec = h + 0x24                               # $FFE4
    struct.pack_into("<HHHHHH", img, vec, 0x8000, 0x8000, 0x8000, NMI, 0x8000, 0x8000)
    struct.pack_into("<HHHHHH", img, h + 0x34, 0x8000, 0x8000, 0x8000, NMI, CODE, 0x8000)  # $FFF4..$FFFE
    struct.pack_into("<HH", img, h + 0x1C, 0xFFFF, 0x0000)
    s = sum(img) & 0xFFFF
    struct.pack_into("<HH", img, h + 0x1C, s ^ 0xFFFF, s)
    return bytes(img)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "bmtest.sfc"
    open(out, "wb").write(build())
    print("wrote", out)
