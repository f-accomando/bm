/* drivers/mmio.h for tests/gpu/test_v3d.c: the V3D's registers are a model
 * in the test (mock_read, mock_write), not memory */
#ifndef MMIO_H
#define MMIO_H

#include <stdint.h>

#define PERIPHERAL_BASE 0x20000000u
#define GPU_MEM_BASE    0x40000000u
#define ARM_TO_BUS(a)   (((uint32_t)(uintptr_t)(a) & ~0xC0000000u) | GPU_MEM_BASE)

uint32_t mock_read(uint32_t reg);
void mock_write(uint32_t reg, uint32_t val);

static inline void mmio_write(uint32_t reg, uint32_t val) { mock_write(reg, val); }
static inline uint32_t mmio_read(uint32_t reg) { return mock_read(reg); }
static inline void dmb(void) {}

#endif
