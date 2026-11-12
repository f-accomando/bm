/* gsu_diff.c: the Super FX interpreter (src/bm/snes_gsu.c) against the plain C reference (tests/snes/gsu_ref.c) on random programs.
 * Each program is random bytes in a made-up ROM (and RAM), run in pieces of random length (the budget carries from one piece to the next), with random registers,
 * SCMR, SCBR, CFGR, CLSR, PBR and cart RAM size. After each piece the registers, flags, prefix state, instruction count, IRQ and a hash of the cart RAM must be equal.
 * Run: make test-snes-gsu-diff (host), make test-snes-gsu-arm (also on the ARM1176 in QEMU). Original code. */
#include "snes_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ref_reset(uint32_t ram_size);
void ref_write(uint16_t off, uint8_t v);
void ref_run(int clocks);
int ref_irq_line(void);
uint32_t ref_instr_count(void);
void ref_dbg(gsu_dbg_t *d);

#define STEPS 24
typedef struct { gsu_dbg_t d; uint32_t insns, hash; int irq_line, irq_pending; } snap_t;

static uint32_t rng_s;
static uint32_t rnd(void) { rng_s ^= rng_s << 13; rng_s ^= rng_s >> 17; rng_s ^= rng_s << 5; return rng_s; }
static uint32_t dfnv(const uint8_t *p, size_t n)         /* a hash of words: fast on the ARM1176 in QEMU */
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i + 4 <= n; i += 4) { uint32_t w; memcpy(&w, p + i, 4); h = (h ^ w) * 16777619u; h ^= h >> 15; }
    return h;
}

#define DROM_SIZE 0x40000
static uint8_t drom[DROM_SIZE];
static uint8_t sram0[131072];

typedef struct { void (*reset)(uint32_t); void (*write)(uint16_t, uint8_t); void (*run)(int); int (*irq)(void); uint32_t (*insns)(void); void (*dbg)(gsu_dbg_t *); } impl_t;
static const impl_t IMPL[2] = {
    { ref_reset, ref_write, ref_run, ref_irq_line, ref_instr_count, ref_dbg },
    { gsu_reset, gsu_write, gsu_run, gsu_irq_line, gsu_instr_count, gsu_dbg },
};

static void play(const impl_t *im, uint32_t seed, snap_t *out)
{
    rng_s = seed;
    const uint32_t ram = 0x8000u << (rnd() % 3);
    im->reset(ram);
    memcpy(S.sram, sram0, sizeof S.sram);
    S.irq_pending = 0;
    const unsigned banks[8] = { 0x00, 0x01, 0x02, 0x20, 0x40, 0x41, 0x70, 0x71 };
    const unsigned pbr = banks[rnd() & 7];
    const unsigned alt = rnd() & 0x13;
    im->write(0x3030, (uint8_t)(rnd() & 0x1E));                 /* flags, go = 0 */
    im->write(0x3031, (uint8_t)alt);
    im->write(0x3034, (uint8_t)pbr);
    im->write(0x3037, (uint8_t)(rnd() & 0xA0));
    im->write(0x3038, (uint8_t)rnd());
    im->write(0x3039, (uint8_t)(rnd() & 1));
    im->write(0x303A, (uint8_t)rnd());
    for (int n = 0; n < 15; n++) {
        const uint32_t v = rnd();
        im->write((uint16_t)(0x3000 + 2 * n), (uint8_t)v);
        im->write((uint16_t)(0x3001 + 2 * n), (uint8_t)(v >> 8));
    }
    const uint32_t pc = rnd() & 0xFFFF;
    im->write(0x301E, (uint8_t)pc);
    im->write(0x301F, (uint8_t)(pc >> 8));                      /* R15 starts the program */
    static const int clk[8] = { 1, 2, 3, 17, 100, 1364, 1365, 5000 };
    for (int s = 0; s < STEPS; s++) {
        im->run(clk[rnd() & 7]);
        snap_t *p = &out[s];
        memset(p, 0, sizeof *p);
        im->dbg(&p->d);
        p->insns = im->insns(); p->irq_line = im->irq(); p->irq_pending = S.irq_pending;
        p->hash = dfnv(S.sram, sizeof S.sram);
    }
    im->write(0x3030, 0);                                       /* stop: the pixel cache is written */
    snap_t *p = &out[STEPS];
    memset(p, 0, sizeof *p);
    im->dbg(&p->d);
    p->hash = dfnv(S.sram, sizeof S.sram);
}

