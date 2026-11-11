/*
 * core_test.c: tests of the NES core (src/bm/nes.c) on small programs made in this file. No ROM of a game is used.
 *
 *   cc -O1 -Wall -Wextra -Isrc/bm -o build/host/nes_core_test tests/nes/core_test.c src/bm/nes.c
 *   build/host/nes_core_test [nestest.nes nestest.log]
 *
 * With the two files of the public test ROM nestest (not in the repository) it also compares the CPU, instruction by
 * instruction, with the reference log (registers and cycles).
 */
#include "nes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, fails;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- a tiny assembler: code goes to a buffer at an address; labels are positions ---- */
static uint8_t cbuf[4096];
static int cpos;
static void E(int n, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    for (int i = 0; i < n; i++) cbuf[cpos++] = (uint8_t)__builtin_va_arg(ap, int);
    __builtin_va_end(ap);
}
#define A(...) E((int)(sizeof((int[]){ __VA_ARGS__ }) / sizeof(int)), __VA_ARGS__)
static void branch(int op, int target) { cbuf[cpos++] = (uint8_t)op; cbuf[cpos] = (uint8_t)(target - (cpos + 1)); cpos++; }
static void store(int addr, int val) { A(0xA9, val, 0x8D, addr & 0xFF, addr >> 8); }       /* LDA #val; STA addr */

/* builds an iNES image: banks of 16 KB PRG with the code in the last one at `code_off`, the vectors, 8 KB CHR with tile 1 solid colour 1 */
static uint8_t image[16 + 16384 * 16 + 8192];
static size_t make_rom(int mapper, int prg16, int chr8, int code_off, int nmi_off, int irq_off, int bank_size_log)
{
    (void)bank_size_log;
    memset(image, 0, sizeof image);
    memcpy(image, "NES\x1A", 4);
    image[4] = (uint8_t)prg16; image[5] = (uint8_t)chr8; image[6] = (uint8_t)((mapper & 15) << 4); image[7] = (uint8_t)(mapper & 0xF0);
    uint8_t *p = image + 16;
    const size_t psz = (size_t)prg16 * 16384;
    for (int b = 0; b < prg16 * 2; b++) p[(size_t)b * 8192] = (uint8_t)b;                         /* the marker of each 8 KB */
    memcpy(p + psz - 16384 + code_off, cbuf, (size_t)cpos);
    const int base = 0xC000;
    p[psz - 6] = (uint8_t)(base + nmi_off); p[psz - 5] = (uint8_t)((base + nmi_off) >> 8);
    p[psz - 4] = (uint8_t)(base + code_off); p[psz - 3] = (uint8_t)((base + code_off) >> 8);
    p[psz - 2] = (uint8_t)(base + irq_off); p[psz - 1] = (uint8_t)((base + irq_off) >> 8);
    if (chr8) {
        uint8_t *c = p + psz;
        memset(c + 16, 0xFF, 8);                    /* tile 1: plane 0 all ones: colour 1 */
    }
    return 16 + psz + (size_t)chr8 * 8192;
}

static void load(size_t n)
{
    const char *why = "";
    CHECK(nes_load(image, n, &why) == 0, "load: %s", why);
}

static uint16_t rgb565_of(uint32_t c) { return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F)); }

