#ifndef MMIO_H
#define MMIO_H

#include <stdint.h>

/* BCM2835 peripherals as seen from the ARM (physical address). */
#define PERIPHERAL_BASE 0x20000000u

/* SDRAM as seen from the VideoCore: 0x40000000 = L2-cached alias (Pi 1/Zero). */
#define GPU_MEM_BASE    0x40000000u
#define ARM_TO_BUS(a)   (((uint32_t)(a) & ~0xC0000000u) | GPU_MEM_BASE)
#define BUS_TO_ARM(a)   ((uint32_t)(a) & ~0xC0000000u)

static inline void mmio_write(uint32_t reg, uint32_t val)
{
    *(volatile uint32_t *)reg = val;
}

static inline uint32_t mmio_read(uint32_t reg)
{
    return *(volatile uint32_t *)reg;
}

/* ARMv6 data memory barrier. Required between accesses to different
 * BCM2835 peripherals, since the AXI bus may reorder them. */
static inline void dmb(void)
{
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 5" : : "r"(0) : "memory");
}

#endif
