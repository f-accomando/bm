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

/* The 1 MiB sections wholly inside [start, start + size) of the cached ARM
 * memory made normal uncached (writes merged in the write buffer: for
 * buffers the ARM writes and another master reads, M35) or cached again
 * (cached != 0). The data cache of the range is cleaned and invalidated
 * first, the TLBs after. The number of sections changed. */
int mmu_set_cached(const void *start, uint32_t size, int cached);

#endif
