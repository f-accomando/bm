/* dsp_ref.cpp: compares the S-DSP of bm (src/bm/snes_dsp.c) with the one of ares (ISC licence), sample by sample.
 * ares is NOT part of this repository. Get it into a directory of your own and give the path:
 *     git clone --depth 1 https://github.com/ares-emulator/ares.git /path/to/ares
 *     make test-snes-audio-ref ARES=/path/to/ares
 * The program includes the DSP files of ares from that directory and adds only a few empty stubs (Thread, Node). Nothing of ares is
 * copied here. Both chips get the same sound RAM and the same register writes (random voices, as in dsp_golden.c, and a game-like
 * set up with echo); after each sample the output and the readable registers are compared. A write is applied between two samples.
 *   dsp_ref game <rom> <frames> [from]   a real game: the core runs the ROM, every register write and the sound RAM go to ares too, and the output is
 *                                  compared at each sample (the first difference is reported with the voice states). The ROM is yours; nothing of it is stored.
 *   dsp_ref [cases] [samples]      the number of random cases (default 40) and the length of each in samples (default 20000)
 * Original code. */
#include <nall/nall.hpp>
using namespace nall;
using namespace nall::primitives;
#include <ares/types.hpp>
#define private public

namespace ares {
namespace Node { struct Object {}; namespace Audio { struct Stream {}; } namespace Debugger { struct Memory {}; } }
struct Thread { };
}

namespace ares::SuperFamicom {
struct Smp {} smp;
}

namespace ares::SuperFamicom {
#include <sfc/dsp/dsp.hpp>
}

extern "C" {
#include "snes_int.h"
extern uint8_t *spc_ram_ptr(void);
extern uint8_t *spc_dsp_ptr(void);
int snes_dsp_pop(int16_t out[2]);
int snes_dsp_dbg(int v, int what);
extern void (*snes_dsp_wr_hook)(int reg, uint8_t val);
extern void (*snes_dsp_tick_hook)(void);
extern void (*snes_dsp_pre_hook)(void);
int snes_load(const uint8_t *data, size_t len, const char **why);
void snes_frame(uint16_t pad1, uint16_t pad2, int render);
}

namespace ares::SuperFamicom {
DSP dsp;
static std::vector<std::pair<int, int>> g_out;
#include <sfc/dsp/memory.cpp>
#include <sfc/dsp/gaussian.cpp>
#include <sfc/dsp/counter.cpp>
#include <sfc/dsp/envelope.cpp>
#include <sfc/dsp/brr.cpp>
#include <sfc/dsp/misc.cpp>
#include <sfc/dsp/voice.cpp>
#include <sfc/dsp/echo.cpp>
/* DSP::main (the order of the steps) is cut out of ares/sfc/dsp/dsp.cpp by the Makefile into dsp_ref_main.inc */
auto DSP::tick() -> void {}
auto DSP::sample(i16 l, i16 r) -> void { g_out.push_back({l, r}); }
auto DSP::load(Node::Object) -> void {}
auto DSP::unload() -> void {}
auto DSP::power(bool reset) -> void
{
  if(!reset) { for(auto& b : apuram) b = 0; for(auto& b : registers) b = 0; }
  clock = {}; mainvol = {}; echo = {}; noise = {}; brr = {};
  latch = {};
  for(u32 n : range(8)) { voice[n] = {}; voice[n].index = n << 4; }
  gaussianConstructTable();
}
#include "dsp_ref_main.inc"
}

static uint64_t rs;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }

using namespace ares::SuperFamicom;

static int g_mode;
static void wr(int reg, int v)
{
    dsp.write(reg, v);
    dsp_write(reg, (uint8_t)v);
}

