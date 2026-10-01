/*
 * AArch64 MMU: identity map with 4 KiB granule, 32-bit address space
 * (T0SZ = 32: four level-1 entries of 1 GiB, each pointing to a level-2
 * table of 2 MiB blocks).
 *   RAM          normal memory, write-back read/write-allocate, inner shareable
 *   peripherals  Device-nGnRE, never executed
 *   the rest     unmapped (translation fault)
 * Then the MMU, the data and instruction caches go on. Runs first thing in
 * kernel_main: until the MMU is on every data access is Device memory, so
 * library code (memset with DC ZVA, unaligned copies) must wait for it.
 */
#include "plat.h"
#include "a64.h"
#include "arch/mmu.h"

#define BLOCK_2M        (2u << 20)

#define ATTR_DEVICE     0               /* MAIR index: Device-nGnRE */
#define ATTR_NORMAL     1               /* Normal WB RW-allocate */
#define ATTR_NC         2               /* Normal non-cacheable */
#define MAIR_VALUE      ((0x04ull << 0) | (0xffull << 8) | (0x44ull << 16))

#define D_BLOCK         1ull
#define D_TABLE         3ull
#define D_ATTR(i)       ((uint64_t)(i) << 2)
#define D_SH_INNER      (3ull << 8)
#define D_AF            (1ull << 10)
#define D_PXN           (1ull << 53)
#define D_UXN           (1ull << 54)

static uint64_t l1[512] __attribute__((aligned(4096), section(".pagetables")));
static uint64_t l2[4][512] __attribute__((aligned(4096), section(".pagetables")));
static int mmu_on;

static void map_range(uint64_t start, uint64_t end, uint64_t flags)
{
    for (uint64_t a = start & ~(uint64_t)(BLOCK_2M - 1); a < end; a += BLOCK_2M) {
        unsigned gb = (unsigned)(a >> 30), i = (unsigned)(a >> 21) & 511;
        if (gb >= 4)
            break;
        l1[gb] = (uint64_t)(uintptr_t)l2[gb] | D_TABLE;
        l2[gb][i] = a | flags | D_AF | D_BLOCK;
    }
}

void mmu_init(uint32_t unused)
{
    (void)unused;
    map_range(PLAT_RAM_START, PLAT_RAM_END, D_ATTR(ATTR_NORMAL) | D_SH_INNER);
    map_range(PLAT_DEV_START, PLAT_DEV_END, D_ATTR(ATTR_DEVICE) | D_PXN | D_UXN);

    write_sysreg(mair_el1, MAIR_VALUE);
    write_sysreg(tcr_el1,
                 32ull                  /* T0SZ: 4 GiB */
                 | (1ull << 8)          /* IRGN0: WB WA */
                 | (1ull << 10)         /* ORGN0: WB WA */
                 | (3ull << 12)         /* SH0: inner shareable */
                 | (0ull << 14)         /* TG0: 4 KiB */
                 | (1ull << 23)         /* EPD1: no TTBR1 walks */
                 | (2ull << 32));       /* IPS: 40 bits */
    write_sysreg(ttbr0_el1, (uintptr_t)l1);
    dsb_sy();
    isb();
    __asm__ volatile("tlbi vmalle1\n dsb sy\n ic iallu\n dsb sy\n isb" ::: "memory");

    uint64_t sctlr = read_sysreg(sctlr_el1);
    sctlr |= (1u << 0) | (1u << 2) | (1u << 12);       /* M, C, I */
    sctlr &= ~(uint64_t)(1u << 1);                      /* no alignment faults */
    write_sysreg(sctlr_el1, sctlr);
    isb();
    mmu_on = 1;
}

int mmu_enabled(void)
{
    return mmu_on;
}

/* Marks [start, end) non-cacheable (2 MiB granularity), e.g. for buffers
 * a device writes; the TLB is flushed. */
void mmu_set_uncached(uintptr_t start, uintptr_t end)
{
    map_range(start, end, D_ATTR(ATTR_NC) | D_SH_INNER);
    __asm__ volatile("dsb ishst\n tlbi vmalle1\n dsb ish\n isb" ::: "memory");
}
