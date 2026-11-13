/* The tests of the Super FX co-processor (core/src/bm/snes_gsu.c) and of the cartridge map around it. The programs are original and tiny; they run from a
 * made-up ROM (no commercial ROM). Run: make test-snes-gsu. `gsu_test bench` times the interpreter (make bench-snes-gsu). */
#include "snes_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails, count;
#define CHECK(name, cond) do { count++; if (!(cond)) { fails++; printf("FAIL %s\n", name); } } while (0)
#define CHECKV(name, got, want) do { count++; if ((unsigned)(got) != (unsigned)(want)) { fails++; printf("FAIL %s: got %x want %x\n", name, (unsigned)(got), (unsigned)(want)); } } while (0)

#define ROM_SIZE 0x20000
static uint8_t rom[ROM_SIZE];

/* a Super FX ROM: LoROM header at $7FC0, chipset $14, the code at the start of the ROM (the GSU sees it at bank 0, $8000) */
static void load_rom(const uint8_t *code, size_t n)
{
    memset(rom, 0, sizeof rom);
    for (size_t i = 0; i < ROM_SIZE; i++) rom[i] = (uint8_t)(i * 7 + (i >> 8) * 13 + 3);   /* known data for GETB and the map tests */
    memcpy(rom, code, n);
    memset(rom + 0x7FC0, ' ', 21);
    memcpy(rom + 0x7FC0, "GSU TEST", 8);
    rom[0x7FD5] = 0x20; rom[0x7FD6] = 0x14; rom[0x7FD7] = 0x09; rom[0x7FD8] = 0;
    rom[0x7FDC] = 0xFF; rom[0x7FDD] = 0xFF; rom[0x7FDE] = 0; rom[0x7FDF] = 0;     /* checksum complement and checksum */
    for (int i = 0x7FE0; i < 0x8000; i++) rom[i] = 0;                             /* the vectors of the first bank: zero */
    rom[0x7FFC] = 0x00; rom[0x7FFD] = 0x80;
    snes_load(rom, sizeof rom, NULL);
}

static void wr(uint32_t a, uint8_t v) { snes_write(a, v); }
static void wreg(int n, uint16_t v) { wr(0x3000 + 2 * n, (uint8_t)v); wr(0x3001 + 2 * n, (uint8_t)(v >> 8)); }   /* R15 starts the program */

static int run_lines(int max)
{
    int l = 0;
    while (gsu_go() && l < max) { gsu_run(1364); l++; }
    return l;
}

/* run a program from the ROM at bank 0 offset $8000 with SCMR = scmr, SCBR = 0; the program ends with STOP */
static void run_code(const uint8_t *code, size_t n, uint8_t scmr)
{
    load_rom(code, n);
    wr(0x303A, scmr);
    wr(0x3038, 0);
    wr(0x3039, 1);                          /* 21 MHz */
    wreg(15, 0x8000);
    run_lines(3000);
}
#define RUN(scmr, ...) do { static const uint8_t c_[] = { __VA_ARGS__ }; run_code(c_, sizeof c_, scmr); } while (0)
#define R(n) gsu_reg(n)
#define SFR() gsu_read(0x3030)
#define FZ ((SFR() >> 1) & 1)
#define FCY ((SFR() >> 2) & 1)
#define FS ((SFR() >> 3) & 1)
#define FOV ((SFR() >> 4) & 1)
#define STOP 0x00, 0x01
#define IWT(n, v) (0xF0 | (n)), (uint8_t)(v), (uint8_t)((v) >> 8)
#define IBT(n, v) (0xA0 | (n)), (uint8_t)(v)
#define FROM(n) (0xB0 | (n))
#define TO(n) (0x10 | (n))
#define WITH(n) (0x20 | (n))
#define ALT1 0x3D
#define ALT2 0x3E
#define ALT3 0x3F

