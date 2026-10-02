/*
 * Entry point for Raspberry Pi Zero / Zero W (BCM2835, ARM1176JZF-S) and,
 * built for ARMv7 as kernel7.img, the Pi Zero 2 W (BCM2710A1, Cortex-A53
 * in 32 bit).
 *
 * The GPU firmware (start.elf), or the serial chainloader, jumps to 0x8000
 * with MMU and D-cache off:
 *   r0 = 0, r1 = machine type, r2 = ATAGS/DTB address
 * The ARM1176 is in SVC mode. The Cortex-A53 is in HYP mode (the firmware's
 * stub at 0x0, armstub7), and its three other cores wait in that stub for
 * an address in their mailbox: they stay there, and 0x0 is left alone.
 */

#define MODE_FIQ 0x11
#define MODE_IRQ 0x12
#define MODE_SVC 0x13
#define MODE_ABT 0x17
#define MODE_HYP 0x1A
#define MODE_UND 0x1B

#if __ARM_ARCH >= 7
    .arch_extension virt
#endif

    .section .text.boot
    .arm
    .globl _start
_start:
    b       3f
    /* +4: what the image is, checked before a kernel received over the
     * network replaces this one (src/net/netxfer.c): "bmK6" kernel.img
     * (ARMv6, BCM2835), "bmK7" kernel7.img (Pi Zero 2 W) */
    .ascii  "bmK"
#ifdef BM_ZERO2
    .ascii  "7"
#else
    .ascii  "6"
#endif
3:
    cpsid   if                      @ mask IRQ and FIQ
    mov     r4, r2                  @ keep ATAGS/DTB pointer for kernel_main
    mrs     r11, cpsr               @ the mode we were started in (boot_mode)

#if __ARM_ARCH >= 7
    /* One core only: the others never come here from the Pi's firmware,
     * but QEMU's raspi2b starts all four at 0x8000. */
    mrc     p15, 0, r0, c0, c0, 5   @ MPIDR
    ands    r0, r0, #3
    bne     park

    /* HYP -> SVC, as Linux's hyp-stub does: nothing trapped to HYP (VFP
     * and NEON included), the generic counter readable from SVC. */
    mrs     r0, cpsr
    and     r0, r0, #0x1F
    cmp     r0, #MODE_HYP
    bne     2f
    mov     r0, #0
    mcr     p15, 4, r0, c1, c1, 0   @ HCR
    mcr     p15, 4, r0, c1, c1, 2   @ HCPTR
    mcr     p15, 4, r0, c1, c1, 3   @ HSTR
    mrc     p15, 4, r0, c1, c1, 1   @ HDCR: only HPMN kept
    and     r0, r0, #0x1F
    mcr     p15, 4, r0, c1, c1, 1
    mrc     p15, 4, r0, c14, c1, 0  @ CNTHCTL: PL1PCEN | PL1PCTEN
    orr     r0, r0, #3
    mcr     p15, 4, r0, c14, c1, 0
    mov     r0, #(MODE_SVC | 0x1C0) @ SVC, A I F masked, ARM state
    msr     spsr_cxsf, r0           @ in HYP: SPSR_hyp
    adr     r0, 2f
    msr     elr_hyp, r0
    eret
2:
#endif

    /* One stack per exception mode, then back to SVC. */
    cps     #MODE_UND
    ldr     sp, =__und_stack_top
    cps     #MODE_ABT
    ldr     sp, =__abt_stack_top
    cps     #MODE_FIQ
    ldr     sp, =__fiq_stack_top
    cps     #MODE_IRQ
    ldr     sp, =__irq_stack_top
    cps     #MODE_SVC
    ldr     sp, =__svc_stack_top

#if __ARM_ARCH >= 7
    /* The exception vectors stay where they are (VBAR): 0x0 holds the
     * firmware's stub, where the other cores wait. */
    ldr     r0, =exception_vectors
    mcr     p15, 0, r0, c12, c0, 0  @ VBAR
    mrc     p15, 0, r0, c1, c0, 0   @ SCTLR
    bic     r0, r0, #(1 << 13)      @ V = 0: vectors at VBAR
    mcr     p15, 0, r0, c1, c0, 0
#else
    /* Install the exception vectors at 0x00000000 (low vectors). */
    ldr     r0, =exception_vectors
    mov     r1, #0
    ldmia   r0!, {r2, r3, r5, r6, r7, r8, r9, r10}
    stmia   r1!, {r2, r3, r5, r6, r7, r8, r9, r10}
    ldmia   r0!, {r2, r3, r5, r6, r7, r8, r9, r10}
    stmia   r1!, {r2, r3, r5, r6, r7, r8, r9, r10}
    mrc     p15, 0, r0, c1, c0, 0   @ SCTLR
    bic     r0, r0, #(1 << 13)      @ V = 0: vectors at 0x0
    mcr     p15, 0, r0, c1, c0, 0
#endif

    /* Enable VFP: full access to coprocessors 10 and 11 (CPACR). */
    mrc     p15, 0, r0, c1, c0, 2
    orr     r0, r0, #(0xF << 20)
    mcr     p15, 0, r0, c1, c0, 2
#if __ARM_ARCH >= 7
    isb
#else
    mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 4   @ ARMv6 prefetch flush (ISB)
#endif
    mov     r0, #0x40000000         @ FPEXC.EN
    vmsr    fpexc, r0
    /* RunFast mode (FZ + DN, no traps): without it the VFP11 "bounces"
     * denormal and NaN operands to a support code we do not have, as an
     * undefined-instruction exception. A speed that decays towards zero
     * frame after frame in a Lua game becomes denormal in a minute or two.
     * (The Cortex-A53 needs no support code; the same mode, the same
     * results.) */
    mov     r0, #0x03000000         @ FPSCR.DN | FPSCR.FZ
    vmsr    fpscr, r0

    /* Zero .bss */
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
1:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b

    ldr     r0, =boot_mode
    and     r11, r11, #0x1F
    str     r11, [r0]

    mov     r0, r4
    bl      kernel_main

halt:
    wfi
    b       halt

#if __ARM_ARCH >= 7
park:
    wfe
    b       park
#endif
