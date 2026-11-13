/*
 * G2 (docs/progress/snes-gpu.md): the Mode 7 layer of the SNES PPU on the QPUs of the V3D, on the V3D emulator
 * (tests/gpu/v3d_emu.c). The program qp_snes_mode7 (core/tools/qpusnes.py) makes the 256 x 224 picture of a frame with
 * only BG1 on; every pixel must equal the one that ppu_line of the reference PPU (tests/snes/ppu_ref.c) makes.
 * The ARM does the set-up of each line (X and Y at the start of the line, as draw_mode7 does it); the QPUs do the
 * work for each pixel. Original code.
 *
 *   test_snes_m7 [cases]      states: identity, rotation and scale, mirrors, the four screen-over modes, random
 *   test_snes_m7 cost         the instructions of the program for a frame (the figures for the notes)
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu/v3d.h"
#include "snes_int.h"
#include "v3d_emu.h"

#include "gpu/qpuprog_snes.h"                           /* made by core/tools/qpusnes.py */

void ppu_reset_ref(void);
void ppu_line_ref(int y);
void ppu_cgram_write_ref(int index);

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

static int checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint64_t rs = 88172645463325252ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }

#define LINES 224
#define JOBS 14                                         /* 16 lines each: whole lines for a job */

static uint8_t *g_vram;
static uint16_t *g_pal, *g_dst;
static int32_t *g_tab;
static uint32_t *g_code, *g_uni;

/* X and Y at the start of line y: the set-up of draw_mode7 (the ARM's part) */
static void line_start(int y, int32_t *X, int32_t *Y)
{
    const int yy = (S.m7sel & 2) ? 255 - y : y;
    const int vx = S.m7hofs - S.m7x, vy = S.m7vofs - S.m7y;
    const int cx = (vx & 0x2000) ? (vx | ~0x3FF) : (vx & 0x3FF);
    const int cy = (vy & 0x2000) ? (vy | ~0x3FF) : (vy & 0x3FF);
    *X = ((S.m7a * cx) & ~63) + ((S.m7b * cy) & ~63) + ((S.m7b * yy) & ~63) + (S.m7x << 8);
    *Y = ((S.m7c * cx) & ~63) + ((S.m7d * cy) & ~63) + ((S.m7d * yy) & ~63) + (S.m7y << 8);
}

static void make_state(int mode7_only_clean)
{
    for (int i = 0; i < 65536; i++) S.vram[i] = (uint8_t)rnd();
    for (int i = 0; i < 65536; i += 16) if (rnd() % 4 == 0) memset(&S.vram[i], 0, 16);     /* clear rows: index 0 (the backdrop) */
    ppu_vram_dirty_all();
    for (int i = 0; i < 512; i++) S.cgram[i] = (uint8_t)rnd();
    for (int i = 0; i < 256; i++) { ppu_cgram_write(i); ppu_cgram_write_ref(i); }
    memset(S.oam, 0, sizeof S.oam);
    S.inidisp = 15;
    S.bgmode = 7;
    S.mosaic = 0;
    S.tm = 1; S.ts = 0; S.tmw = S.tsw = 0;
    S.w12sel = S.w34sel = S.wobjsel = 0; S.wbglog = S.wobjlog = 0;
    S.cgwsel = 0; S.cgadsub = 0;
    S.fixr = S.fixg = S.fixb = 0;
    (void)mode7_only_clean;
}

/* the reference picture of the 224 lines, and the picture of the QPUs; 0 if equal */
static int one_state(const char *name)
{
    static uint16_t ref[SNES_W * LINES];
    for (int y = 0; y < LINES; y++) {
        ppu_line_ref(y);
        memcpy(&ref[y * SNES_W], &S.picture[y * SNES_W], SNES_W * 2);
    }
    memcpy(g_vram, S.vram, 65536);
    for (int k = 0; k < 256; k++) {                 /* the colour table of ppu_line: the palette as RGB565, brightness 15 (the ARM keeps it until the palette changes) */
        const unsigned c = S.pal[k], r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
        g_pal[k] = (uint16_t)((r << 11) | (((g << 1) | (g >> 4)) << 5) | b);
    }
    memset(g_dst, 0xAB, LINES * SNES_W * 2);
    for (int y = 0; y < LINES; y++) line_start(y, &g_tab[y * 2], &g_tab[y * 2 + 1]);
    v3d_qpu_job_t jobs[JOBS];
    const int per = LINES / JOBS;
    for (int j = 0; j < JOBS; j++) {
        uint32_t *u = g_uni + j * 8;
        u[0] = v3d_bus(g_vram);
        u[1] = v3d_bus(g_pal);
        u[2] = v3d_bus(g_dst + j * per * SNES_W);
        u[3] = v3d_bus(g_tab + j * per * 2);
        u[4] = (uint32_t)(int32_t)S.m7a;
        u[5] = (uint32_t)(int32_t)S.m7c;
        u[6] = S.m7sel;
        u[7] = (uint32_t)per * 8;
        jobs[j].code = v3d_bus(g_code);
        jobs[j].unif = v3d_bus(u);
        jobs[j].nunif = 8;
    }
    const uint32_t steps0 = emu_stats.qpu_steps;
    const int r = v3d_qpu_run(jobs, JOBS, 5000000u);
    CHECK(r == 0, "%s: v3d_qpu_run returned %d (%s)", name, r, emu_error);
    if (r != 0) return 1;
    int bad = 0, first = -1;
    for (int i = 0; i < SNES_W * LINES; i++)
        if (g_dst[i] != ref[i]) { if (first < 0) first = i; bad++; }
    CHECK(bad == 0, "%s: %d pixels differ (first at x %d y %d: QPU %04x, reference %04x; m7a %d b %d c %d d %d x %d y %d hofs %d vofs %d sel %02x)",
          name, bad, first % SNES_W, first / SNES_W, first >= 0 ? g_dst[first] : 0, first >= 0 ? ref[first] : 0,
          S.m7a, S.m7b, S.m7c, S.m7d, S.m7x, S.m7y, S.m7hofs, S.m7vofs, S.m7sel);
    if (!bad) printf("ok   %s (%u QPU instructions)\n", name, emu_stats.qpu_steps - steps0);
    return bad != 0;
}

