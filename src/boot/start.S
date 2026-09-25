/*
 * Entry point for Raspberry Pi Zero / Zero W (BCM2835, ARM1176JZF-S).
 *
 * The GPU firmware (start.elf) loads kernel.img at 0x8000 and jumps here
 * in SVC mode with MMU and D-cache off:
 *   r0 = 0, r1 = machine type, r2 = ATAGS/DTB address
 */

    .section .text.boot
    .arm
    .globl _start
_start:
    cpsid   if                      @ mask IRQ and FIQ

    ldr     sp, =__stack_top

    mov     r4, r2                  @ keep ATAGS/DTB pointer for kernel_main

    /* Enable VFP: full access to coprocessors 10 and 11 (CPACR). */
    mrc     p15, 0, r0, c1, c0, 2
    orr     r0, r0, #(0xF << 20)
    mcr     p15, 0, r0, c1, c0, 2
    mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 4   @ ARMv6 prefetch flush (ISB)
    mov     r0, #0x40000000         @ FPEXC.EN
    vmsr    fpexc, r0

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
