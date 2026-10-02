/* bmhost: the PC has coherent caches. */
#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>

#define CACHE_LINE 32

static inline void arm_dsb(void) { }
static inline void arm_isb(void) { }
static inline void dcache_clean_range(const volatile void *a, uint32_t n) { (void)a; (void)n; }
static inline void dcache_clean_invalidate_range(const volatile void *a, uint32_t n) { (void)a; (void)n; }
static inline void dcache_clean_all(void) { }
static inline void dcache_clean_invalidate_all(void) { }
static inline void icache_invalidate_all(void) { }

#endif
