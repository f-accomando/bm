/*
 * G2 in the kernel shape (docs/progress/snes-gpu.md): the SNES PPU with the V3D helper on, on the V3D emulator.
 * snes_ppu.c keeps the lines of BG1 Mode 7 and core/src/gpu/qpu_snes.c draws them with qp_snes_mode7; every frame must be
 * the picture of the reference PPU (tests/snes/ppu_ref.c) byte for byte, also when the registers, the palette, the
 * video RAM or the screen change in the middle of the frame (the lines the GPU does not take go to the ARM, G8).
 *
 *   test_snes_gpu 0 [cases]   the QPUs work: the 56 states of G2 as frames, frames with changes in the middle, the micro-benchmark
 *   test_snes_gpu 1           the QPUs stop answering after the start: the ARM version of the run draws the same picture
 *   test_snes_gpu 2           the QPUs store wrong data: the probe turns Mode 7 off, the same picture
 * With BM_SNES_ROMS (a folder) mode 0 also plays a Mode 7 ROM (Super Castlevania IV, F-Zero) with and without the helper.
 * Original code.
 */
#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu/qpu_snes.h"
#include "gpu/v3d.h"
#include "snes.h"
#include "snes_int.h"
#include "v3d_emu.h"

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

void *qpu_alloc(size_t bytes) { return test_aligned_alloc(64, (bytes + 63) & ~(size_t)63); }
void qpu_free(void *p) { (void)p; }

static int checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint64_t rs = 88172645463325252ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }

static uint32_t fake_clock;
static uint32_t fake_us(void) { return fake_clock += 3; }

#define LINES 224

static void setup(uint64_t seed, int variant)
{
    rs = seed;
    for (int i = 0; i < 65536; i++) S.vram[i] = (uint8_t)rnd();
    for (int i = 0; i < 65536; i += 16) if (rnd() % 4 == 0) memset(&S.vram[i], 0, 16);
    ppu_vram_dirty_all();
    for (int i = 0; i < 512; i++) S.cgram[i] = (uint8_t)rnd();
    for (int i = 0; i < 256; i++) { ppu_cgram_write(i); ppu_cgram_write_ref(i); }
    for (int i = 0; i < 544; i++) S.oam[i] = (uint8_t)rnd();
    S.inidisp = 15; S.bgmode = 7; S.mosaic = 0; S.tm = 1; S.ts = 0; S.tmw = S.tsw = 0;
    S.w12sel = S.w34sel = S.wobjsel = 0; S.wbglog = S.wobjlog = 0; S.cgwsel = 0; S.cgadsub = 0;
    S.fixr = S.fixg = S.fixb = 0;
    S.m7a = (int16_t)(variant & 1 ? 0x0B5 : (int)(rnd() % 1024) - 512); S.m7b = (int16_t)((int)(rnd() % 256) - 128);
    S.m7c = (int16_t)(variant & 1 ? -0x0B5 : (int)(rnd() % 256) - 128); S.m7d = (int16_t)(variant & 1 ? 0x0B5 : (int)(rnd() % 1024) - 512);
    S.m7x = (int16_t)(rnd() & 0x1FFF); S.m7y = (int16_t)(rnd() & 0x1FFF);
    S.m7hofs = (int16_t)(rnd() & 0x1FFF); S.m7vofs = (int16_t)(rnd() & 0x1FFF);
    S.m7sel = (uint8_t)(rnd() & 0xC3);
}

/* what changes in the middle of the frame, before line y */
static void mutate(int y, int variant)
{
    if (variant & 2) {                                  /* HDMA on the matrix */
        if (y == 60) S.m7a = (int16_t)(S.m7a + 0x40);
        if (y == 120) { S.m7c = (int16_t)(S.m7c - 0x30); S.m7sel ^= 1; }
    }
    if (variant & 4) {                                  /* the palette: a gradient, a fade */
        if (y == 40) for (int k = 0; k < 8; k++) { const int i = (int)(rnd() & 255); S.cgram[i * 2] = (uint8_t)rnd(); S.cgram[i * 2 + 1] = (uint8_t)(rnd() & 0x7F); ppu_cgram_write(i); ppu_cgram_write_ref(i); }
        if (y == 90) S.inidisp = 9;
    }
    if (variant & 8) {                                  /* the video RAM: written on a line */
        if (y == 70) for (int k = 0; k < 40; k++) { const unsigned a = rnd() & 0xFFFF; S.vram[a] ^= (uint8_t)rnd(); ppu_vram_dirty((uint16_t)a); }
    }
    if (variant & 16) {                                 /* a forced blank for three lines */
        if (y == 10) S.inidisp |= 0x80;
        if (y == 13) S.inidisp &= 0x7F;
    }
    if (variant & 32) {                                 /* the sprites on from line 150: the ARM draws those lines */
        if (y == 150) S.tm = 0x11;
    }
    if (variant & 64) {                                 /* colour math for lines 100 to 130 */
        if (y == 100) { S.cgadsub = 0x21; S.fixr = 7; S.fixg = 3; S.fixb = 11; S.cgwsel = 0; }
        if (y == 131) S.cgadsub = 0;
    }
    if (variant & 128) {                                /* BG1 Mode 7 stops and Mode 1 starts for the last lines */
        if (y == 200) { S.bgmode = 1; S.tm = 7; }
    }
}