static void alu_tests(void)
{
    RUN(0, IWT(1, 0x1234), IBT(2, 0xFE), IWT(3, 0xFFFF), STOP);
    CHECKV("iwt", R(1), 0x1234); CHECKV("ibt sign", R(2), 0xFFFE); CHECKV("iwt 3", R(3), 0xFFFF);
    CHECK("stop clears go", !gsu_go());

    RUN(0, IWT(1, 0x7FFF), IWT(2, 1), FROM(1), TO(3), 0x52, STOP);           /* ADD R2 */
    CHECKV("add", R(3), 0x8000); CHECK("add flags", FOV && FS && !FCY && !FZ);
    RUN(0, IWT(1, 0xFFFF), IWT(2, 1), FROM(1), TO(3), 0x52, STOP);
    CHECKV("add carry", R(3), 0); CHECK("add carry flags", FCY && FZ && !FOV && !FS);
    RUN(0, IWT(1, 5), IBT(2, 0), FROM(1), TO(3), ALT2, 0x57, STOP);          /* ADD #7 */
    CHECKV("add imm", R(3), 12);
    RUN(0, IWT(1, 0xFFFF), IWT(2, 1), FROM(1), TO(3), 0x52, IWT(1, 10), IWT(2, 20), FROM(1), TO(4), ALT1, 0x52, STOP);   /* ADC R2 with CY = 1 */
    CHECKV("adc", R(4), 31);
    RUN(0, IWT(1, 10), IWT(2, 3), FROM(1), TO(3), 0x62, STOP);               /* SUB R2 */
    CHECKV("sub", R(3), 7); CHECK("sub flags", FCY && !FZ && !FS && !FOV);
    RUN(0, IWT(1, 3), IWT(2, 10), FROM(1), TO(3), 0x62, STOP);
    CHECKV("sub borrow", R(3), 0xFFF9); CHECK("sub borrow flags", !FCY && FS);
    RUN(0, IWT(1, 0x8000), IWT(2, 1), FROM(1), TO(3), 0x62, STOP);
    CHECKV("sub ov", R(3), 0x7FFF); CHECK("sub ov flags", FOV && !FS && FCY);
    RUN(0, IWT(1, 9), IWT(2, 9), FROM(1), TO(3), ALT3, 0x62, STOP);          /* CMP R2: no result */
    CHECKV("cmp keeps rd", R(3), 0); CHECK("cmp flags", FZ && FCY);
    RUN(0, IWT(1, 9), IWT(2, 0), FROM(1), TO(3), 0x62, IWT(1, 20), IWT(2, 5), FROM(1), TO(4), ALT1, 0x62, STOP);   /* SBC with CY = 1: 20 - 5 */
    CHECKV("sbc", R(4), 15);
    RUN(0, IWT(1, 0x0FF0), IWT(2, 0x00FF), FROM(1), TO(3), 0x72, FROM(1), TO(4), ALT1, 0x72, STOP);
    CHECKV("and", R(3), 0x00F0); CHECKV("bic", R(4), 0x0F00);
    RUN(0, IWT(1, 0x0FF0), FROM(1), TO(3), ALT2, 0x7C, FROM(1), TO(4), ALT3, 0x7C, STOP);   /* AND #12, BIC #12 */
    CHECKV("and imm", R(3), 0); CHECKV("bic imm", R(4), 0x0FF0);
    RUN(0, IWT(1, 0x00F0), IWT(2, 0x0F00), FROM(1), TO(3), 0xC2, FROM(1), TO(4), ALT1, 0xC2, STOP);
    CHECKV("or", R(3), 0x0FF0); CHECKV("xor", R(4), 0x0FF0);
    RUN(0, IWT(1, 0x00FF), FROM(1), TO(3), ALT2, 0xC3, FROM(1), TO(4), ALT3, 0xC3, STOP);
    CHECKV("or imm", R(3), 0xFF); CHECKV("xor imm", R(4), 0xFC);
    RUN(0, IWT(1, 0x00FF), FROM(1), TO(3), 0x4F, STOP);
    CHECKV("not", R(3), 0xFF00); CHECK("not flags", FS && !FZ);
    RUN(0, IWT(1, 0x8001), FROM(1), TO(3), 0x03, STOP);
    CHECKV("lsr", R(3), 0x4000); CHECK("lsr flags", FCY && !FS);
    RUN(0, IWT(1, 0x8001), FROM(1), TO(3), 0x96, STOP);
    CHECKV("asr", R(3), 0xC000); CHECK("asr flags", FCY && FS);
    RUN(0, IWT(1, 0xFFFF), FROM(1), TO(3), ALT1, 0x96, STOP);
    CHECKV("div2 -1", R(3), 0); CHECK("div2 flags", FZ && FCY);
    RUN(0, IWT(1, 0xFFFC), FROM(1), TO(3), ALT1, 0x96, STOP);
    CHECKV("div2", R(3), 0xFFFE);
    RUN(0, IWT(1, 0x8001), FROM(1), TO(3), 0x04, STOP);                      /* ROL, CY = 0 */
    CHECKV("rol", R(3), 0x0002); CHECK("rol flags", FCY);
    RUN(0, IWT(1, 0x8001), FROM(1), TO(3), 0x97, STOP);                      /* ROR, CY = 0 */
    CHECKV("ror", R(3), 0x4000); CHECK("ror flags", FCY);
    RUN(0, IWT(1, 0x1280), FROM(1), TO(3), 0x4D, FROM(1), TO(4), 0x95, FROM(1), TO(5), 0x9E, FROM(1), TO(6), 0xC0, STOP);
    CHECKV("swap", R(3), 0x8012); CHECKV("sex", R(4), 0xFF80); CHECKV("lob", R(5), 0x80); CHECKV("hib", R(6), 0x12);
    RUN(0, IWT(7, 0xAB00), IWT(8, 0xCD00), TO(3), 0x70, STOP);
    CHECKV("merge", R(3), 0xABCD); CHECK("merge flags", FS && FOV && FCY && FZ);
    RUN(0, IWT(7, 0x0100), IWT(8, 0x0100), TO(3), 0x70, STOP);
    CHECKV("merge 2", R(3), 0x0101); CHECK("merge 2 flags", !FS && !FOV && !FCY && !FZ);
    RUN(0, IWT(1, 0x1000), 0xD1, 0xD1, 0xE1, STOP);                           /* INC INC DEC */
    CHECKV("inc dec", R(1), 0x1001);
    RUN(0, IWT(1, 0x1234), WITH(1), TO(3), IWT(2, 0x9999), STOP);              /* MOVE R3,R1 (WITH R1, TO R3) */
    CHECKV("move", R(3), 0x1234);
    RUN(0, IWT(1, 0x0080), WITH(1), FROM(2), STOP);                            /* MOVES with B: R1 = R2 (0), flags */
    CHECKV("moves", R(1), 0); CHECK("moves flags", FZ);
    RUN(0, IBT(1, 0xFD), IBT(2, 7), FROM(1), TO(3), 0x82, FROM(1), TO(4), ALT1, 0x82, STOP);   /* MULT R2, UMULT R2 */
    CHECKV("mult", R(3), 0xFFEB); CHECKV("umult", R(4), 0xFD * 7);
    RUN(0, IBT(1, 0xFD), FROM(1), TO(3), ALT2, 0x83, FROM(1), TO(4), ALT3, 0x83, STOP);        /* MULT #3, UMULT #3 */
    CHECKV("mult imm", R(3), 0xFFF7); CHECKV("umult imm", R(4), 0xFD * 3);
    RUN(0, IWT(1, 0x4000), IWT(6, 0x0004), FROM(1), TO(3), 0x9F, STOP);       /* FMULT: 0x4000 * 4 >> 16 = 1 */
    CHECKV("fmult", R(3), 1);
    RUN(0, IWT(1, 0x4000), IWT(6, 0x0004), FROM(1), TO(3), ALT1, 0x9F, STOP); /* LMULT: 0x10000 = hi 1, lo 0 */
    CHECKV("lmult hi", R(3), 1); CHECKV("lmult lo", R(4), 0);
    RUN(0, IWT(1, 0xFFFF), IWT(6, 0x0003), FROM(1), TO(3), ALT1, 0x9F, STOP); /* -1 * 3 = -3 */
    CHECKV("lmult neg hi", R(3), 0xFFFF); CHECKV("lmult neg lo", R(4), 0xFFFD);
}

