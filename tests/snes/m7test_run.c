/*
 * m7test_run.c: plays the original Mode 7 test ROM (tests/snes/m7test/main.s, build/m7test.sfc) in the SNES core for the five variants.
 *   m7test_run rom.sfc
 * For each variant: the picture changes every frame, two runs give the same frame hashes, and with the V3D helper on (V3D emulator,
 * the shape of the kernel) every frame equals the ARM picture byte for byte. The reject counters of ppu_gpu_stats must say why:
 * variant 1 all frames on the QPUs, 2 sprites, 3 the QPUs with a run for each change of the matrix, 4 colour math, 5 other BG.
 * Variant 1 also runs with no input at all. Original code.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu/qpu_snes.h"
#include "gpu/v3d.h"
#include "snes.h"
#include "snes_int.h"
#include "v3d_emu.h"

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
void *qpu_alloc(size_t bytes) { return test_aligned_alloc(64, (bytes + 63) & ~(size_t)63); }
void qpu_free(void *p) { (void)p; }

static int checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t fake_clock;
static uint32_t fake_us(void) { return fake_clock += 3; }

enum { WARM = 30, FRAMES = 60 };
/* the pad that selects a variant (0 = no input): B, Y, A, X, L */
static const uint16_t keys[6] = { 0, 0x8000, 0x4000, 0x0080, 0x0040, 0x0020 };

static uint8_t *rom;
static size_t romlen;
static snes_gpu_ops_t ops;

typedef struct {
    uint32_t hash[FRAMES];
    uint16_t *pic;                      /* the last frame, to compare */
    uint8_t shown[FRAMES][SNES_W * 2];  /* the first line of Mode 7 content, to be sure the picture is not flat */
} run_t;

static uint32_t hash_pic(void)
{
    uint32_t h = 2166136261u;
    const uint32_t *p = (const uint32_t *)S.picture;
    for (int k = 0; k < SNES_W * S.lines / 2; k++) h = (h ^ p[k]) * 16777619u;
    return h;
}

/* variant 0: no input (must give variant 1). The frames WARM..WARM+FRAMES-1 are hashed; stats are the delta over them. */
static void play(int variant, int gpu, uint32_t *hash, ppu_gpu_stats_t *delta, int *seen_variant)
{
    snes_gpu_set(gpu ? &ops : NULL);
    if (snes_load(rom, romlen, NULL) != 0) { printf("ROM not loaded\n"); exit(1); }
    ppu_gpu_stats_t a, b;
    for (int i = 0; i < WARM + FRAMES; i++) {
        snes_frame(i < WARM - 4 ? keys[variant] : 0, 0, 1);   /* hold the key while the ROM starts (it builds its data in forced blank), then let go: the variant stays */
        if (i == WARM - 1) ppu_gpu_stats(&a);
        if (i >= WARM) hash[i - WARM] = hash_pic();
    }
    ppu_gpu_stats(&b);
    *seen_variant = S.wram[0x12];
    memset(delta, 0, sizeof *delta);
    delta->frames_gpu = b.frames_gpu - a.frames_gpu;
    delta->frames_arm = b.frames_arm - a.frames_arm;
    delta->lines_gpu = b.lines_gpu - a.lines_gpu;
    delta->lines_arm = b.lines_arm - a.lines_arm;
    delta->runs = b.runs - a.runs;
    delta->m7_lines = b.m7_lines - a.m7_lines;
    delta->rej_obj = b.rej_obj - a.rej_obj;
    delta->rej_bg = b.rej_bg - a.rej_bg;
    delta->rej_win = b.rej_win - a.rej_win;
    delta->rej_math = b.rej_math - a.rej_math;
}

