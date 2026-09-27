#include "cache.h"

void dcache_clean_range(const volatile void *addr, uint32_t len)
{
    uint32_t a = (uint32_t)addr & ~(CACHE_LINE - 1);
    uint32_t end = (uint32_t)addr + len;

    for (; a < end; a += CACHE_LINE)
        __asm__ volatile("mcr p15, 0, %0, c7, c10, 1" : : "r"(a) : "memory");
    arm_dsb();
}

void dcache_clean_invalidate_range(const volatile void *addr, uint32_t len)
{
    uint32_t a = (uint32_t)addr & ~(CACHE_LINE - 1);
    uint32_t end = (uint32_t)addr + len;

    for (; a < end; a += CACHE_LINE)
        __asm__ volatile("mcr p15, 0, %0, c7, c14, 1" : : "r"(a) : "memory");
    arm_dsb();
}

void icache_invalidate_all(void)
{
    __asm__ volatile("mcr p15, 0, %0, c7, c5, 0" : : "r"(0) : "memory");
    arm_isb();
}

void dcache_clean_all(void)
{
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 0" : : "r"(0) : "memory");
    arm_dsb();
}

void dcache_clean_invalidate_all(void)
{
    __asm__ volatile("mcr p15, 0, %0, c7, c14, 0" : : "r"(0) : "memory");
    arm_dsb();
}
