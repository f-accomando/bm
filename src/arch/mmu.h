#ifndef MMU_H
#define MMU_H

#include <stdint.h>

/*
 * Flat (identity) mapping with 1 MiB sections, ARMv6 descriptor format:
 *   [0, arm_mem_end)          normal memory, write-back write-allocate cached
 *   [arm_mem_end, 0x20000000) GPU memory (framebuffer): normal, uncached,
 *                             bufferable, execute never
 *   [0x20000000, 0x21000000)  peripherals: shared device, execute never
 *   everything else           unmapped (translation fault)
 * Then enables MMU, L1 I/D caches and branch prediction.
 */
void mmu_init(uint32_t arm_mem_end);

int mmu_enabled(void);

#endif