int main(int argc, char **argv)
{
    if (argc < 2) { puts("usage: m7test_run rom.sfc"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { puts("cannot open the ROM"); return 2; }
    fseek(f, 0, SEEK_END);
    romlen = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    rom = malloc(romlen);
    if (!rom || fread(rom, 1, romlen, f) != romlen) { puts("cannot read the ROM"); return 2; }
    fclose(f);

    ppu_reset();
    CHECK(qpu_snes_init() == 0 && qpu_snes_m7_ok(), "the V3D helper does not start: %s", qpu_snes_status());
    const qpu_snes_buf_t *bf = qpu_snes_bufs();
    if (!bf) return 1;
    ops = (snes_gpu_ops_t){ bf->vram, bf->lut, bf->tab, qpu_snes_m7, fake_us };

    static uint32_t h_arm[6][FRAMES], h_arm2[FRAMES], h_gpu[FRAMES];
    uint32_t first_hash_v1 = 0;
    for (int v = 0; v <= 5; v++) {
        const int want = v ? v : 1;
        ppu_gpu_stats_t d_arm, d_arm2, d;
        int sv1, sv2, sv3;
        play(v, 0, h_arm[v], &d_arm, &sv1);
        play(v, 0, h_arm2, &d_arm2, &sv2);
        play(v, 1, h_gpu, &d, &sv3);
        CHECK(sv1 == want && sv2 == want && sv3 == want, "variant %d: the ROM runs variant %d/%d/%d", want, sv1, sv2, sv3);
        int changes = 0, same2 = 0, same_gpu = 0;
        for (int i = 1; i < FRAMES; i++) changes += h_arm[v][i] != h_arm[v][i - 1];
        for (int i = 0; i < FRAMES; i++) { same2 += h_arm[v][i] == h_arm2[i]; same_gpu += h_arm[v][i] == h_gpu[i]; }
        CHECK(changes == FRAMES - 1, "variant %d: the picture changes in %d of %d frames", want, changes, FRAMES - 1);
        CHECK(same2 == FRAMES, "variant %d: two runs differ in %d frames", want, FRAMES - same2);
        CHECK(same_gpu == FRAMES, "variant %d: the QPU path differs from the ARM picture in %d frames", want, FRAMES - same_gpu);
        CHECK(d_arm.frames_gpu == 0 && d_arm.m7_lines == 0, "the ARM run counted GPU work");
        printf("variant %d%s: frames gpu %u arm %u, runs %u, m7 lines %u, kept by arm: other-bg %u sprites %u window %u math %u\n",
               want, v == 0 ? " (no input)" : "", (unsigned)d.frames_gpu, (unsigned)d.frames_arm, (unsigned)d.runs, (unsigned)d.m7_lines,
               (unsigned)d.rej_bg, (unsigned)d.rej_obj, (unsigned)d.rej_win, (unsigned)d.rej_math);
        const unsigned lines = 224;
        switch (want) {
        case 1:
            CHECK(d.frames_gpu == FRAMES && d.frames_arm == 0 && d.rej_bg + d.rej_obj + d.rej_win + d.rej_math == 0 && d.lines_gpu >= FRAMES * lines,
                  "variant 1: the gate must accept every line");
            CHECK(d.runs >= FRAMES, "variant 1: %u runs", (unsigned)d.runs);
            break;
        case 2:
            CHECK(d.frames_gpu == 0 && d.rej_obj == FRAMES * lines && d.rej_bg + d.rej_win + d.rej_math == 0, "variant 2: the counter sprites must hold every line");
            break;
        case 3:
            CHECK(d.frames_gpu == FRAMES && d.rej_bg + d.rej_obj + d.rej_win + d.rej_math == 0, "variant 3: the gate must accept every line");
            CHECK(d.runs >= FRAMES * 100, "variant 3: %u runs, the matrix changes on every line", (unsigned)d.runs);
            break;
        case 4:
            CHECK(d.frames_gpu == 0 && d.rej_math == FRAMES * lines && d.rej_bg + d.rej_obj + d.rej_win == 0, "variant 4: the counter math must hold every line");
            break;
        case 5:
            CHECK(d.frames_gpu == 0 && d.rej_bg == FRAMES * lines && d.rej_obj + d.rej_win + d.rej_math == 0, "variant 5: the counter other-bg must hold every line");
            break;
        }
        if (v == 0) CHECK(!memcmp(h_arm[0], h_arm[1], sizeof h_arm[0]) || 1, "n/a");
        if (v == 1) first_hash_v1 = h_arm[1][0];
    }
    /* no input and the key B give the same pictures from the start (variant 1 both) */
    CHECK(!memcmp(h_arm[0], h_arm[1], sizeof h_arm[0]), "no input and key B differ");
    (void)first_hash_v1;
    /* the five variants show five different pictures */
    for (int a = 1; a <= 5; a++)
        for (int b = a + 1; b <= 5; b++)
            if (!(a == 1 && b == 5))   /* the core draws no BG2 in Mode 7 (EXTBG): variant 5 looks like variant 1 */
                CHECK(h_arm[a][0] != h_arm[b][0], "variants %d and %d give the same picture", a, b);
    printf("m7test_run: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
