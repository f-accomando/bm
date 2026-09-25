/*
 * Serial chainloader entry. Runs at 0x8000 (where the firmware put it),
 * copies itself to its link address (0x02000000) with position independent
 * code, then continues there. The stack grows down from 0x02000000.
 */
    .section .text.boot
    .arm
    .globl _start
_start:
    cpsid   if
    mov     r4, r0                  @ firmware boot args, forwarded to the kernel
    mov     r5, r1
    mov     r6, r2

    adr     r0, _start              @ where we are running
    ldr     r1, =_start             @ where we are linked
    ldr     r2, =__image_end
    cmp     r0, r1
    beq     2f
1:  ldr     r3, [r0], #4
    str     r3, [r1], #4
    cmp     r1, r2
    blo     1b
2:  mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 0   @ invalidate I-cache
    mcr     p15, 0, r0, c7, c5, 4   @ prefetch flush
    ldr     pc, =relocated

relocated:
    ldr     sp, =_start

    mrc     p15, 0, r0, c1, c0, 2   @ enable VFP (hard-float ABI)
    orr     r0, r0, #(0xF << 20)
    mcr     p15, 0, r0, c1, c0, 2
    mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 4
    mov     r0, #0x40000000
    vmsr    fpexc, r0

    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
3:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     3b

    mov     r0, r4
    mov     r1, r5
    mov     r2, r6
    bl      loader_main
4:  wfi
    b       4b

/* void boot_kernel(r0, r1, r2): jump to 0x8000 with the firmware arguments. */
    .globl boot_kernel
boot_kernel:
    mov     r3, #0
    mcr     p15, 0, r3, c7, c5, 0   @ invalidate I-cache: new code at 0x8000
    mcr     p15, 0, r3, c7, c10, 4  @ DSB
    mcr     p15, 0, r3, c7, c5, 4   @ prefetch flush
    mov     r3, #0x8000
    bx      r3