static void flow_tests(void)
{
    /* the delay slot: the byte after BRA runs, the next one is skipped */
    RUN(0, IBT(1, 0), 0x05, 0x02, 0xD1, 0xD1, STOP);
    CHECKV("bra delay slot", R(1), 1);
    /* BNE not taken, BEQ taken */
    RUN(0, IBT(1, 0), IBT(2, 0), FROM(2), 0xD2, 0xE2, 0x08, 0x02, 0xD1, 0xD1, STOP);   /* Z = 1: BNE falls through, both INC R1 run */
    CHECKV("bne not taken", R(1), 2);
    RUN(0, IBT(1, 0), 0xD2, 0x08, 0x02, 0xD1, 0xD1, STOP);                    /* INC R2 clears Z: BNE taken, one INC R1 in the slot */
    CHECKV("bne taken", R(1), 1);
    RUN(0, IBT(1, 0), IBT(2, 5), IBT(3, 5), FROM(2), ALT3, 0x63, 0x09, 0x02, 0xD1, 0xD1, STOP);   /* CMP equal: BEQ taken */
    CHECKV("beq taken", R(1), 1);
    RUN(0, IBT(1, 0), IWT(2, 1), IWT(3, 2), FROM(2), ALT3, 0x63, 0x07, 0x02, 0xD1, 0xD1, STOP);   /* 1 < 2: BLT taken */
    CHECKV("blt taken", R(1), 1);
    RUN(0, IBT(1, 0), IWT(2, 2), IWT(3, 1), FROM(2), ALT3, 0x63, 0x06, 0x02, 0xD1, 0xD1, STOP);   /* 2 >= 1: BGE taken */
    CHECKV("bge taken", R(1), 1);
    RUN(0, IBT(1, 0), IWT(2, 3), 0x0D, 0x02, 0xD1, 0xD1, STOP);               /* CY from IWT is 0: BCS not taken: two INC */
    CHECKV("bcs not taken", R(1), 2);
    /* a backward branch: count R1 to 3 with BNE */
    RUN(0, IBT(1, 0), IBT(2, 3), 0xD1, 0xE2, 0x08, 0xFC, 0x01, STOP);
    CHECKV("loop with bne", R(1), 3);
    /* LOOP with MOVE R13,R15 */
    RUN(0, IWT(12, 5), IBT(1, 0), WITH(15), TO(13), 0xD1, 0x3C, 0x01, STOP);
    CHECKV("loop", R(1), 5); CHECKV("loop r12", R(12), 0);
    /* JMP and LINK: a call with a delay byte */
    RUN(0, IWT(8, 0x800F), 0x94, 0x98, 0xD1, 0xD1, 0xD1, STOP, 0x01, 0x01, 0x01, 0x01, 0x01, IBT(2, 9), 0x9B, 0x01);
    /* LINK #4 at offset 3 has R15 = $8004 (the next opcode), so R11 = $8008; JMP R8 goes to $800F after the slot (INC R1) */
    CHECKV("jmp", R(2), 9); CHECKV("link", R(11), 0x8008); CHECKV("jmp slot", R(1), 1);
    /* LJMP into the ROM, the cache follows */
    RUN(0, IWT(9, 0), IWT(1, 0x800E), FROM(1), ALT1, 0x99, 0x01, STOP, 0x01, 0x01, IBT(5, 0x2A), STOP);
    /* LJMP R9 to $800E (the IBT); without the jump the program would stop at the first STOP */
    CHECKV("ljmp", R(5), 0x2A);
    /* the prefix state: a branch keeps ALT1 for the opcode after the slot */
    RUN(0, IWT(1, 0x00FF), FROM(1), TO(3), ALT1, 0x05, 0x01, 0x72, 0x01, STOP);   /* ALT1, BRA +1: the delay slot is BIC R2 with the ALT1 kept: R3 = R1 & ~R2 */
    CHECKV("prefix survives bra", R(3), 0xFF);
}

