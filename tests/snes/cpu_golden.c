/* cpu_golden.c: the 65C816 and the bus on random programs. A ROM of random bytes is code for the CPU (every opcode is valid): it reads and writes the
 * memory map, the PPU and CPU registers, DMA and HDMA, in LoROM and in HiROM. The machine runs a few frames; a hash of the registers, the clock, the
 * work RAM and the battery RAM after each frame must equal the value that the code had before the speed work (the table below). A change of the CPU or
 * of the memory map that changes a bit of the result, or of a clock count, fails.
 *   cpu_golden        check against the table
 *   cpu_golden print  print the table (only when the behaviour is changed on purpose)
 * Original code. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes_int.h"

static uint64_t rs;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }

static uint64_t hash_bytes(uint64_t h, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001B3ull; }
    return h;
}

#define CASES 12
static uint64_t run_case(int c)
{
    const int hirom = c & 1;
    rs = 0x9E3779B97F4A7C15ull + (uint64_t)c * 7919u;
    /* ROMs that are not a power of two, and some that are not a multiple of 4 KB: the mirroring (a page of the memory map that wraps in the middle is not fast) */
    const size_t len = hirom ? (c % 4 == 3 ? 0x140C00 : 0x100000) : (c % 4 == 2 ? 0x1C0800 : 0x180000);
    uint8_t *rom = malloc(len);
    /* No STP, WAI, branch, jump, call, return or software interrupt: the program is a straight line of the other opcodes, in a loop of the bank,
     * and keeps running (a random program with branches sits in a tight loop after a few instructions). */
    static const uint8_t flow[] = { 0xDB, 0xCB, 0x80, 0x82, 0x4C, 0x5C, 0x6C, 0x7C, 0x20, 0x22, 0xFC, 0x60, 0x6B, 0x40, 0xDC, 0x44, 0x54, 0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0, 0x00, 0x02 };
    for (size_t i = 0; i < len; i++) {
        rom[i] = (uint8_t)rnd();
        for (size_t k = 0; k < sizeof flow; k++) if (rom[i] == flow[k]) { rom[i] = 0xEA; break; }
    }
    const size_t jmp = hirom ? 0xFF00 : 0x7F00;                       /* JMP $8000 before the header: the program loops in the bank instead of running into the registers */
    rom[jmp] = 0x4C; rom[jmp + 1] = 0x00; rom[jmp + 2] = 0x80;
    const size_t off = hirom ? 0xFFC0 : 0x7FC0;
    memcpy(rom + off, "GOLDEN CPU TEST       ", 21);
    rom[off + 0x15] = hirom ? 0x21 : 0x20;
    rom[off + 0x18] = (uint8_t)(c % 3 ? 3 : 0);                      /* 8 KB of battery RAM, or none */
    rom[off + 0x1C] = 0x00; rom[off + 0x1D] = 0x00; rom[off + 0x1E] = 0xFF; rom[off + 0x1F] = 0xFF;   /* the checksum pair */
    const size_t vec = off + 0x3C;                                    /* the vectors at $FFFC: reset, $FFEA: nmi, $FFEE: irq */
    rom[vec] = 0x00; rom[vec + 1] = 0x80;                             /* reset: $8000 */
    rom[off + 0x2A] = 0x40; rom[off + 0x2B] = 0x80;                   /* nmi: $8040 */
    rom[off + 0x2E] = 0x80; rom[off + 0x2F] = 0x80;                   /* irq: $8080 */
    for (size_t i = 0x8000 + 0x40; i < 0x8000 + 0x200; i += 0x40) rom[hirom ? i : i] = 0x40;   /* an RTI here and there, so the interrupts return */
    const char *why = NULL;
    if (snes_load(rom, len, &why)) { printf("load failed: %s\n", why); free(rom); return 0; }
    free(rom);
    S.nmitimen = (uint8_t)(c % 4 == 0 ? 0xB1 : c % 4 == 1 ? 0x81 : 0x00);   /* NMI and the joypad read on, or not */
    uint64_t h = 0xCBF29CE484222325ull;
    for (int f = 0; f < 5; f++) {
        snes_frame((uint16_t)rnd(), (uint16_t)rnd(), 0);
        cpu_regs_t r;
        cpu_get(&r);
        uint8_t tmp[32];
        memcpy(tmp, &r.a, 2); memcpy(tmp + 2, &r.x, 2); memcpy(tmp + 4, &r.y, 2); memcpy(tmp + 6, &r.s, 2); memcpy(tmp + 8, &r.d, 2); memcpy(tmp + 10, &r.pc, 2);
        tmp[12] = r.p; tmp[13] = (uint8_t)r.e; tmp[14] = r.pb; tmp[15] = r.db;
        const int clk = snes_clk; memcpy(tmp + 16, &clk, 4);
        h = hash_bytes(h, tmp, 20);
        h = hash_bytes(h, S.wram, sizeof S.wram);
        h = hash_bytes(h, S.sram, sizeof S.sram);
        h = hash_bytes(h, S.vram, sizeof S.vram);
        h = hash_bytes(h, S.oam, sizeof S.oam);
        h = hash_bytes(h, (const uint8_t *)&S.frames, 4);
        if (getenv("GOLDEN_TRACE")) printf("case %d frame %d: pc %02X:%04X a %04X x %04X p %02X clk %d stopped %d waiting %d\n", c, f, r.pb, r.pc, r.a, r.x, r.p, clk, cpu_stopped, cpu_waiting);
    }
    return h;
}

static const uint64_t GOLDEN[CASES] = {
    0xC7FC20C6E116AE06ull,
    0x8C1BDB9CAA4E3D6Aull,
    0xDCAF740852B4C0DAull,
    0xBC85683A272BA58Dull,
    0xDFA43BF76ECEF6FCull,
    0x454EBF5D8676DE41ull,
    0xA0674AF613856F05ull,
    0x27A7C40CBEB8CD72ull,
    0x1C20CC1D5D51EFD0ull,
    0x1A3509E5FF20C72Bull,
    0x726EA000DBB2B164ull,
    0xF7DC6371048EFEAEull,
};

int main(int argc, char **argv)
{
    const int print = argc > 1 && !strcmp(argv[1], "print");
    int bad = 0;
    if (print) printf("static const uint64_t GOLDEN[CASES] = {\n");
    for (int c = 0; c < CASES; c++) {
        const uint64_t h = run_case(c);
        if (print) printf("    0x%016llXull,\n", (unsigned long long)h);
        else if (h != GOLDEN[c]) { printf("MISMATCH case %d: %016llX, expected %016llX\n", c, (unsigned long long)h, (unsigned long long)GOLDEN[c]); bad++; }
    }
    if (print) printf("};\n");
    else printf("cpu golden: %d cases, %s\n", CASES, bad ? "FAILED" : "ok");
    return bad != 0;
}