static void setup(int c, int game)
{
    rs = 0x2545F4914F6CDD1Dull + (uint64_t)c * 104729u + (uint64_t)game * 7;
    spc_reset();
    dsp_reset();
    dsp.power(false);
    uint8_t *ram = spc_ram_ptr();
    for (int i = 0; i < 65536; i++) ram[i] = (uint8_t)rnd();
    for (int i = 0; i < 8; i++) {
        const unsigned s = 0x2000 + (rnd() % 0x1000) * 9, l = 0x2000 + (rnd() % 0x1000) * 9;
        ram[0x1000 + i * 4] = (uint8_t)s; ram[0x1000 + i * 4 + 1] = (uint8_t)(s >> 8);
        ram[0x1000 + i * 4 + 2] = (uint8_t)l; ram[0x1000 + i * 4 + 3] = (uint8_t)(l >> 8);
    }
    for (unsigned a = 0x2000; a + 9 <= 0x2000 + 0x1000 * 9 + 9 && a < 0xFFF0; a += 9) {
        const unsigned range = (rnd() % 8 == 0) ? 13 + rnd() % 3 : rnd() % 13, filter = rnd() % 4;
        const unsigned end = (rnd() % 6 == 0), loop = (rnd() % 2);
        ram[a] = (uint8_t)((range << 4) | (filter << 2) | (loop << 1) | end);
    }
    for (int i = 0; i < 65536; i++) dsp.apuram[i] = ram[i];
    for (int i = 0; i < 8; i++) {
        wr(i * 16 + 0, rnd() & 255); wr(i * 16 + 1, rnd() & 255);
        const unsigned pitch = (c % 3 == 0) ? 0x1000 + rnd() % 0x400 : rnd() % 0x3FFF;
        wr(i * 16 + 2, pitch & 255); wr(i * 16 + 3, pitch >> 8);
        wr(i * 16 + 4, rnd() % 8);
        wr(i * 16 + 5, (rnd() % 4 ? 0x80 : 0) | (rnd() & 0x7F));
        wr(i * 16 + 6, rnd() & 255);
        wr(i * 16 + 7, rnd() & 255);
        wr(i * 16 + 15, rnd() & 255);                                  /* the FIR taps */
    }
    wr(0x0C, 0x40 + rnd() % 0x3F); wr(0x1C, 0x40 + rnd() % 0x3F);
    wr(0x2C, rnd() & 255); wr(0x3C, rnd() & 255);                      /* the echo volumes */
    wr(0x0D, rnd() & 255);                                             /* feedback */
    wr(0x5D, 0x10);
    wr(0x2D, rnd() & 255);
    wr(0x3D, rnd() % 3 == 0 ? rnd() & 255 : 0);
    wr(0x4D, rnd() & 255);                                             /* echo on */
    wr(0x6D, 0xC0 + rnd() % 0x20);                                     /* ESA: a page in the middle of the RAM */
    wr(0x7D, rnd() % 4);                                               /* EDL */
    if (g_mode & 1) { for (int i = 1; i < 8; i++) { wr(i * 16, 0); wr(i * 16 + 1, 0); } }      /* only voice 0 is heard */
    if (g_mode & 2) { wr(0x2C, 0); wr(0x3C, 0); wr(0x4D, 0); wr(0x0D, 0); }                    /* no echo */
    if (g_mode & 4) { wr(0x2D, 0); wr(0x3D, 0); }                                              /* no modulation, no noise */
    wr(0x6C, (rnd() & 0x1F) | (game ? 0 : 0x20) | 0);                  /* not muted, not in reset */
}

static int cmp_regs(int *bad)
{
    for (int i = 0; i < 8; i++) {
        if (dsp.registers[i * 16 + 8] != spc_dsp_ptr()[i * 16 + 8]) { *bad = i * 16 + 8; return 1; }
        if (dsp.registers[i * 16 + 9] != spc_dsp_ptr()[i * 16 + 9]) { *bad = i * 16 + 9; return 1; }
    }
    if (dsp.registers[0x7C] != spc_dsp_ptr()[0x7C]) { *bad = 0x7C; return 1; }
    return 0;
}

/* ------------------------------------------------------------------ a real game */
static long g_ticks, g_diff, g_regdiff;
static int g_first = -1;
static void game_wr(int reg, uint8_t v) { dsp.write(reg, v); }
static void game_pre(void)
{
    uint8_t *ram = spc_ram_ptr();                                  // the sound RAM as the program left it, before the chip of bm writes the echo
    if (memcmp(ram, (const uint8_t *)dsp.apuram, 65536)) for (int i = 0; i < 65536; i++) dsp.apuram[i] = ram[i];
}
static void game_tick(void)
{
    dsp.main();
    int16_t o[2];
    snes_dsp_pop(o);
    const auto a = g_out.back();
    g_out.clear();
    if (a.first != o[0] || a.second != o[1]) {
        g_diff++;
        if (g_first < 0) {
            g_first = (int)g_ticks;
            printf("first output difference at sample %ld: bm %d,%d ares %d,%d\n", g_ticks, o[0], o[1], a.first, a.second);
            for (int v = 0; v < 8; v++)
                printf("  voice %d bm: mode %d env %d delay %d goff %04X addr %04X | ares: mode %d env %d delay %d goff %04X addr %04X\n", v,
                       snes_dsp_dbg(v, 0), snes_dsp_dbg(v, 1), snes_dsp_dbg(v, 2), snes_dsp_dbg(v, 3), snes_dsp_dbg(v, 4), (int)dsp.voice[v].envelopeMode,
                       (int)dsp.voice[v].envelope, (int)dsp.voice[v].keyonDelay, (int)dsp.voice[v].gaussianOffset, (int)dsp.voice[v].brrAddress);
        }
    }
    int bad;
    if (cmp_regs(&bad)) { g_regdiff++; if (g_regdiff == 1) printf("first register difference: %02X at sample %ld: bm %02X ares %02X\n", bad, g_ticks, spc_dsp_ptr()[bad], (int)dsp.registers[bad]); }
    g_ticks++;
}