static void ram_tests(void)
{
    RUN(0, IWT(1, 0x1234), IWT(5, 0x0100), FROM(1), 0x35, IWT(6, 0x0102), FROM(1), ALT1, 0x36, STOP);   /* STW (R5), STB (R6) */
    CHECKV("stw lo", S.sram[0x100], 0x34); CHECKV("stw hi", S.sram[0x101], 0x12); CHECKV("stb", S.sram[0x102], 0x34); CHECKV("stb no hi", S.sram[0x103], 0);
    CHECK("ram dirty", S.sram_dirty);
    RUN(0, IWT(5, 0x0100), TO(3), 0x45, TO(4), ALT1, 0x45, STOP);              /* LDW, LDB: RAM is zero after a load */
    CHECKV("ldw zero", R(3), 0);
    RUN(0, IWT(1, 0xBEEF), IWT(5, 0x0200), FROM(1), 0x35, TO(3), 0x45, TO(4), ALT1, 0x45, STOP);
    CHECKV("ldw", R(3), 0xBEEF); CHECKV("ldb", R(4), 0xEF);
    RUN(0, IWT(1, 0xCAFE), ALT2, 0xF1, 0x40, 0x00, ALT1, 0xF2, 0x40, 0x00, STOP);   /* SM (0040),R1 ; LM R2,(0040) */
    CHECKV("sm", S.sram[0x40], 0xFE); CHECKV("lm", R(2), 0xCAFE);
    RUN(0, IWT(1, 0xF00D), ALT2, 0xA1, 0x10, ALT1, 0xA2, 0x10, STOP);          /* SMS (10*2),R1 ; LMS R2,(10*2) */
    CHECKV("sms", S.sram[0x20], 0x0D); CHECKV("lms", R(2), 0xF00D);
    RUN(0, IWT(1, 0x5A5A), IWT(5, 0x0300), FROM(1), 0x35, FROM(1), 0x90, STOP); /* SBK stores at the last address */
    CHECKV("sbk", S.sram[0x300], 0x5A);
    /* RAMB: the second 64 KB of the RAM; with a 64 KB RAM it mirrors the first one */
    RUN(0, IWT(1, 0x1111), IWT(5, 0x0400), FROM(1), 0x35, IBT(2, 1), FROM(2), ALT2, 0xDF, TO(3), 0x45, STOP);
    CHECKV("ramb mirror", R(3), 0x1111);
}

