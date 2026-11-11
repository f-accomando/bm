#!/usr/bin/env python3
"""
make_testrom.py - a small NES test ROM (mapper 0, 16 KB program, 8 KB picture ROM), made here so that no ROM of a game
is needed in the repository.

  python3 tests/nes/make_testrom.py out.nes

What it does: the background is a checkerboard of white and green squares of 4 x 4 pixels (tile 1); a red 8 x 8 sprite
(tile 2) starts at (0, 100) and moves one pixel to the right in every frame (the NMI handler writes the sprite memory
with the DMA at $4014); while the A button of pad 1 is held, the sprite also moves down one pixel a frame.
"""
import sys

code = bytearray()


def e(*b):
    code.extend(b)


def br(op, target):                      # a branch back to the position `target`
    code.extend([op, (target - (len(code) + 2)) & 0xFF])


def sta(addr):
    e(0x8D, addr & 0xFF, addr >> 8)


def lda(v):
    e(0xA9, v)


e(0x78, 0xD8, 0xA2, 0xFF, 0x9A)                          # SEI CLD LDX #$FF TXS
lda(0x3F); sta(0x2006); lda(0x00); sta(0x2006)           # the palette at $3F00
for c in (0x30, 0x2A, 0x2A, 0x2A):
    lda(c); sta(0x2007)
lda(0x3F); sta(0x2006); lda(0x11); sta(0x2006)           # sprite colour 1 at $3F11: red
lda(0x16); sta(0x2007)
lda(0x20); sta(0x2006); lda(0x00); sta(0x2006)           # the name table: 960 tiles of tile 1, 64 attribute bytes of 0
lda(1)
e(0xA0, 3)                                               # LDY #3: three pages of 256
page = len(code)
e(0xA2, 0)
loop = len(code)
sta(0x2007); e(0xCA); br(0xD0, loop)
e(0x88); br(0xD0, page)
e(0xA2, 192)
loop = len(code)
sta(0x2007); e(0xCA); br(0xD0, loop)
lda(0)
e(0xA2, 64)
loop = len(code)
sta(0x2007); e(0xCA); br(0xD0, loop)
lda(100); sta(0x0200)                                    # the sprite: y 100, tile 2, no attributes, x 0
lda(2); sta(0x0201)
lda(0); sta(0x0202); sta(0x0203)
sta(0x2005); sta(0x2005)                                 # scroll 0, 0
lda(0x80); sta(0x2000)                                   # NMI on
lda(0x1E); sta(0x2001)                                   # the picture on, with the left 8 pixels
spin = len(code)
e(0x4C, (0xC000 + spin) & 0xFF, (0xC000 + spin) >> 8)    # JMP spin
nmi = len(code)
e(0x48)                                                  # PHA
e(0xEE, 0x03, 0x02)                                      # INC $0203: x
lda(0x02); sta(0x4014)                                   # the sprite memory from $0200
lda(1); sta(0x4016); lda(0); sta(0x4016)                 # read pad 1
e(0xAD, 0x16, 0x40, 0x29, 0x01, 0xF0, 0x03, 0xEE, 0x00, 0x02)   # LDA $4016; AND #1; BEQ +3; INC $0200: y
e(0x68, 0x40)                                            # PLA; RTI

prg = bytearray(16384)
prg[:len(code)] = code
prg[0x3FFA:0x3FFC] = bytes([(0xC000 + nmi) & 0xFF, (0xC000 + nmi) >> 8])
prg[0x3FFC:0x3FFE] = bytes([0x00, 0xC0])
prg[0x3FFE:0x4000] = bytes([0x00, 0xC0])
chr_rom = bytearray(8192)
chr_rom[16:24] = bytes([0xF0] * 4 + [0x0F] * 4)          # tile 1: squares of 4 x 4, colours 1 and 0
chr_rom[32:40] = bytes([0xFF] * 8)                       # tile 2: solid colour 1
header = b"NES\x1a" + bytes([1, 1, 0, 0]) + bytes(8)
open(sys.argv[1], "wb").write(header + bytes(prg) + bytes(chr_rom))
