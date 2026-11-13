/* ref_diff_ours.c: our 65C816 (core/src/bm/snes_cpu.c, built with -DSNES_TRACE) driven for tests/snes/ref_diff.cpp.
 * The memory map is replaced by one flat 16 MB array (every page plain memory, 6 master clocks per access) so the harness sees every write in order.
 * Only the C core of the repository is in this file; the reference side (ref_diff.cpp) is a separate program part that reads an outside source tree.
 * Original code. */
#include <stdint.h>
#include <string.h>

#include "snes_int.h"

#include "snes_gsu.h"
#include "ref_diff_ours.h"

static uint8_t *flat;
rd_write_t ours_w[RD_MAXW];
int ours_nw;

static void on_write(uint32_t a, uint8_t v, uint32_t pc)
{
    (void)pc;
    if (ours_nw < RD_MAXW) { ours_w[ours_nw].addr = a & 0xFFFFFF; ours_w[ours_nw].val = v; ours_w[ours_nw].old = flat[a & 0xFFFFFF]; }
    ours_nw++;
}

void ours_init(uint8_t *mem16mb)
{
    flat = mem16mb;
    for (int i = 0; i < 4096; i++) {
        snes_rtab[i] = flat + (size_t)i * 4096;
        snes_wtab[i] = flat + (size_t)i * 4096;
        snes_ctab[i] = 6;
    }
    snes_trace.wr = on_write;
}

int ours_step(const rd_regs_t *in, rd_regs_t *out, int nmi, int irq, int *waiting, int *stopped)
{
    ours_nw = 0;
    S.nmi_pending = nmi;
    S.irq_pending = irq;
    cpu_set((const cpu_regs_t *)in);
    snes_clk = 0;
    cpu_run(1);
    cpu_get((cpu_regs_t *)out);
    *waiting = cpu_waiting;
    *stopped = cpu_stopped;
    S.nmi_pending = S.irq_pending = 0;
    return snes_clk;
}

/* ---- the Super FX */
void ours_gsu_load(const uint8_t *rom128k)
{
    static uint8_t rom[0x20000];
    memcpy(rom, rom128k, sizeof rom);
    snes_load(rom, sizeof rom, NULL);
}
uint8_t *ours_gsu_ram(void) { return S.sram; }
unsigned ours_gsu_ram_size(void) { return S.sram_size; }
void ours_wr(uint32_t addr, uint8_t v) { snes_write(addr, v); }
int ours_gsu_run(int max_lines)
{
    int l = 0;
    const uint32_t i0 = gsu_instr_count();
    while (gsu_go() && l < max_lines) { gsu_run(1364); l++; }
    if (gsu_go()) return -1;
    return (int)(gsu_instr_count() - i0);
}
void ours_gsu_state(rd_gsu_t *o)
{
    gsu_dbg_t d;
    gsu_dbg(&d);
    for (int i = 0; i < 16; i++) o->r[i] = d.r[i];
    o->pbr = d.pbr; o->rombr = d.rombr; o->rambr = d.rambr; o->scbr = d.scbr; o->scmr = d.scmr; o->colr = d.colr; o->por = d.por; o->clsr = d.clsr;
    o->sfr_lo = d.sfr_lo; o->cbr = d.cbr;
}