static uint16_t refpic[SNES_W * LINES];

/* one frame both ways from the same state; 0 if equal. The rng is reset by setup, so the changes are the same in both passes. */
static int frame(const char *name, uint64_t seed, int variant)
{
    setup(seed, variant);
    for (int y = 0; y < LINES; y++) {
        mutate(y, variant);
        ppu_line_ref(y);
        memcpy(&refpic[y * SNES_W], &S.picture[y * SNES_W], SNES_W * 2);
    }
    setup(seed, variant);
    memset(S.picture, 0xAB, sizeof S.picture);
    ppu_frame_begin(1);
    for (int y = 0; y < LINES; y++) {
        mutate(y, variant);
        ppu_line(y);
    }
    ppu_frame_end();
    int bad = 0, first = -1;
    for (int i = 0; i < SNES_W * LINES; i++)
        if (S.picture[i] != refpic[i]) { if (first < 0) first = i; bad++; }
    CHECK(bad == 0, "%s: %d pixels differ (first at x %d y %d: %04x, reference %04x; variant %02x)", name, bad, first % SNES_W,
          first / SNES_W, first >= 0 ? S.picture[first] : 0, first >= 0 ? refpic[first] : 0, variant);
    return bad != 0;
}

static void regs(int a, int b, int c, int d, int x, int y, int hofs, int vofs, int sel)
{
    S.m7a = (int16_t)a; S.m7b = (int16_t)b; S.m7c = (int16_t)c; S.m7d = (int16_t)d;
    S.m7x = (int16_t)x; S.m7y = (int16_t)y; S.m7hofs = (int16_t)hofs; S.m7vofs = (int16_t)vofs;
    S.m7sel = (uint8_t)sel;
}

/* a state of G2 (tests/gpu/test_snes_m7.c) as a whole frame */
static int state_frame(const char *name, int a, int b, int c, int d, int x, int y, int hofs, int vofs, int sel)
{
    setup(77, 0);
    regs(a, b, c, d, x, y, hofs, vofs, sel);
    for (int l = 0; l < LINES; l++) {
        ppu_line_ref(l);
        memcpy(&refpic[l * SNES_W], &S.picture[l * SNES_W], SNES_W * 2);
    }
    memset(S.picture, 0xAB, sizeof S.picture);
    ppu_frame_begin(1);
    for (int l = 0; l < LINES; l++) ppu_line(l);
    ppu_frame_end();
    const int bad = memcmp(S.picture, refpic, sizeof refpic) != 0;
    CHECK(!bad, "%s differs", name);
    return bad;
}

