/* The V3D helper of the SNES PPU on the Pi Zero W (config key snes_gpu_m7=1): glue between core/src/gpu/qpu_snes.c and
 * snes_ppu.c. The RGB30 and the host builds do not have this file and draw every line on the ARM. */
#include "config.h"
#include "bm/snes_int.h"
#include "drivers/timer.h"
#include "gpu/qpu_snes.h"
#include "lib/printf.h"

void snes_gpu_enable(void)
{
    static snes_gpu_ops_t ops;
    if (!snes_gpu_m7_key())
        return;
    if (qpu_snes_init() != 0 || !qpu_snes_m7_ok()) {
        kprintf("snes gpu: Mode 7 on the QPUs is off (%s); the ARM PPU draws everything\n", qpu_snes_status());
        return;
    }
    const qpu_snes_buf_t *b = qpu_snes_bufs();
    ops.vram = b->vram;
    ops.lut = b->lut;
    ops.tab = b->tab;
    ops.m7 = qpu_snes_m7;
    ops.us = timer_ticks;
    snes_gpu_set(&ops);
    kprintf("snes gpu: Mode 7 on the QPUs is on (%s)\n", qpu_snes_status());
}
