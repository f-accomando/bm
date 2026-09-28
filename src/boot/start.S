/*
 * Entry point for Raspberry Pi Zero / Zero W (BCM2835, ARM1176JZF-S).
 *
 * The GPU firmware (start.elf), or the serial chainloader, jumps to 0x8000
 * in SVC mode with MMU and D-cache off:
 *   r0 = 0, r1 = machine type, r2 = ATAGS/DTB address
 */

#define MODE_FIQ 0x11
#define MODE_IRQ 0x12
#define MODE_SVC 0x13
#define MODE_ABT 0x17
#define MODE_UND 0x1B

    .section .text.boot
    .arm
    .globl _start
_start:
    cpsid   if                      @ mask IRQ and FIQ
    mov     r4, r2                  @ keep ATAGS/DTB pointer for kernel_main

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

    /* Enable VFP: full access to coprocessors 10 and 11 (CPACR). */
    mrc     p15, 0, r0, c1, c0, 2
    orr     r0, r0, #(0xF << 20)
    mcr     p15, 0, r0, c1, c0, 2
    mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 4   @ ARMv6 prefetch flush (ISB)
    mov     r0, #0x40000000         @ FPEXC.EN
    vmsr    fpexc, r0
    /* RunFast mode (FZ + DN, no traps): without it the VFP11 "bounces"
     * denormal and NaN operands to a support code we do not have, as an
     * undefined-instruction exception. A speed that decays towards zero
     * frame after frame in a Lua game becomes denormal in a minute or two. */
    mov     r0, #0x03000000         @ FPSCR.DN | FPSCR.FZ
    vmsr    fpscr, r0

    /* Zero .bss */
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
1:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b

    mov     r0, r4
    bl      kernel_main

halt:
    wfi
    b       halt
