#!/usr/bin/env python3
"""
make_testexe.py - writes a small original PS-EXE test program for the bm PSX emulator.

  python3 tests/psx/make_testexe.py out.exe

What it does: sets the GPU to 320x240, fills the screen dark blue, draws a Gouraud triangle (red, green, blue
corners) and a white square, prints a line with the BIOS call printf, and loops. It holds no code or data
of any console game.
"""
import struct, sys

BASE = 0x80010000


class Asm:
    def __init__(self):
        self.w = []

    def emit(self, v):
        self.w.append(v & 0xFFFFFFFF)

    def lui(self, rt, imm): self.emit((15 << 26) | (rt << 16) | (imm & 0xFFFF))
    def ori(self, rt, rs, imm): self.emit((13 << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF))
    def sw(self, rt, off, rs): self.emit((43 << 26) | (rs << 21) | (rt << 16) | (off & 0xFFFF))
    def jalr(self, rd, rs): self.emit((rs << 21) | (rd << 11) | 9)
    def nop(self): self.emit(0)

    def li(self, rt, v):
        self.lui(rt, v >> 16)
        self.ori(rt, rt, v & 0xFFFF)

    def gp0(self, v):                       # r8 holds 0x1F800000
        self.li(9, v)
        self.sw(9, 0x1810, 8)

    def gp1(self, v):
        self.li(9, v)
        self.sw(9, 0x1814, 8)


def main():
    a = Asm()
    a.lui(8, 0x1F80)
    a.gp1(0x00000000)                                              # reset
    a.gp1(0x03000000)                                              # display on
    a.gp1(0x08000001)                                              # 320 x 240
    a.gp1(0x05000000)                                              # display start (0, 0)
    a.gp1(0x06000000 | 0x260 | (0xC60 << 12))                      # horizontal range
    a.gp1(0x07000000 | 0x10 | (0x100 << 10))                       # vertical range: 240 lines
    a.gp0(0xE1000000)
    a.gp0(0xE3000000)
    a.gp0(0xE4000000 | 319 | (239 << 10))
    a.gp0(0xE5000000)
    a.gp0(0x02402010); a.gp0(0); a.gp0((240 << 16) | 320)          # fill: R 10 G 20 B 40
    a.gp0(0x300000FF); a.gp0((40 << 16) | 160)                     # gouraud triangle
    a.gp0(0x0000FF00); a.gp0((200 << 16) | 60)
    a.gp0(0x00FF0000); a.gp0((200 << 16) | 260)
    a.gp0(0x60FFFFFF); a.gp0((10 << 16) | 10); a.gp0((20 << 16) | 20)   # white square, variable size
    # printf("bm psx test ok %d\n", 42) through the BIOS call A0 / 3F
    a.ori(9, 0, 0x3F)
    a.lui(4, BASE >> 16); a.ori(4, 4, 0x0200)                      # the format string at BASE + 0x200
    a.ori(5, 0, 42)
    a.ori(8, 0, 0xA0)
    a.jalr(31, 8); a.nop()
    loop = len(a.w)
    a.emit((2 << 26) | (((BASE + loop * 4) & 0x0FFFFFFF) >> 2)); a.nop()
    text = bytearray(struct.pack("<%dI" % len(a.w), *a.w))
    text += bytes(0x200 - len(text))
    text += b"bm psx test ok %d\n\0"
    text += bytes((-len(text)) % 2048)
    hdr = bytearray(0x800)
    hdr[0:8] = b"PS-X EXE"
    struct.pack_into("<IIII", hdr, 0x10, BASE, 0, BASE, len(text))     # pc, gp, text address, text size
    struct.pack_into("<II", hdr, 0x30, 0x801FFF00, 0)                   # stack
    open(sys.argv[1], "wb").write(bytes(hdr) + bytes(text))


main()