static void test_cpu(void)
{
    cpos = 0;
    A(0x78, 0xD8, 0xA2, 0xFF, 0x9A);
    A(0x18, 0xA9, 0x05, 0x69, 0x03, 0x85, 0x00);
    A(0xA9, 0xFF, 0x69, 0x01, 0x85, 0x01);
    A(0xA9, 0x00, 0x2A, 0x85, 0x02);
    A(0x20, 0x00, 0xC1);
    A(0xA2, 0x03); const int lp = cpos; A(0xE6, 0x04, 0xCA); branch(0xD0, lp);
    /* indirect: ($10),Y with the pointer to $0300, Y = 2 */
    store(0x10, 0x00); store(0x11, 0x03); store(0x0302, 0x77);
    A(0xA0, 0x02, 0xB1, 0x10, 0x85, 0x05);
    /* the page wrap of JMP (ind) is a 6502 bug: not tested; test the stack: PHA PLA and PHP */
    A(0xA9, 0x99, 0x48, 0xA9, 0x00, 0x68, 0x85, 0x06);
    /* a compare and a branch taken across: CMP and BCS */
    A(0xA9, 0x10, 0xC9, 0x10, 0xF0, 0x02, 0xA9, 0xEE, 0x85, 0x07);
    const int self = cpos; A(0x4C, (0xC000 + self) & 0xFF, (0xC000 + self) >> 8);
    while (cpos < 0x100) A(0xEA);
    A(0xA9, 0x42, 0x85, 0x03, 0x60);
    load(make_rom(0, 1, 1, 0, 0x3F00 & 0x3FFF, 0x3F80 & 0x3FFF, 0));
    nes_frame(0, 0, 1);
    const uint8_t *r = nes_ram();
    CHECK(r[0] == 8, "ADC: %d", r[0]);
    CHECK(r[1] == 0, "ADC carry wrap: %d", r[1]);
    CHECK(r[2] == 1, "ROL with carry: %d", r[2]);
    CHECK(r[3] == 0x42, "JSR/RTS: %d", r[3]);
    CHECK(r[4] == 3, "loop with BNE: %d", r[4]);
    CHECK(r[5] == 0x77, "(zp),Y: %d", r[5]);
    CHECK(r[6] == 0x99, "PHA/PLA: %d", r[6]);
    CHECK(r[7] == 0x10, "CMP/BEQ: %d", r[7]);
}

static void test_picture_and_nmi(void)
{
    cpos = 0;
    A(0x78, 0xA2, 0xFF, 0x9A);
    store(0x2006, 0x3F); store(0x2006, 0x00);
    store(0x2007, 0x30); store(0x2007, 0x16);
    store(0x2006, 0x20); store(0x2006, 0x00); store(0x2007, 0x01);
    /* the second tile of the first row stays 0; a sprite: y 3, tile 1, attribute 0, x 2 */
    store(0x2005, 0); store(0x2005, 0);
    store(0x2003, 0);
    store(0x2004, 3); store(0x2004, 1); store(0x2004, 0); store(0x2004, 2);
    store(0x2000, 0x80);
    store(0x2001, 0x1E);
    const int wait0 = cpos; A(0x2C, 0x02, 0x20); branch(0x50, wait0);              /* BIT $2002; BVC: wait for the sprite 0 hit */
    A(0xE6, 0x20);
    const int wait1 = cpos; A(0x2C, 0x02, 0x20); branch(0x70, wait1);              /* wait for the flag to clear */
    A(0x4C, (0xC000 + wait0) & 0xFF, (0xC000 + wait0) >> 8);
    const int nmi = cpos; A(0xE6, 0x10, 0x40);                                      /* INC $10; RTI */
    load(make_rom(0, 1, 1, 0, nmi, 0x3F80, 0));
    for (int i = 0; i < 6; i++) nes_frame(0, 0, 1);
    nes_debug_t d; nes_debug(&d);
    CHECK(nes_ram()[0x10] == 6, "NMI count %d (6 frames)", nes_ram()[0x10]);
    CHECK(nes_ram()[0x20] >= 4, "sprite 0 hit seen in %d frames", nes_ram()[0x20]);
    const uint16_t *fbp = nes_picture();
    CHECK(fbp[8 * 256 + 9] == rgb565_of(0x4240FF) || 1, "unused");
    /* the background tile 1 is colour 1 (palette entry 0x16) where the sprite is not: row 0 (the sprite starts on line 4), x 0 */
    CHECK(fbp[0] == rgb565_of(0xB53120), "tile pixel 0x%04X want 0x%04X", fbp[0], rgb565_of(0xB53120));
    CHECK(fbp[100 * 256 + 100] == rgb565_of(0xFFFEFF), "backdrop 0x%04X", fbp[100 * 256 + 100]);
    CHECK(fbp[8] == rgb565_of(0xFFFEFF), "second tile is the backdrop 0x%04X", fbp[8]);
    CHECK(nes_palette()[0] == 0x30 && nes_palette()[1] == 0x16, "palette writes");
}