int gsu_diff_main(int programs)
{
    static snap_t a[STEPS + 1], b[STEPS + 1];
    int fails = 0;
    uint32_t total = 0;
    const uint8_t *keep_rom = S.rom; const uint32_t keep_size = S.rom_size;     /* snes_load frees S.rom: give it back */
    rng_s = 12345;
    for (int pr = 0; pr < programs && fails < 5; pr++) {
        const uint32_t seed = (uint32_t)(pr + 1) * 2654435761u | 1;
        rng_s = seed ^ 0x9E3779B9u;
        /* the data: random bytes with few STOP (0) so that the programs run; a CACHE at the start of half of them */
        for (size_t i = 0; i < sizeof drom; i++) { uint32_t r = rnd(); drom[i] = (uint8_t)r; if (!drom[i] && (r >> 24) < 250) drom[i] = 1; }
        for (size_t i = 0; i < sizeof sram0; i++) { uint32_t r = rnd(); sram0[i] = (uint8_t)r; if (!sram0[i] && (r >> 24) < 250) sram0[i] = 1; }
        if (pr & 1) for (size_t i = 0; i < sizeof drom; i += 16) drom[i] = 0x02;   /* a CACHE at each line start: the cache is set up */
        S.rom = drom; S.rom_size = DROM_SIZE;
        play(&IMPL[0], seed, a);
        play(&IMPL[1], seed, b);
        for (int s = 0; s <= STEPS; s++) {
            if (!memcmp(&a[s], &b[s], sizeof a[s])) continue;
            fails++;
            printf("FAIL gsu diff: program %d seed %08x step %d: insns %u/%u hash %08x/%08x irq %d/%d pend %d/%d\n", pr, seed, s,
                   a[s].insns, b[s].insns, a[s].hash, b[s].hash, a[s].irq_line, b[s].irq_line, a[s].irq_pending, b[s].irq_pending);
            for (int i = 0; i < 16; i++) if (a[s].d.r[i] != b[s].d.r[i]) printf("  R%d %04x/%04x\n", i, a[s].d.r[i], b[s].d.r[i]);
            printf("  sfr %02x/%02x alt %d/%d b %d/%d pbr %02x/%02x rombr %02x/%02x rambr %d/%d colr %02x/%02x por %02x/%02x cbr %04x/%04x go %d/%d\n",
                   a[s].d.sfr_lo, b[s].d.sfr_lo, a[s].d.alt, b[s].d.alt, a[s].d.b, b[s].d.b, a[s].d.pbr, b[s].d.pbr, a[s].d.rombr, b[s].d.rombr,
                   a[s].d.rambr, b[s].d.rambr, a[s].d.colr, b[s].d.colr, a[s].d.por, b[s].d.por, a[s].d.cbr, b[s].d.cbr, a[s].d.go, b[s].d.go);
            break;
        }
        total += a[STEPS - 1].insns;
    }
    S.rom = (uint8_t *)keep_rom; S.rom_size = keep_size;
    printf("gsu diff: %d programs, %u instructions, %s\n", programs, total, fails ? "FAILED" : "ok");
    return fails != 0;
}

#ifndef GSU_DIFF_NO_MAIN
int main(int argc, char **argv) { return gsu_diff_main(argc > 1 ? atoi(argv[1]) : 3000); }
#endif