static void rom_tests(void)
{
    RUN(0, IWT(14, 0x8010), TO(3), 0xEF, STOP);                                 /* GETB at ROMBR:R14 */
    CHECKV("getb", R(3), rom[0x10]);
    RUN(0, IWT(14, 0x8010), IWT(1, 0x1234), FROM(1), TO(3), ALT1, 0xEF, FROM(1), TO(4), ALT2, 0xEF, FROM(1), TO(5), ALT3, 0xEF, STOP);
    CHECKV("getbh", R(3), (rom[0x10] << 8) | 0x34); CHECKV("getbl", R(4), 0x1200 | rom[0x10]);
    CHECKV("getbs", R(5), (uint16_t)(int16_t)(int8_t)rom[0x10]);
    RUN(0, IBT(1, 1), FROM(1), ALT3, 0xDF, IWT(14, 0x8010), TO(3), 0xEF, STOP);   /* ROMB 1 (then R14: the buffer reloads): bank 1 is $8000 in the ROM */
    CHECKV("romb", R(3), rom[0x8000 + 0x10]);
    /* ROMB without a new R14: GETB returns the byte buffered from the old bank (the buffer loads when R14 is written; Doom relies on it) */
    RUN(0, IWT(14, 0x8010), IBT(1, 1), FROM(1), ALT3, 0xDF, TO(3), 0xEF, STOP);
    CHECKV("romb keeps buffer", R(3), rom[0x10]);
    RUN(0, IWT(14, 0x8010), IBT(1, 1), FROM(1), ALT3, 0xDF, IWT(14, 0x8011), TO(3), 0xEF, STOP);   /* R14 written after ROMB: the new bank */
    CHECKV("romb then r14", R(3), rom[0x8000 + 0x11]);
    /* R14 written after ROMB with the SAME value reloads the buffer from the new bank (Yoshi's Island: ROMB, then MOVES R14, then GETB; the old rule kept the stale byte) */
    RUN(0, IWT(14, 0x8010), TO(2), 0xEF, IBT(1, 1), FROM(1), ALT3, 0xDF, IWT(14, 0x8010), TO(3), 0xEF, STOP);
    CHECKV("romb then same r14 (IWT)", R(3), rom[0x8000 + 0x10]);
    RUN(0, IWT(14, 0x8010), TO(2), 0xEF, IBT(1, 1), FROM(1), ALT3, 0xDF, WITH(14), TO(14), TO(3), 0xEF, STOP);
    CHECKV("romb then same r14 (MOVES)", R(3), rom[0x8000 + 0x10]);
    RUN(0, IWT(14, 0x8010), TO(2), 0xEF, IBT(1, 1), FROM(1), ALT3, 0xDF, IWT(5, 0x8010), WITH(5), TO(14), TO(3), 0xEF, STOP);
    CHECKV("romb then same r14 (MOVE)", R(3), rom[0x8000 + 0x10]);
    /* the 64 KB banks $40-$5F: the same bytes */
    RUN(0, IBT(1, 0x40), FROM(1), ALT3, 0xDF, IWT(14, 0x0012), TO(3), 0xEF, STOP);
    CHECKV("rom bank 40", R(3), rom[0x12]);
}

static void plot_tests(void)
{
    /* 4 bpp, 128 lines (SCMR = 1): the pixel (0,0) = 5 and (1,0) = 5 */
    RUN(1, IBT(3, 5), FROM(3), 0x4E, 0x4C, 0x4C, STOP);
    CHECKV("plot 4bpp p0", S.sram[0], 0xC0); CHECKV("plot 4bpp p1", S.sram[1], 0); CHECKV("plot 4bpp p2", S.sram[16], 0xC0); CHECKV("plot 4bpp p3", S.sram[17], 0);
    CHECKV("plot r1", R(1), 2);
    /* a row of 8 pixels with colour 15, then a pixel in another row; the merge keeps the old bits */
    RUN(1, IBT(3, 15), FROM(3), 0x4E, IBT(12, 8), WITH(15), TO(13), 0x4C, 0x3C, 0x01, IBT(1, 3), IBT(2, 1), IBT(3, 2), FROM(3), 0x4E, 0x4C, STOP);
    CHECKV("row full p0", S.sram[0], 0xFF); CHECKV("row full p3", S.sram[17], 0xFF);
    CHECKV("row 1 p0", S.sram[2], 0); CHECKV("row 1 p1", S.sram[3], 0x10);
    /* 2 bpp: the tile at x = 8 is the tile 16, 16 bytes each */
    RUN(0, IBT(1, 8), IBT(3, 3), FROM(3), 0x4E, 0x4C, STOP);
    CHECKV("plot 2bpp", S.sram[0x100], 0x80); CHECKV("plot 2bpp p1", S.sram[0x101], 0x80);
    /* 8 bpp (SCMR = 3), colour $A5 at (0,0): planes 0,2,5,7 */
    RUN(3, IBT(3, 0xA5), FROM(3), 0x4E, 0x4C, STOP);
    CHECKV("plot 8bpp p0", S.sram[0], 0x80); CHECKV("plot 8bpp p1", S.sram[1], 0); CHECKV("plot 8bpp p2", S.sram[16], 0x80);
    CHECKV("plot 8bpp p5", S.sram[33], 0x80); CHECKV("plot 8bpp p7", S.sram[49], 0x80); CHECKV("plot 8bpp p4", S.sram[32], 0);
    /* 160 lines (HT0, SCMR = 4 | 1): the tile of x = 8 is the tile $14 (32 bytes each) */
    RUN(0x05, IBT(1, 8), IBT(3, 1), FROM(3), 0x4E, 0x4C, STOP);
    CHECKV("plot 160", S.sram[0x14 * 32], 0x80);
    /* 192 lines (HT1): the tile of x = 8 is the tile $18 */
    RUN(0x21, IBT(1, 8), IBT(3, 1), FROM(3), 0x4E, 0x4C, STOP);
    CHECKV("plot 192", S.sram[0x18 * 32], 0x80);
    /* OBJ mode: x = $80 is the tile $100 */
    RUN(0x25, IWT(1, 0x80), IBT(3, 1), FROM(3), 0x4E, 0x4C, STOP);
    CHECKV("plot obj", S.sram[0x100 * 32], 0x80);
    /* POR bit 4 forces the OBJ layout whatever SCMR says (Yoshi's Island menu icons): x = $80 is the tile $100 */
    RUN(1, IBT(4, 0x10), FROM(4), ALT1, 0x4E, IWT(1, 0x80), IBT(3, 1), FROM(3), 0x4E, 0x4C, STOP);
    CHECKV("plot por obj", S.sram[0x100 * 32], 0x80);
    /* the screen base */
    wr(0x3038, 0);
    load_rom((const uint8_t[]){ IBT(3, 1), FROM(3), 0x4E, 0x4C, STOP }, 7);
    wr(0x303A, 1); wr(0x3038, 2); wr(0x3039, 1); wreg(15, 0x8000); run_lines(100);
    CHECKV("scbr", S.sram[2 * 1024], 0x80);
    /* transparent colour 0 is not drawn; with CMODE bit 0 it is */
    RUN(1, IBT(3, 15), FROM(3), 0x4E, 0x4C, IBT(1, 0), IBT(3, 0), FROM(3), 0x4E, 0x4C, STOP);
    CHECKV("transparent", S.sram[0], 0x80);
    RUN(1, IBT(3, 15), FROM(3), 0x4E, 0x4C, IBT(1, 0), IBT(3, 1), FROM(3), ALT1, 0x4E, IBT(3, 0), FROM(3), 0x4E, 0x4C, STOP);
    CHECKV("not transparent", S.sram[0], 0);
    /* RPIX */
    RUN(1, IBT(3, 9), FROM(3), 0x4E, IBT(1, 5), 0x4C, IBT(1, 5), TO(4), ALT1, 0x4C, STOP);
    CHECKV("rpix", R(4), 9);
    /* dither (POR bit 1): the colour $35 is 5 at (0,0) and 3 at (1,0) */
    RUN(1, IBT(4, 2), FROM(4), ALT1, 0x4E, IBT(3, 0x35), FROM(3), 0x4E, 0x4C, 0x4C, STOP);
    CHECKV("dither p0", S.sram[0], 0xC0); CHECKV("dither p1", S.sram[1], 0x40); CHECKV("dither p2", S.sram[16], 0x80); CHECKV("dither p3", S.sram[17], 0);
}

