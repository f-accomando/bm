/* bmhost: the kernel's mmio.h without the ARM instructions (the code that
 * touches peripherals is compiled but never runs on the PC). */
#ifndef MMIO_H
#define MMIO_H

#include <stdint.h>

#define PERIPHERAL_BASE 0x20000000u
#define GPU_MEM_BASE    0x40000000u
#define ARM_TO_BUS(a)   (((uint32_t)(uintptr_t)(a) & ~0xC0000000u) | GPU_MEM_BASE)
#define BUS_TO_ARM(a)   ((uint32_t)(a) & ~0xC0000000u)

static inline void mmio_write(uint32_t reg, uint32_t val) { (void)reg; (void)val; }
static inline uint32_t mmio_read(uint32_t reg) { (void)reg; return 0; }
static inline void dmb(void) { }

#endif
