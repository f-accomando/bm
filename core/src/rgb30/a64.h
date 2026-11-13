/*
 * AArch64 (Cortex-A55) helpers shared by the RGB30 and QEMU virt builds:
 * system registers, barriers, interrupt masking, the exception frame.
 */
#ifndef A64_H
#define A64_H

/* exception types (vectors.S -> a64_exception) */
#define EXC64_SYNC      1
#define EXC64_IRQ       2
#define EXC64_FIQ       3
#define EXC64_SERROR    4

/* frame built by vectors.S: x0-x30, sp, elr, spsr, esr, far, type */
#define FRAME_TYPE      288
#define FRAME_SIZE      304

#ifndef __ASSEMBLER__
#include <stdint.h>

typedef struct {
    uint64_t x[31];
    uint64_t sp, elr, spsr, esr, far;
    uint64_t type, pad;
} a64_frame_t;

_Static_assert(sizeof(a64_frame_t) == FRAME_SIZE, "frame layout");

#define read_sysreg(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " #r : "=r"(_v)); _v; })
#define write_sysreg(r, v) __asm__ volatile("msr " #r ", %0" :: "r"((uint64_t)(v)) : "memory")

static inline void dsb_sy(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static inline void dmb_sy(void) { __asm__ volatile("dmb sy" ::: "memory"); }
static inline void isb(void)    { __asm__ volatile("isb" ::: "memory"); }

void a64_exception(uint64_t type, a64_frame_t *f) __attribute__((noreturn));

/* the device tree U-Boot passed (0 in QEMU) */
extern uintptr_t a64_dtb;

#endif
#endif
