/*
 * The QPU mix of the SNES lines (qp_snes_mix, core/tools/qpuprogs.py) on the V3D emulator: every pixel equals the ARM mix of snes_ppu.c (the C loop of
 * the lines with colour maths and a sub screen, brightness 15, no clip rule), for random lines in the four modes, and for a job of many lines.
 *
 *   test_snes_mix [cases]
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu/qpuprog.h"
#include "gpu/v3d.h"
#include "v3d_emu.h"

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

static uint32_t seed = 12345;
static uint32_t rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }

/* the ARM mix (snes_ppu.c): lanes of 8 times the 5-bit values */
static uint32_t uqadd(uint32_t a, uint32_t b) { uint32_t r = 0; for (int i = 0; i < 32; i += 8) { uint32_t t = ((a >> i) & 255) + ((b >> i) & 255); r |= (t > 255 ? 255u : t) << i; } return r; }
static uint32_t uqsub(uint32_t a, uint32_t b) { uint32_t r = 0; for (int i = 0; i < 32; i += 8) { uint32_t x = (a >> i) & 255, y = (b >> i) & 255; r |= (x > y ? x - y : 0u) << i; } return r; }
static uint32_t uhadd(uint32_t a, uint32_t b) { uint32_t r = 0; for (int i = 0; i < 32; i += 8) r |= ((((a >> i) & 255) + ((b >> i) & 255)) >> 1) << i; return r; }
static uint16_t rgb565(unsigned r, unsigned g, unsigned b) { return (uint16_t)((r << 11) | (((g << 1) | (g >> 4)) << 5) | b); }

static void ref_line(const uint8_t *idx, const uint8_t *flag, const uint8_t *sidx, const uint32_t *pal8, uint32_t fix8, int mode, uint16_t *out)
{
    for (int x = 0; x < 256; x++) {
        const uint32_t c = pal8[idx[x]];
        uint32_t v = c;
        if (flag[x]) {
            const uint32_t sc = sidx[x] ? pal8[sidx[x]] : fix8;
            switch (mode) {
            case 3: v = uhadd(c, sc); break;
            case 2: v = uqadd(c, sc); break;
            case 1: v = uhadd(uqsub(c, sc), 0); break;
            default: v = uqsub(c, sc); break;
            }
        }
        out[x] = rgb565((v >> 3) & 31, (v >> 11) & 31, (v >> 19) & 31);
    }
}

int main(int argc, char **argv)
{
    const int cases = argc > 1 ? atoi(argv[1]) : 64;
    if (v3d_init() != 0) { printf("snes mix: no V3D emulator (%s)\n", v3d_status()); return 1; }
    uint32_t *code = test_aligned_alloc(64, sizeof qp_snes_mix);
    memcpy(code, qp_snes_mix, sizeof qp_snes_mix);
    enum { LINES = 16 };
    uint8_t *rec = test_aligned_alloc(64, LINES * 1024);
    uint32_t *pal8 = test_aligned_alloc(64, 256 * 4);
    uint16_t *dst = test_aligned_alloc(64, LINES * 512);
    uint32_t *uni = test_aligned_alloc(64, (1 + 5 * LINES) * 4);
    uint16_t ref[256];
    int bad = 0;
    for (int c = 0; c < cases && !bad; c++) {
        const int nl = c == 0 ? LINES : 1 + (int)(rnd() % LINES);
        for (int k = 0; k < 256; k++) {
            const uint32_t p = rnd() & 0x7FFF;
            pal8[k] = (p & 31) * 8u | ((p >> 5) & 31) * 8u << 8 | ((p >> 10) & 31) * 8u << 16;
        }
        uni[0] = (uint32_t)nl;
        int modes[LINES];
        uint32_t fixes[LINES];
        for (int l = 0; l < nl; l++) {
            uint8_t *r = rec + l * 1024;
            for (int x = 0; x < 256; x++) {
                r[x] = (uint8_t)rnd();
                r[256 + x] = (uint8_t)(rnd() % 3 != 0);
                r[512 + x] = (uint8_t)(rnd() % 4 == 0 ? 0 : rnd());
            }
            modes[l] = (int)(rnd() & 3);
            fixes[l] = (rnd() % 32) * 8u | (rnd() % 32) * 8u << 8 | (rnd() % 32) * 8u << 16;
            uint32_t *u = uni + 1 + 5 * l;
            u[0] = v3d_bus(r); u[1] = v3d_bus(dst + l * 256); u[2] = v3d_bus(pal8); u[3] = fixes[l]; u[4] = (uint32_t)modes[l];
        }
        memset(dst, 0xA5, LINES * 512);
        v3d_qpu_job_t job = { v3d_bus(code), v3d_bus(uni), (uint32_t)(1 + 5 * nl) };
        if (v3d_qpu_run(&job, 1, 1000000u) != 0) { printf("snes mix: the run failed (%s) %s\n", v3d_qpu_status(), emu_error); return 1; }
        for (int l = 0; l < nl && !bad; l++) {
            const uint8_t *r = rec + l * 1024;
            ref_line(r, r + 256, r + 512, pal8, fixes[l], modes[l], ref);
            for (int x = 0; x < 256; x++)
                if (dst[l * 256 + x] != ref[x]) {
                    printf("MISMATCH case %d line %d x %d mode %d flag %d sidx %d: qpu %04X ref %04X\n", c, l, x, modes[l], r[256 + x], r[512 + x],
                           dst[l * 256 + x], ref[x]);
                    bad = 1;
                    break;
                }
        }
    }
    printf("snes mix: %d cases, %s (%lu QPU instructions)\n", cases, bad ? "FAILED" : "ok", (unsigned long)emu_stats.qpu_steps);
    return bad;
}