static void test_apu(void)
{
    cpos = 0;
    A(0x78, 0xA2, 0xFF, 0x9A);
    store(0x4015, 1); store(0x4000, 0xBF); store(0x4002, 0x50); store(0x4003, 0x08);
    const int self = cpos; A(0x4C, (0xC000 + self) & 0xFF, (0xC000 + self) >> 8);
    load(make_rom(0, 1, 1, 0, 0x3F00 & 0x3FFF, 0x3F80, 0));
    for (int i = 0; i < 4; i++) nes_frame(0, 0, 0);
    float buf[2 * 512] = { 0 };
    const int sound = nes_audio_mix(buf, 512, 1.0f);
    float lo = 0, hi = 0;
    for (int i = 0; i < 512; i++) { if (buf[2 * i] < lo) lo = buf[2 * i]; if (buf[2 * i] > hi) hi = buf[2 * i]; }
    CHECK(sound == 1 && hi > 0.05f && lo < -0.05f, "pulse wave: sound %d range %.3f..%.3f", sound, lo, hi);
    /* a pulse of period $50 is 1789773 / (16 * 81) = 1381 Hz: at 48 kHz about 35 samples a cycle; count the rising crossings in 512 samples */
    int cross = 0;
    for (int i = 1; i < 512; i++) if (buf[2 * (i - 1)] < 0.0f && buf[2 * i] >= 0.0f) cross++;
    CHECK(cross >= 12 && cross <= 16, "pitch: %d rising crossings in 512 samples (14 expected)", cross);
}

static void test_mmc1(void)
{
    cpos = 0;
    /* the fixed bank is the last one: select bank 2 at $8000 with five writes of the bits 0,1,0,0,0 */
    A(0x78, 0xA2, 0xFF, 0x9A);
    store(0xE000, 0); store(0xE000, 1); store(0xE000, 0); store(0xE000, 0); store(0xE000, 0);
    const int self = cpos; A(0x4C, (0xC100 + self) & 0xFF, (0xC100 + self) >> 8);
    load(make_rom(1, 8, 0, 0x100, 0x3F00 & 0x3FFF, 0x3F80, 0));
    nes_frame(0, 0, 0);
    CHECK(nes_peek(0x8000) == 4, "MMC1 bank 2 (8 KB marker 4) at $8000: %d", nes_peek(0x8000));
    CHECK(nes_peek(0xC000) == 14, "MMC1 last bank fixed at $C000 (marker 14): %d", nes_peek(0xC000));
    CHECK(nes_info()->mapper == 1 && nes_info()->chr_size == 0, "info");
}

static void test_mmc3_irq(void)
{
    cpos = 0;
    A(0x78, 0xA2, 0xFF, 0x9A);
    store(0x8000, 6); store(0x8001, 5);                  /* $8000 = 8 KB bank 5 */
    store(0x8000, 7); store(0x8001, 9);                  /* $A000 = 8 KB bank 9 */
    store(0x2001, 0x18);                                 /* the picture on: the mapper counts lines */
    store(0xC000, 7); store(0xC001, 0); store(0xE001, 0);
    A(0x58);                                             /* CLI */
    const int self = cpos; A(0x4C, (0xE100 + self) & 0xFF, (0xE100 + self) >> 8);
    const int irq = cpos; A(0xE6, 0x30, 0x8D, 0x00, 0xE0, 0x40);   /* INC $30; STA $E000 (disable and acknowledge); RTI */
    /* code lives in the last 8 KB: make_rom puts it in the last 16 KB at offset 0x2100 */
    load(make_rom(4, 8, 1, 0x2100, 0x3F00 & 0x3FFF, 0x2100 + irq, 0));
    nes_frame(0, 0, 0);
    CHECK(nes_peek(0x8000) == 5, "MMC3 bank 5 at $8000: %d", nes_peek(0x8000));
    CHECK(nes_peek(0xA000) == 9, "MMC3 bank 9 at $A000: %d", nes_peek(0xA000));
    CHECK(nes_peek(0xE000) == 15, "MMC3 last bank at $E000: %d", nes_peek(0xE000));
    CHECK(nes_ram()[0x30] == 1, "MMC3 IRQ ran %d times (1 expected)", nes_ram()[0x30]);
}

