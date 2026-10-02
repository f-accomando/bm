#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>

#if __ARM_ARCH >= 7
#define CACHE_LINE 64   /* Cortex-A53 (Pi Zero 2 W) L1 and L2 line size */

static inline void arm_dsb(void)
{
    __asm__ volatile("dsb" ::: "memory");
}

static inline void arm_isb(void)
{
    __asm__ volatile("isb" ::: "memory");
}
#else
#define CACHE_LINE 32   /* ARM1176 L1 line size */

/* ARM1176 (ARMv6) CP15 barriers. */
static inline void arm_dsb(void)
{
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 4" : : "r"(0) : "memory");
}

static inline void arm_isb(void)
{
    __asm__ volatile("mcr p15, 0, %0, c7, c5, 4" : : "r"(0) : "memory");
}
#endif

/* Write dirty lines of [addr, addr+len) back to memory (before a device
 * such as the GPU reads it). */
void dcache_clean_range(const volatile void *addr, uint32_t len);

/* Discard cached lines of [addr, addr+len) (after a device wrote it).
 * Also cleans them first, so partially covered lines are never lost. */
void dcache_clean_invalidate_range(const volatile void *addr, uint32_t len);

/* Writes every dirty line of the data cache back (16 KB on the ARM1176:
 * cheaper than a range when the range is much bigger than the cache; on
 * the Cortex-A53 every set and way of L1 and of the 512 KB L2). */
void dcache_clean_all(void);
/* The same, then drops every line (before a device writes cached RAM). */
void dcache_clean_invalidate_all(void);

void icache_invalidate_all(void);

#endif
