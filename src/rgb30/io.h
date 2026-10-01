/*
 * Register access for the AArch64 build (identity mapped: physical
 * addresses are pointers), and the Rockchip "hiword" convention: the top
 * 16 bits of a write say which of the low 16 bits it changes.
 */
#ifndef RGB30_IO_H
#define RGB30_IO_H

#include <stdint.h>

static inline uint32_t readl(uintptr_t a)
{
    return *(volatile uint32_t *)a;
}

static inline void writel(uintptr_t a, uint32_t v)
{
    *(volatile uint32_t *)a = v;
}

static inline void setbits(uintptr_t a, uint32_t bits)   { writel(a, readl(a) | bits); }
static inline void clrbits(uintptr_t a, uint32_t bits)   { writel(a, readl(a) & ~bits); }

static inline void clrsetbits(uintptr_t a, uint32_t clr, uint32_t set)
{
    writel(a, (readl(a) & ~clr) | set);
}

/* Rockchip: write `val` into the bits of `mask` (both within the low 16) */
#define HIWORD(mask, val)   (((uint32_t)(mask) << 16) | ((uint32_t)(val) & (mask)))

static inline void rk_write_mask(uintptr_t a, uint32_t mask, uint32_t val)
{
    writel(a, HIWORD(mask, val));
}

#endif