static void cache_irq_tests(void)
{
    /* code uploaded by the SNES into the cache: SFR GO = 0, the lines at $3100, R15 = 0 */
    load_rom((const uint8_t[]){ 0 }, 1);
    wr(0x3030, 0);
    const uint8_t prog[16] = { IWT(1, 0x4321), IBT(2, 0x11), 0x02, STOP };
    for (int i = 0; i < 16; i++) wr(0x3100 + i, prog[i]);
    wr(0x303A, 0x00); wr(0x3039, 0);                                             /* 10 MHz */
    wreg(15, 0);
    CHECK("running", gsu_go());
    run_lines(10);
    CHECKV("cache run r1", R(1), 0x4321); CHECKV("cache run r2", R(2), 0x11);
    CHECKV("cache base", gsu_read(0x303E), 0); CHECK("stopped", !gsu_go());
    CHECKV("cache read back", gsu_read(0x3100 + 3), prog[3]);
    /* IRQ at STOP, cleared by reading SFR high */
    S.irq_pending = 0;
    load_rom((const uint8_t[]){ STOP }, 2);
    wr(0x3037, 0); wr(0x303A, 0x18); wreg(15, 0x8000); run_lines(10);
    CHECK("irq line", gsu_irq_line() && S.irq_pending);
    CHECK("sfr irq bit", (snes_read(0x003031) & 0x80) != 0);
    CHECK("irq cleared", !gsu_irq_line() && !S.irq_pending);
    CHECK("sfr irq bit gone", (snes_read(0x003031) & 0x80) == 0);
    /* the mask */
    load_rom((const uint8_t[]){ STOP }, 2);
    wr(0x3037, 0x80); wr(0x303A, 0x18); wreg(15, 0x8000); run_lines(10);
    CHECK("irq masked", !gsu_irq_line() && !S.irq_pending);
    /* a program that never ends is stopped by a write of GO = 0 */
    load_rom((const uint8_t[]){ 0x05, 0xFE, 0x01 }, 3);      /* BRA -2: back to the BRA, the slot is NOP */
    wr(0x303A, 0x18); wreg(15, 0x8000); gsu_run(1364);
    CHECK("still running", gsu_go());
    wr(0x3030, 0);
    CHECK("stopped by the SNES", !gsu_go());
}

/* The lazy run (snes_bus.c): the GSU runs when the CPU touches it, not in steps of its own. snes_read and snes_write advance snes_clk, so a loop of reads is
 * a CPU that waits. */