static void regs(int a, int b, int c, int d, int x, int y, int hofs, int vofs, int sel)
{
    S.m7a = (int16_t)a; S.m7b = (int16_t)b; S.m7c = (int16_t)c; S.m7d = (int16_t)d;
    S.m7x = (int16_t)x; S.m7y = (int16_t)y; S.m7hofs = (int16_t)hofs; S.m7vofs = (int16_t)vofs;
    S.m7sel = (uint8_t)sel;
}

int main(int argc, char **argv)
{
    const int cost = argc > 1 && !strcmp(argv[1], "cost");
    const int cases = argc > 1 && !cost ? atoi(argv[1]) : 40;
    g_vram = test_aligned_alloc(64, 65536);
    g_pal = test_aligned_alloc(64, 512);
    g_dst = test_aligned_alloc(64, LINES * SNES_W * 2);
    g_tab = test_aligned_alloc(64, LINES * 8);
    g_code = test_aligned_alloc(64, sizeof qp_snes_mode7);
    g_uni = test_aligned_alloc(64, JOBS * 32);
    memcpy(g_code, qp_snes_mode7, sizeof qp_snes_mode7);
    ppu_reset_ref();
    make_state(1);

    regs(0x100, 0, 0, 0x100, 0, 0, 0, 0, 0);                                    /* identity */
    one_state("identity");
    regs(0x100, 0, 0, 0x100, 128, 112, 100, 60, 0);                             /* scroll and a centre */
    one_state("scroll");
    regs(0x0B5, 0x0B5, -0x0B5, 0x0B5, 128, 112, 0, 0, 0);                      /* a rotation of 45 degrees, scale 0.7 */
    one_state("rotation 45");
    regs(0x200, 0x040, -0x040, 0x200, 0x1FF0, 0x1F80, 0x1FF0, 0x1F80, 0);      /* a zoom out with the plane's corner (negative centre) */
    one_state("zoom out, corner");
    regs(0x100, 0, 0, 0x100, 0, 0, 0, 0, 3);                                    /* flip x and y */
    one_state("mirror x and y");
    regs(-0x120, 0x30, 0x30, -0x120, 100, 100, 0x1F00, 0x1F00, 1);             /* negative scale, flip x */
    one_state("negative scale, flip x");
    for (int rep = 0; rep < 4; rep++) {                                         /* the screen over: wrap, wrap, clear, tile 0 */
        char n[48];
        regs(0x0C0, 0x040, -0x040, 0x0C0, 0x300, 0x300, 0x300, 0x300, rep << 6);
        snprintf(n, sizeof n, "screen over mode %d, outside", rep);
        one_state(n);
        regs(0x040, 0, 0, 0x040, 0, 0, 0x2000 - 256, 0x2000 - 256, rep << 6 | (rep & 1));
        snprintf(n, sizeof n, "screen over mode %d, a long way out", rep);
        one_state(n);
    }
    regs(0x7FFF, 0x7FFF, -0x8000, 0x7FFF, 0, 0, 0, 0, 0);                       /* the largest words */
    one_state("largest matrix");
    regs(-0x8000, 0, 0, -0x8000, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0xC1);
    one_state("smallest matrix");
    if (!cost) {
        for (int i = 0; i < cases; i++) {                                       /* random states */
            if (i % 8 == 0) make_state(1);
            regs((int)(rnd() % 65536) - 32768, (int)(rnd() % 65536) - 32768, (int)(rnd() % 65536) - 32768, (int)(rnd() % 65536) - 32768,
                 (int)(rnd() & 0x1FFF), (int)(rnd() & 0x1FFF), (int)(rnd() & 0x1FFF), (int)(rnd() & 0x1FFF), (int)(rnd() & 0xFF));
            if (rnd() % 2) regs((int)(rnd() % 1024) - 512, (int)(rnd() % 256) - 128, (int)(rnd() % 256) - 128, (int)(rnd() % 1024) - 512,
                                S.m7x, S.m7y, S.m7hofs, S.m7vofs, S.m7sel);
            char n[24];
            snprintf(n, sizeof n, "random %d", i);
            if (one_state(n) && failures > 5) break;
        }
    }
    printf("snes mode 7 on the QPUs: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
