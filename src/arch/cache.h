#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>

#ifdef __aarch64__
#define CACHE_LINE 64   /* Cortex-A55 (RGB30 build, src/rgb30/cache.c) */

static inline void arm_dsb(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static inline void arm_isb(void) { __asm__ volatile("isb" ::: "memory"); }
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

/* Writes every dirty line of the data cache back (16 KB: cheaper than a
 * range when the range is much bigger than the cache). */
void dcache_clean_all(void);
/* The same, then drops every line (before a device writes cached RAM). */
void dcache_clean_invalidate_all(void);

void icache_invalidate_all(void);

#endif
