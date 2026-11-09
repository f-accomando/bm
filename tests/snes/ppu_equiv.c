/* ppu_equiv.c: the fast PPU (src/bm/snes_ppu.c) against the reference copy (tests/snes/ppu_ref.c).
 *   ppu_equiv equiv [cases]   random register, VRAM, OAM and CGRAM states; every line must match bit for bit
 *   ppu_equiv bench [loops]   time of ppu_line for a scene like Super Mario World (mode 1, three layers, sprites, colour math)
 * Original code. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "snes_int.h"

void ppu_reset_ref(void);
void ppu_line_ref(int y);
void ppu_cgram_write_ref(int index);

static uint64_t rs = 88172645463325252ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }

static void fill_vram(int clear_pct)
{
    for (int i = 0; i < 65536; i++) S.vram[i] = (rnd() % 100 < (uint32_t)clear_pct) ? 0 : (uint8_t)rnd();
    /* tile rows that are plain: clear rows of 8 bytes now and then, and tile maps that point to few tiles */
    for (int i = 0; i < 65536; i += 16) if (rnd() % 4 == 0) memset(&S.vram[i], 0, 16);
    ppu_vram_dirty_all();                                            /* the PPU keeps decoded tiles: tell it that the video RAM changed */
}

static void fill_cgram(void)
{
    for (int i = 0; i < 512; i++) S.cgram[i] = (uint8_t)rnd();
    for (int i = 0; i < 256; i++) { ppu_cgram_write(i); ppu_cgram_write_ref(i); }
}

static void random_state(void)
{
    static const uint8_t modes[] = { 0, 1, 2, 3, 4, 7, 5, 6, 1, 1, 1 };
    fill_vram((int)(rnd() % 80));
    fill_cgram();
    for (int i = 0; i < 544; i++) S.oam[i] = (uint8_t)rnd();
    if (rnd() & 1) for (int i = 0; i < 128; i++) S.oam[i * 4 + 1] = (uint8_t)(rnd() % 240);   /* more sprites on the lines */
    S.inidisp = (uint8_t)((rnd() % 4 == 0) ? (rnd() & 15) : 15);
    S.obsel = (uint8_t)rnd();
    S.oamrot = (uint8_t)(rnd() & 1); S.oamaddr_reload = (uint16_t)(rnd() & 0x3FF);       /* the priority rotation of the sprites */
    S.bgmode = (uint8_t)(modes[rnd() % sizeof modes] | ((rnd() & 1) ? (rnd() & 0xF8) : 0));
    S.mosaic = 0;
    for (int n = 0; n < 4; n++) {
        S.sc[n] = (uint8_t)rnd();
        S.nba[n] = (uint16_t)((rnd() & 7) << 13);
        S.hofs[n] = (uint16_t)(rnd() & 0x3FF);
        S.vofs[n] = (uint16_t)(rnd() & 0x3FF);
    }
    S.m7sel = (uint8_t)rnd();
    S.m7a = (int16_t)(rnd() % 1024 - 512); S.m7b = (int16_t)(rnd() % 256 - 128);
    S.m7c = (int16_t)(rnd() % 256 - 128); S.m7d = (int16_t)(rnd() % 1024 - 512);
    S.m7x = (int16_t)(rnd() & 0x1FFF); S.m7y = (int16_t)(rnd() & 0x1FFF);
    S.m7hofs = (int16_t)(rnd() & 0x1FFF); S.m7vofs = (int16_t)(rnd() & 0x1FFF);
    S.tm = (uint8_t)rnd(); S.ts = (uint8_t)rnd();
    if (rnd() & 1) { S.tm |= 0x17; }
    S.tmw = (uint8_t)rnd(); S.tsw = (uint8_t)rnd();
    if (rnd() % 3) { S.w12sel = S.w34sel = S.wobjsel = 0; S.wbglog = S.wobjlog = 0; }
    else { S.w12sel = (uint8_t)rnd(); S.w34sel = (uint8_t)rnd(); S.wobjsel = (uint8_t)rnd(); S.wbglog = (uint8_t)rnd(); S.wobjlog = (uint8_t)rnd(); }
    S.wh0 = (uint8_t)rnd(); S.wh1 = (uint8_t)rnd(); S.wh2 = (uint8_t)rnd(); S.wh3 = (uint8_t)rnd();
    const unsigned k = rnd() % 4;                                   /* colour maths: off, plain, with the sub screen, with window rules */
    S.cgwsel = (uint8_t)(k == 0 ? 0 : k == 1 ? (rnd() & 0x00) : k == 2 ? 2 : rnd());
    S.cgadsub = (uint8_t)(k == 0 ? 0 : rnd());
    S.fixr = (uint8_t)(rnd() & 31); S.fixg = (uint8_t)(rnd() & 31); S.fixb = (uint8_t)(rnd() & 31);
}

