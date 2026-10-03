#ifndef MMU_H
#define MMU_H

#include <stdint.h>

/*
 * Flat (identity) mapping with 1 MiB sections, ARMv6 descriptor format
 * (the same on ARMv7):
 *   [0, arm_mem_end)          normal memory, write-back write-allocate cached
 *   [arm_mem_end, ram_end)    GPU memory (framebuffer): normal, uncached,
 *                             bufferable, execute never
 *   [0x20000000, 0x21000000)  peripherals: shared device, execute never
 *                             (Pi Zero 2 W: [0x3F000000, 0x40100000), with
 *                             the ARM's own at 0x40000000)
 *   everything else           unmapped (translation fault)
 * ram_end is the end of the SDRAM (the GPU's share is on top), at most the
 * peripherals' start. Then enables MMU, I/D caches and branch prediction.
 */
void mmu_init(uint32_t arm_mem_end, uint32_t ram_end);

int mmu_enabled(void);

#endif