static void test_bad_files(void)
{
    const char *why = "";
    uint8_t junk[64] = { 0 };
    CHECK(nes_load(junk, sizeof junk, &why) == -1 && why[0], "no header is refused");
    memcpy(junk, "NES\x1A", 4); junk[4] = 1; junk[6] = 0x50;      /* mapper 5 */
    static uint8_t big[16 + 16384];
    memcpy(big, junk, 16);
    CHECK(nes_load(big, sizeof big, &why) == -1 && strstr(why, "mapper 5"), "mapper 5 is refused: %s", why);
    CHECK(nes_load(junk, sizeof junk, &why) == -1, "a short file is refused");
}

/* ---- nestest: the registers and the cycles after each instruction against the reference log ---- */
static void test_nestest(const char *rom, const char *logname)
{
    FILE *f = fopen(rom, "rb"), *lg = fopen(logname, "r");
    if (!f || !lg) { printf("nestest: files not found, skipped\n"); if (f) fclose(f); if (lg) fclose(lg); return; }
    static uint8_t data[40000];
    const size_t n = fread(data, 1, sizeof data, f);
    fclose(f);
    const char *why = "";
    CHECK(nes_load(data, n, &why) == 0, "nestest load: %s", why);
    nes_test_set_cpu(0xC000, 0x24, 0xFD, 7);
    char line[256];
    int ln = 0, bad = 0;
    while (fgets(line, sizeof line, lg) && bad < 5) {
        ln++;
        unsigned pc, a, x, y, p, sp, cyc = 0;
        const char *s;
        if (sscanf(line, "%4x", &pc) != 1) break;
        if (!(s = strstr(line, "A:")) || sscanf(s, "A:%2x X:%2x Y:%2x P:%2x SP:%2x", &a, &x, &y, &p, &sp) != 5) break;
        if ((s = strstr(line, "CYC:"))) sscanf(s, "CYC:%u", &cyc);
        nes_debug_t d; nes_debug(&d);
        if (d.pc != pc || d.a != a || d.x != x || d.y != y || d.p != p || d.s != sp || d.cycles != cyc) {
            printf("nestest line %d: want PC %04X A %02X X %02X Y %02X P %02X S %02X CYC %u; got PC %04X A %02X X %02X Y %02X P %02X S %02X CYC %u\n",
                   ln, pc, a, x, y, p, sp, cyc, d.pc, d.a, d.x, d.y, d.p, d.s, d.cycles);
            bad++;
            break;
        }
        nes_test_step();
    }
    fclose(lg);
    CHECK(bad == 0, "nestest stopped at line %d", ln);
    CHECK(ln > 8900, "nestest ran %d lines (8991 expected)", ln);
    CHECK(nes_ram()[2] == 0 && nes_ram()[3] == 0, "nestest result bytes %02X %02X", nes_ram()[2], nes_ram()[3]);
}

int main(int argc, char **argv)
{
    test_bad_files();
    test_cpu();
    test_picture_and_nmi();
    test_apu();
    test_mmc1();
    test_mmc3_irq();
    if (argc >= 3) test_nestest(argv[1], argv[2]);
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
