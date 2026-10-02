/*
 * start.S of kernel7.img in Hyp mode. The Pi Zero 2 W's firmware starts
 * the kernel in Hyp mode, but QEMU's raspi2b in SVC: this runs start.S and
 * vectors.S in QEMU's virt machine with the virtualization extensions,
 * which starts in Hyp (make test-hyp). Checked on its PL011: the kernel
 * runs in SVC, VFP and NEON registers (d16-d31) and the generic counter
 * are not trapped to Hyp, an exception comes to our vectors (VBAR).
 */
#include <stdint.h>
#include "exceptions.h"

#define UART_DR 0x09000000u             /* virt's PL011 */

uint32_t boot_mode;                     /* written by start.S */
static int fails;

static void puts_(const char *s)
{
    while (*s)
        *(volatile uint32_t *)UART_DR = (uint8_t)*s++;
}

static void hex(uint32_t v)
{
    char b[11] = "0x";
    for (int i = 0; i < 8; i++)
        b[2 + i] = "0123456789abcdef"[v >> (28 - 4 * i) & 0xF];
    b[10] = 0;
    puts_(b);
}

static void check(int ok, const char *what)
{
    puts_(ok ? "ok   " : "FAIL ");
    puts_(what);
    puts_("\n");
    fails += !ok;
}

void kernel_main(uint32_t atags)
{
    (void)atags;
    uint32_t cpsr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
    puts_("hyp test: started in mode ");
    hex(boot_mode);
    puts_(", now ");
    hex(cpsr & 0x1F);
    puts_("\n");
    check(boot_mode == 0x1A, "the machine starts the kernel in Hyp mode");
    check((cpsr & 0x1F) == 0x13, "start.S went on in SVC mode");

    /* VFP and NEON, the upper registers too (kernel7.img uses d16-d31) */
    uint32_t lo = 0, hi = 0;
    __asm__ volatile("vmov.f64 d16, #1.5\n"
                     "vadd.f64 d17, d16, d16\n"
                     "vmov %0, %1, d17" : "=r"(lo), "=r"(hi) : : "d16", "d17");
    check(lo == 0 && hi == 0x40080000u, "VFP d16-d31 usable in SVC (1.5 + 1.5 = 3.0)");

    /* the generic counter, readable from SVC (CNTHCTL) */
    uint32_t c0, c1, h;
    __asm__ volatile("isb\n mrrc p15, 0, %0, %1, c14" : "=r"(c0), "=r"(h));
    for (volatile int i = 0; i < 100000; i++)
        ;
    __asm__ volatile("isb\n mrrc p15, 0, %0, %1, c14" : "=r"(c1), "=r"(h));
    check(c1 != c0, "generic counter readable from SVC");

    uint32_t vbar;
    __asm__ volatile("mrc p15, 0, %0, c12, c0, 0" : "=r"(vbar));
    extern char exception_vectors[];
    check(vbar == (uint32_t)exception_vectors, "VBAR points at exception_vectors");
    puts_("hyp test: SVC #0x42...\n");
    __asm__ volatile("svc #0x42");
    check(0, "the SVC came back instead of reaching exception_handler");
    for (;;)
        ;
}

void exception_handler(uint32_t type, exc_frame_t *f)
{
    uint32_t insn = *(volatile uint32_t *)f->pc;
    check(type == EXC_SWI && (insn & 0x0FFFFFFFu) == 0x0F000042u,
          "SVC #0x42 reached exception_handler through VBAR");
    puts_(fails ? "hyp test: FAILED\n" : "hyp test: all passed\n");
    for (;;)
        ;
}

void irq_handler(void)
{
}