static void lazy_tests(void)
{
    /* R1 = $5A5A, R2 = $0010, STW (R2): the word goes to RAM $0010, then STOP; a long delay loop first so that the job lasts some lines */
    static const uint8_t prog[] = { IWT(12, 400), IWT(13, 0x8006), 0x01, 0x3C, 0x01, 0x01, IWT(1, 0x5A5A), IWT(2, 0x0010), FROM(1), 0x32, STOP };
    const uint32_t page = 0x700000u >> 12;
    load_rom(prog, sizeof prog);
    wr(0x303A, 0x18); wr(0x3039, 1); wr(0x3037, 0x80);
    snes_write(0x700010, 0); snes_write(0x700011, 0);
    CHECK("cart ram pages on at rest", snes_rtab[page] != NULL);
    wreg(15, 0x8000);
    CHECK("cart ram pages off while it runs", snes_rtab[page] == NULL && snes_rtab[0x006000u >> 12] == NULL);
    int polls = 0;
    while ((snes_read(0x003030) & 0x20) && polls < 100000) polls++;
    CHECK("a poll of the status sees the end", polls < 100000 && !gsu_go());
    CHECK("a poll takes more than one read (the status is as old as the last run)", polls > 1);
    CHECKV("the job ran: RAM", snes_read(0x700010), 0x5A);
    CHECK("cart ram pages on again", snes_rtab[page] != NULL && snes_rtab[0x006000u >> 12] != NULL);
    /* the same, polling the RAM: the read goes the slow way while the GSU runs and catches the store */
    snes_write(0x700010, 0);
    wreg(15, 0x8000);
    polls = 0;
    while (snes_read(0x700010) != 0x5A && polls < 100000) polls++;
    CHECK("a poll of the RAM sees the store", polls < 100000);
    /* a read of the vectors brings the GSU up to date first (the pattern is for a running GSU that owns the ROM) */
    snes_write(0x700010, 0);
    wr(0x303A, 0x18);
    wreg(15, 0x8000);
    for (int i = 0; i < 3000; i++) (void)snes_read(0x00FFEA);
    CHECK("vector reads end the job", !gsu_go());
}

/* HDMA switched on during the picture (Yoshi's Island does it a few lines after the top of the frame): the channel is set up at once and runs from the next line */
static void hdma_mid_tests(void)
{
    load_rom((const uint8_t[]){ STOP }, 2);
    static const uint8_t tbl[] = { 0x01, 0x11, 0x01, 0x22, 0x00 };           /* 1 line: $11, 1 line: $22, end */
    for (unsigned i = 0; i < sizeof tbl; i++) wr(0x7E0100 + i, tbl[i]);
    wr(0x4370, 0x00); wr(0x4371, 0x07); wr(0x4372, 0x00); wr(0x4373, 0x01); wr(0x4374, 0x7E);     /* channel 7: direct, one register, $2107 */
    S.hdmaen = 0; S.line = 10; S.lines = 224;
    wr(0x420C, 0x80);
    snes_hdma_line();
    CHECKV("hdma enabled mid-frame: first line", S.sc[0], 0x11);
    snes_hdma_line();
    CHECKV("hdma enabled mid-frame: second line", S.sc[0], 0x22);
    S.hdmaen = 0; S.line = 0;                                                  /* at line 0 a channel is not set up by the write: the frame start does it */
    S.sc[0] = 0;
    wr(0x420C, 0x80);
    snes_hdma_line();
    CHECKV("hdma enabled at line 0 waits for the frame start", S.sc[0], 0);
    S.hdmaen = 0;
}