static int game(const char *rom, int frames)
{
    FILE *f = fopen(rom, "rb");
    if (!f) { printf("cannot open %s\n", rom); return 2; }
    fseek(f, 0, SEEK_END); const long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = (uint8_t *)malloc(n);
    if (fread(d, 1, n, f) != (size_t)n) { fclose(f); return 2; }
    fclose(f);
    const char *why;
    if (snes_load(d, n, &why)) { printf("load: %s\n", why); return 2; }
    dsp.power(false);
    g_out.clear();
    snes_dsp_wr_hook = game_wr;
    snes_dsp_tick_hook = game_tick;
    snes_dsp_pre_hook = game_pre;
    for (int fr = 0; fr < frames; fr++) snes_frame(0, 0, 0);
    printf("game %s: %ld samples, %ld differ, %ld register differences\n", rom, g_ticks, g_diff, g_regdiff);
    return g_diff != 0;
}

int main(int argc, char **argv)
{
    if (argc > 3 && !strcmp(argv[1], "game")) return game(argv[2], atoi(argv[3]));
    const int cases = argc > 1 ? atoi(argv[1]) : 40, N = argc > 2 ? atoi(argv[2]) : 20000;
    g_mode = argc > 3 ? atoi(argv[3]) : 0;
    int total_bad = 0;
    for (int c = 0; c < cases; c++) {
        const int game = c & 1;
        setup(c, game);
        g_out.clear();
        int first = -1, regbad = -1, nreg = 0, nout = 0;
        for (int t = 0; t < N; t++) {
            if (t % 700 == 0) { wr(0x4C, rnd() & 255); if (t % 2100 == 1400) wr(0x5C, rnd() & 255); else wr(0x5C, 0); }
            if (t % 1500 == 1000 && game) { wr(0x0C, rnd() & 255); wr(0x6D, 0xC0 + rnd() % 0x20); wr(0x7D, rnd() % 6); }
            if (t % 3100 == 3000) wr(0x6C, (rnd() & 0x1F) | (rnd() % 4 == 0 ? 0x20 : 0) | (rnd() % 8 == 0 ? 0x40 : 0));
            dsp.main();
            dsp_tick();
            int16_t o[2];
            snes_dsp_pop(o);
            const auto a = g_out.back();
            nout++;
            if (first < 0 && (a.first != o[0] || a.second != o[1])) {
                first = t;
                printf("  case %d: first difference at sample %d: bm %d,%d  ares %d,%d\n", c, t, o[0], o[1], a.first, a.second);
            }
            int bad;
            if (regbad < 0 && cmp_regs(&bad)) { regbad = t; printf("  case %d: register %02X differs at sample %d: bm %02X ares %02X\n", c, bad, t,
                                                                     spc_dsp_ptr()[bad], (int)dsp.registers[bad]);
                const int v = bad >> 4;
                if (v < 8) printf("    voice %d bm: mode %d env %d delay %d goff %04X addr %04X | ares: mode %d env %d delay %d goff %04X addr %04X\n", v,
                    snes_dsp_dbg(v, 0), snes_dsp_dbg(v, 1), snes_dsp_dbg(v, 2), snes_dsp_dbg(v, 3), snes_dsp_dbg(v, 4),
                    (int)dsp.voice[v].envelopeMode, (int)dsp.voice[v].envelope, (int)dsp.voice[v].keyonDelay, (int)dsp.voice[v].gaussianOffset, (int)dsp.voice[v].brrAddress); }
            nreg++;
            if (first >= 0 && regbad >= 0) break;
        }
        int ramdiff = 0;
        for (int i = 0; i < 65536; i++) if (dsp.apuram[i] != spc_ram_ptr()[i]) ramdiff++;
        printf("case %d: %s%s ram differences %d\n", c, first < 0 ? "output equal" : "OUTPUT DIFFERS", regbad < 0 ? ", registers equal," : ", REGISTERS DIFFER,", ramdiff);
        if (first >= 0 || regbad >= 0 || ramdiff) total_bad++;
    }
    printf("dsp_ref: %d of %d cases differ\n", total_bad, cases);
    return total_bad != 0;
}
