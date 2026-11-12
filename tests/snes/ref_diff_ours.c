/* ref_diff_ours.c: our 65C816 (src/bm/snes_cpu.c, built with -DSNES_TRACE) driven for tests/snes/ref_diff.cpp.
 * The memory map is replaced by one flat 16 MB array (every page plain memory, 6 master clocks per access) so the harness sees every write in order.
 * Only the C core of the repository is in this file; the reference side (ref_diff.cpp) is a separate program part that reads an outside source tree.
 * Original code. */
#include <stdint.h>
#include <string.h>

#include "snes_int.h"

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