static void map_tests(void)
{
    load_rom((const uint8_t[]){ STOP }, 2);
    CHECK("gsu cart found", S.gsu == 1 && S.sram_size == 65536 && !S.gsu_battery);
    CHECKV("rom lo bank", snes_read(0x028005), rom[0x10005]);
    CHECKV("rom 64k bank", snes_read(0x410005), rom[0x10005]);
    CHECKV("rom c0", snes_read(0xC00007), rom[7]);
    CHECKV("rom 80", snes_read(0x808007), rom[7]);
    snes_write(0x701234, 0x77);
    CHECKV("ram 70", S.sram[0x1234], 0x77); CHECKV("ram f0", snes_read(0xF01234), 0x77);
    CHECKV("ram 71 mirror", snes_read(0x711234), 0x77);
    snes_write(0x006005, 0x66);
    CHECKV("ram 6000", S.sram[5], 0x66); CHECKV("ram 6000 read", snes_read(0x006005), 0x66);
    CHECKV("ram 6000 mirror", snes_read(0x306005), 0x66);
    snes_write(0x008000, 0x99);
    CHECKV("rom write ignored", snes_read(0x008000), rom[0]);
    for (uint32_t a = 0; a < 0x1000000; a += 0x777) {            /* the page tables give what raw_read gives */
        const unsigned pg = (a >> 12) & 0xFFF;
        if (!snes_rtab[pg]) continue;
        if (snes_read_f(a) != snes_read(a)) { CHECKV("page table equals bus", a, 0); break; }
    }
    /* the registers */
    wreg(3, 0xABCD);
    CHECKV("reg r3 lo", snes_read(0x003006), 0xCD); CHECKV("reg r3 hi", snes_read(0x003007), 0xAB);
    CHECKV("vcr", snes_read(0x00303B), 4);
    CHECKV("mirror regs", snes_read(0x003046), 0xCD);
    /* the vector pattern while the GSU owns the ROM */
    load_rom((const uint8_t[]){ 0x05, 0xFE, 0x01 }, 3);
    wr(0x303A, 0x10); wreg(15, 0x8000);
    CHECK("go", gsu_go());
    CHECKV("vec nmi", snes_read(0x00FFEA), 0x08); CHECKV("vec nmi hi", snes_read(0x00FFEB), 0x01);
    CHECKV("vec irq", snes_read(0x00FFEE), 0x0C); CHECKV("vec cop", snes_read(0x00FFE4), 0x04); CHECKV("vec reset", snes_read(0x00FFFC), 0x00);
    wr(0x3030, 0);
    CHECKV("vec free", snes_read(0x00FFEB), rom[0x7FEB]);
    /* a battery chipset and the RAM size from the header */
    load_rom((const uint8_t[]){ STOP }, 2);
    rom[0x7FD6] = 0x15; rom[0x7FD8] = 5;
    snes_load(rom, sizeof rom, NULL);
    CHECK("battery", S.gsu && S.gsu_battery && S.sram_size == 32768);
    { uint32_t sz = 0; snes_sram(&sz); CHECKV("sram size seen", sz, 32768); }
    rom[0x7FD6] = 0x13; rom[0x7FD8] = 0;
    snes_load(rom, sizeof rom, NULL);
    { uint32_t sz = 1; snes_sram(&sz); CHECKV("no battery no save", sz, 0); }
    /* a plain LoROM cart is not a GSU cart */
    rom[0x7FD6] = 0x00;
    snes_load(rom, sizeof rom, NULL);
    CHECK("plain cart", !S.gsu);
}

/* a deterministic program: plots, mult, stores, a loop of 200; the hash of the RAM must not change with a speed-up */
static const uint8_t golden_code[] = {
    0x02, IWT(12, 200), IBT(1, 0), IBT(2, 0), IWT(3, 1), WITH(15), TO(13),
    FROM(3), 0x4E, 0x4C, 0xD2, 0xD3, FROM(3), TO(4), 0x83, FROM(4), 0x35, 0xD5, 0xD5, 0x3C, 0x01, STOP
};
static uint32_t fnv(const uint8_t *p, size_t n) { uint32_t h = 2166136261u; while (n--) h = (h ^ *p++) * 16777619u; return h; }

static void golden_test(void)
{
    load_rom(golden_code, sizeof golden_code);
    wr(0x303A, 1); wr(0x3039, 1); wreg(15, 0x8000);
    run_lines(3000);
    const uint32_t h = fnv(S.sram, 65536);
    CHECK("golden program ends", !gsu_go());
    CHECKV("golden hash", h, 0x2F6B2706u);
    if (getenv("GSU_PRINT")) printf("golden hash %08x instr %u\n", h, gsu_instr_count());
}

static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + t.tv_nsec * 1e-9; }

/* the speed of the interpreter on a mix of plots, multiplies, RAM moves and loops (about 20 instructions a turn) */
static void bench(void)
{
    static const uint8_t code[] = {
        0x02, IWT(12, 20000), IBT(1, 0), IBT(2, 0), IWT(3, 1), IWT(5, 0), IWT(14, 0x8100), WITH(15), TO(13),
        FROM(3), 0x4E, 0x4C, 0xD2, 0xD3, FROM(3), TO(4), 0x83, FROM(4), 0x35, 0xD5, 0xD5,
        TO(6), 0xEF, 0xDE, 0xD7, 0x56, FROM(6), TO(7), 0x57, 0x3C, 0x01, STOP
    };
    load_rom(code, sizeof code);
    uint32_t instr = 0; double t = 0; int frames = 0;
    for (int rep = 0; rep < 5; rep++) {
        wr(0x303A, 0x19); wr(0x3039, 1); wreg(15, 0x8000);
        const uint32_t i0 = gsu_instr_count();
        const double a = now_s();
        int lines = 0;
        while (gsu_go()) { gsu_run(1364); lines++; }
        t += now_s() - a; instr += gsu_instr_count() - i0; frames += lines;
    }
    printf("gsu bench: %u instructions in %.1f ms: %.1f M instr/s; a frame of 350000 instr costs %.2f ms; %d lines (%.1f frames) at 21 MHz\n",
           instr, t * 1000, instr / t / 1e6, 350000 / (instr / t) * 1000, frames, frames / 262.0);
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "bench")) { bench(); return 0; }
    alu_tests();
    flow_tests();
    ram_tests();
    rom_tests();
    plot_tests();
    cache_irq_tests();
    lazy_tests();
    hdma_mid_tests();
    map_tests();
    golden_test();
    printf("gsu: %d checks, %d failed\n", count, fails);
    return fails ? 1 : 0;
}