static void play(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *rom = malloc((size_t)len);
    if (!rom || fread(rom, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(rom); return; }
    fclose(f);
    enum { FRAMES = 900 };
    static uint32_t hashes[2][FRAMES];
    uint32_t gpu_frames = 0;
    for (int pass = 0; pass < 2; pass++) {
        snes_gpu_set(NULL);
        if (pass) {
            static snes_gpu_ops_t ops;
            const qpu_snes_buf_t *b = qpu_snes_bufs();
            ops = (snes_gpu_ops_t){ b->vram, b->lut, b->tab, qpu_snes_m7, fake_us };
            snes_gpu_set(&ops);
        }
        if (snes_load(rom, (size_t)len, NULL) != 0) { printf("%s: not loaded\n", path); free(rom); return; }
        for (int i = 0; i < FRAMES; i++) {
            const uint16_t pad = (uint16_t)((i / 20) % 3 == 0 ? 0x1000 : (i / 20) % 3 == 1 ? 0x0080 : 0x8000);
            snes_frame(pad, 0, 1);
            uint32_t h = 2166136261u;
            const uint32_t *p = (const uint32_t *)S.picture;
            for (int k = 0; k < SNES_W * S.lines / 2; k++) h = (h ^ p[k]) * 16777619u;
            hashes[pass][i] = h;
        }
        if (pass) { ppu_gpu_stats_t st; ppu_gpu_stats(&st); gpu_frames = st.frames_gpu; }
    }
    int bad = 0;
    for (int i = 0; i < FRAMES; i++) bad += hashes[0][i] != hashes[1][i];
    CHECK(bad == 0, "%s: %d of %d frames differ with the helper on", path, bad, FRAMES);
    printf("rom %s: %d frames, %u drawn by the QPUs, %d differ\n", path, FRAMES, (unsigned)gpu_frames, bad);
    free(rom);
    snes_gpu_set(NULL);
}

static void roms(void)
{
    const char *dir = getenv("BM_SNES_ROMS");
    if (!dir || !*dir) { printf("BM_SNES_ROMS not set: no ROM frames\n"); return; }
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0;
    while (d && (e = readdir(d))) {
        char low[256];
        size_t i = 0;
        for (; e->d_name[i] && i < sizeof low - 1; i++) low[i] = (char)tolower((unsigned char)e->d_name[i]);
        low[i] = 0;
        if ((strstr(low, "castlevania") || strstr(low, "f-zero") || strstr(low, "fzero") || strstr(low, "mario kart")) &&
            (strstr(low, ".sfc") || strstr(low, ".smc"))) {
            char path[1024];
            snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
            play(path);
            n++;
        }
    }
    if (d) closedir(d);
    if (!n) printf("no Mode 7 ROM in %s\n", dir);
}

int main(int argc, char **argv)
{
    const int mode = argc > 1 ? atoi(argv[1]) : 0;
    const int cases = argc > 2 ? atoi(argv[2]) : 40;
    if (mode == 2) emu_qpu = 2;                         /* set before the start: the probe sees wrong data */
    ppu_reset_ref();
    ppu_reset();
    const int init = qpu_snes_init();
    if (mode == 2) {
        CHECK(init != 0 && !qpu_snes_m7_ok(), "a QPU that stores wrong data must turn Mode 7 off (init %d, %s)", init, qpu_snes_status());
    } else {
        CHECK(init == 0 && qpu_snes_m7_ok(), "init %d (%s)", init, qpu_snes_status());
    }
    static snes_gpu_ops_t ops;
    const qpu_snes_buf_t *b = qpu_snes_bufs();
    CHECK(b != NULL, "no buffers");
    if (!b) return 1;
    ops = (snes_gpu_ops_t){ b->vram, b->lut, b->tab, qpu_snes_m7, fake_us };
    snes_gpu_set(&ops);                                 /* also with the probe off: then the run is the ARM version */
    if (mode == 1) emu_qpu = 1;                         /* the QPUs stop answering after the start */

    state_frame("identity", 0x100, 0, 0, 0x100, 0, 0, 0, 0, 0);
    state_frame("scroll", 0x100, 0, 0, 0x100, 128, 112, 100, 60, 0);
    state_frame("rotation 45", 0x0B5, 0x0B5, -0x0B5, 0x0B5, 128, 112, 0, 0, 0);
    state_frame("zoom out, corner", 0x200, 0x040, -0x040, 0x200, 0x1FF0, 0x1F80, 0x1FF0, 0x1F80, 0);
    state_frame("mirror x and y", 0x100, 0, 0, 0x100, 0, 0, 0, 0, 3);
    state_frame("negative scale, flip x", -0x120, 0x30, 0x30, -0x120, 100, 100, 0x1F00, 0x1F00, 1);
    for (int rep = 0; rep < 4; rep++) {
        char n[48];
        snprintf(n, sizeof n, "screen over %d, outside", rep);
        state_frame(n, 0x0C0, 0x040, -0x040, 0x0C0, 0x300, 0x300, 0x300, 0x300, rep << 6);
        snprintf(n, sizeof n, "screen over %d, far out", rep);
        state_frame(n, 0x040, 0, 0, 0x040, 0, 0, 0x2000 - 256, 0x2000 - 256, rep << 6 | (rep & 1));
    }
    state_frame("largest matrix", 0x7FFF, 0x7FFF, -0x8000, 0x7FFF, 0, 0, 0, 0, 0);
    state_frame("smallest matrix", -0x8000, 0, 0, -0x8000, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0xC1);
    for (int i = 0; i < cases && failures < 6; i++) {
        char n[32];
        snprintf(n, sizeof n, "random state %d", i);
        state_frame(n, (int)(rnd() % 65536) - 32768, (int)(rnd() % 65536) - 32768, (int)(rnd() % 65536) - 32768,
                    (int)(rnd() % 65536) - 32768, (int)(rnd() & 0x1FFF), (int)(rnd() & 0x1FFF), (int)(rnd() & 0x1FFF),
                    (int)(rnd() & 0x1FFF), (int)(rnd() & 0xFF));
    }
    ppu_gpu_stats_t st0;
    ppu_gpu_stats(&st0);
    if (mode == 0) CHECK(st0.frames_gpu >= 16 && st0.lines_gpu >= 16u * LINES, "the QPUs drew %u frames, %u lines", (unsigned)st0.frames_gpu, (unsigned)st0.lines_gpu);
    if (mode == 2) CHECK(st0.lines_gpu == 0, "the probe is off but the QPUs drew %u lines", (unsigned)st0.lines_gpu);

    /* changes in the middle of the frame, each alone and mixed */
    for (int v = 1; v < 256 && failures < 6; v <<= 1) {
        char n[32];
        snprintf(n, sizeof n, "mid-frame change %02x", v);
        frame(n, 1000 + (uint64_t)v, v);
    }
    for (int i = 0; i < cases / 2 && failures < 6; i++) {
        char n[32];
        const int v = (int)(rnd() & 0xFF);
        snprintf(n, sizeof n, "mixed changes %d", i);
        frame(n, 5000 + (uint64_t)i, v);
    }

    /* a frame with no Mode 7 line at all is the ARM's, counted as such */
    ppu_gpu_stats_t a;
    ppu_gpu_stats(&a);
    setup(9, 0);
    S.bgmode = 1; S.tm = 7;
    ppu_frame_begin(1);
    for (int y = 0; y < LINES; y++) ppu_line(y);
    ppu_frame_end();
    ppu_gpu_stats_t c;
    ppu_gpu_stats(&c);
    CHECK(c.frames_arm == a.frames_arm + 1 && c.frames_gpu == a.frames_gpu, "a Mode 1 frame must count as an ARM frame");

    /* Mode 7 with the sprites on: the ARM draws it and the reason is counted */
    ppu_gpu_stats(&a);
    setup(10, 0);
    S.tm = 0x11;
    ppu_frame_begin(1);
    for (int y = 0; y < LINES; y++) ppu_line(y);
    ppu_frame_end();
    ppu_gpu_stats(&c);
    CHECK(c.rej_obj == a.rej_obj + LINES && c.m7_lines == a.m7_lines + LINES && c.frames_arm == a.frames_arm + 1 && c.rej_bg == a.rej_bg,
          "sprites on: rej_obj %u -> %u, m7_lines %u -> %u", (unsigned)a.rej_obj, (unsigned)c.rej_obj, (unsigned)a.m7_lines, (unsigned)c.m7_lines);

    /* ppu_line outside a frame (the tests) draws at once */
    setup(11, 0);
    memset(S.picture, 0xAB, sizeof S.picture);
    ppu_line(0);
    CHECK(S.picture[0] != 0xABAB || S.picture[1] != 0xABAB, "ppu_line without ppu_frame_begin must draw the line at once");

    if (mode == 1) {
        qpu_snes_stats_t q = qpu_snes_stats;
        CHECK(q.failures >= 1 && q.on_qpu == 0 && q.on_arm > 0, "failures %u on_qpu %u on_arm %u", (unsigned)q.failures, (unsigned)q.on_qpu, (unsigned)q.on_arm);
        CHECK(!qpu_snes_m7_ok(), "two failed runs in a row must turn the QPUs off");
    }
    if (mode == 0) {
        char line[480];
        emu_qpu = 0;
        const int r = qpu_snes_bench(fake_us, 2, 1u << 17, line, sizeof line);
        CHECK(r == 0 && !strncmp(line, "snes gpu bench:", 15), "bench %d: %s", r, line);
        printf("%s\n", line);
        roms();
    }
    qpu_snes_stats_t q = qpu_snes_stats;
    ppu_gpu_stats_t st;
    ppu_gpu_stats(&st);
    printf("snes gpu mode %d: %d checks, %d failed (QPU runs %u on the QPUs %u on the ARM %u failed %u; frames gpu %u arm %u)\n", mode,
           checks, failures, (unsigned)q.runs, (unsigned)q.on_qpu, (unsigned)q.on_arm, (unsigned)q.failures, (unsigned)st.frames_gpu, (unsigned)st.frames_arm);
    return failures != 0;
}