static int equiv(int cases)
{
    static uint16_t ref[SNES_W];
    int bad = 0;
    for (int c = 0; c < cases && bad < 5; c++) {
        random_state();
        for (int y = 0; y < 224; y++) {
            if (rnd() % 8 == 0) { S.hofs[rnd() & 3] = (uint16_t)(rnd() & 0x3FF); S.cgadsub ^= (uint8_t)(rnd() & 1); }   /* a change in the middle of the frame */
            if (rnd() % 6 == 0) {                                                    /* the video RAM written through the registers, as a game does: the cache of decoded tiles must follow */
                snes_ppu_write(0x2115, 0);
                const uint16_t wa = (uint16_t)(rnd() & 0x7FFF);
                snes_ppu_write(0x2116, (uint8_t)wa); snes_ppu_write(0x2117, (uint8_t)(wa >> 8));
                for (int i = 0; i < 6; i++) { snes_ppu_write(0x2118, (uint8_t)rnd()); snes_ppu_write(0x2119, (uint8_t)rnd()); }
            }
            S.inidisp = (uint8_t)((S.inidisp & 0x80) | (S.inidisp & 15));
            memset(&S.picture[y * SNES_W], 0xA5, SNES_W * 2);
            ppu_line_ref(y);
            memcpy(ref, &S.picture[y * SNES_W], sizeof ref);
            memset(&S.picture[y * SNES_W], 0x5A, SNES_W * 2);
            ppu_line(y);
            if (memcmp(ref, &S.picture[y * SNES_W], sizeof ref)) {
                int x = 0;
                while (ref[x] == S.picture[y * SNES_W + x]) x++;
                printf("MISMATCH case %d line %d x %d: ref %04X new %04X  mode %d tm %02X ts %02X cgwsel %02X cgadsub %02X bright %d\n", c, y, x, ref[x],
                       S.picture[y * SNES_W + x], S.bgmode & 7, S.tm, S.ts, S.cgwsel, S.cgadsub, S.inidisp & 15);
                bad++;
                break;
            }
        }
    }
    printf("ppu equivalence: %d cases, %s\n", cases, bad ? "FAILED" : "ok");
    return bad != 0;
}

