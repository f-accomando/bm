#ifndef MMIO_H
#define MMIO_H

#include <stdint.h>

#ifdef BM_ZERO2
/* BCM2710A1 (Pi Zero 2 W, kernel7.img): the BCM2835's peripherals, seen
 * at 0x3F000000; the ARM's own (core timers, mailboxes, interrupt routing
 * of the four cores) at 0x40000000. */
#define PERIPHERAL_BASE 0x3F000000u
#define LOCAL_BASE      0x40000000u

/* SDRAM as seen from the VideoCore: 0xC0000000 = the uncached alias (the
 * Cortex-A53 has its own L2; the VideoCore's L2 is not for the ARM). */
#define GPU_MEM_BASE    0xC0000000u
#else
/* BCM2835 peripherals as seen from the ARM (physical address). */
#define PERIPHERAL_BASE 0x20000000u

/* SDRAM as seen from the VideoCore: 0x40000000 = L2-cached alias (Pi 1/Zero). */
#define GPU_MEM_BASE    0x40000000u
#endif
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

/* Data memory barrier. Required between accesses to different BCM2835
 * peripherals, since the AXI bus may reorder them. */
static inline void dmb(void)
{
#if __ARM_ARCH >= 7
    __asm__ volatile("dmb" ::: "memory");
#else
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 5" : : "r"(0) : "memory");
#endif
}

#endif