extern uint32_t ppu_us_ref[5];
static uint32_t host_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)((uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1e6 + (double)t.tv_nsec / 1e3; }

static void scene(void)
{
    rs = 12345;
    fill_vram(35);
    fill_cgram();
    for (int i = 0; i < 128; i++) { S.oam[i * 4] = (uint8_t)rnd(); S.oam[i * 4 + 1] = (uint8_t)(i < 40 ? rnd() % 200 : 240); S.oam[i * 4 + 2] = (uint8_t)rnd(); S.oam[i * 4 + 3] = (uint8_t)(rnd() & 0x3F); }
    memset(&S.oam[512], 0, 32);
    S.inidisp = 15; S.obsel = 0x03; S.bgmode = 1;
    S.sc[0] = 0x00; S.sc[1] = 0x04; S.sc[2] = 0x08; S.sc[3] = 0;
    S.nba[0] = 0x0000; S.nba[1] = 0x2000; S.nba[2] = 0x4000;
    S.hofs[0] = 37; S.hofs[1] = 90; S.hofs[2] = 5; S.vofs[0] = 12; S.vofs[1] = 3;
    S.tm = 0x17; S.ts = 0x00; S.tmw = S.tsw = 0; S.w12sel = S.w34sel = S.wobjsel = 0; S.wbglog = S.wobjlog = 0;
    S.cgwsel = 0x00; S.cgadsub = 0x21; S.fixr = 6; S.fixg = 12; S.fixb = 20;        /* maths on BG1 and the backdrop with the fixed colour */
}

static void bench(int loops)
{
    static const char *name[2] = { "reference", "fast" };
    for (int which = 0; which < 2; which++) {
        for (int variant = 0; variant < 2; variant++) {
            scene();
            if (variant) { S.cgadsub = 0; }                 /* no maths: the common line */
            memset(ppu_us, 0, sizeof ppu_us); memset(ppu_us_ref, 0, sizeof ppu_us);
            snes_now_us = (loops <= 40) ? host_us : NULL;   /* with a few loops: the parts are timed too (the clock itself costs) */
            double t0 = now();
            for (int l = 0; l < loops; l++)
                for (int y = 0; y < 224; y++) { if (which) ppu_line(y); else ppu_line_ref(y); }
            double dt = now() - t0;
            const uint32_t *u = which ? ppu_us : ppu_us_ref;
            printf("%-9s %-9s %6.2f us a line, %7.1f us a frame", name[which], variant ? "no maths" : "maths", dt / loops / 224, dt / loops);
            if (snes_now_us) printf("   layers %.1f obj %.1f win %.1f compose %.1f mix %.1f us a frame", (double)u[0] / loops, (double)u[1] / loops, (double)u[2] / loops, (double)u[3] / loops, (double)u[4] / loops);
            printf("\n");
            snes_now_us = NULL;
        }
    }
}

/* The priority rotation ($2103 bit 7): two sprites on the same pixel; the one at the OAM address wins. */
static int rotation(void)
{
    memset(S.oam, 0, sizeof S.oam);
    for (int i = 0; i < 128; i++) S.oam[i * 4 + 1] = 240;
    memset(S.vram, 0, sizeof S.vram);
    memset(&S.vram[0x4000 + 1 * 32], 0xFF, 16);                       /* tile 1: planes 0 and 1: colour 3 of its palette */
    for (int r = 0; r < 8; r++) S.vram[0x4000 + 2 * 32 + r * 2] = 0xFF;   /* tile 2: plane 0 only: colour 1 */
    ppu_vram_dirty_all();
    memset(S.cgram, 0, sizeof S.cgram);
    S.cgram[(128 + 3) * 2] = 0x1F;                                    /* sprite colour 3: red */
    S.cgram[(128 + 1) * 2 + 1] = 0x7C;                                /* sprite colour 1: blue */
    for (int i = 0; i < 256; i++) { ppu_cgram_write(i); ppu_cgram_write_ref(i); }
    S.oam[0] = 10; S.oam[1] = 9; S.oam[2] = 1; S.oam[3] = 0x30;      /* sprite 0: colour 3 */
    S.oam[20] = 10; S.oam[21] = 9; S.oam[22] = 2; S.oam[23] = 0x30;  /* sprite 5: colour 1 */
    S.inidisp = 15; S.obsel = 0x01; S.bgmode = 1; S.tm = 0x10; S.ts = 0; S.cgwsel = 0; S.cgadsub = 0;
    int bad = 0;
    for (int rot = 0; rot < 2; rot++) {
        S.oamrot = (uint8_t)rot; S.oamaddr_reload = (uint16_t)(5 * 4);
        ppu_line(10); const uint16_t n = S.picture[10 * SNES_W + 12];
        ppu_line_ref(10); const uint16_t r = S.picture[10 * SNES_W + 12];
        const uint16_t want = rot ? 0x001F : 0xF800;                  /* blue (sprite 5) with the rotation, red (sprite 0) without; RGB565 */
        if (n != want || r != want) { printf("rotation %d: want %04X got %04X (ref %04X)\n", rot, want, n, r); bad = 1; }
    }
    return bad;
}

int main(int argc, char **argv)
{
    ppu_reset(); ppu_reset_ref();
    if (rotation()) return 1;
    if (argc > 1 && !strcmp(argv[1], "bench")) { bench(argc > 2 ? atoi(argv[2]) : 200); return 0; }
    return equiv(argc > 2 ? atoi(argv[2]) : 400);
}
