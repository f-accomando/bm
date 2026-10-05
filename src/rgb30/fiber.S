/*
 * fiber_switch(uint32_t **save, uint32_t *load) on AArch64: the context
 * switch of the fibers (src/kernel/fiber.c; the Pi's is src/arch/fiber.S).
 * Pushes what a C call must keep (x19-x30, d8-d15, FPCR) on the current
 * stack, stores sp in *save, takes load as the stack and pops the same
 * frame from it: the call returns in the other context. Frame, from sp up
 * (176 bytes, 16-byte aligned):
 *   x19 x20 ... x28, x29, x30, d8 ... d15, fpcr, pad
 * A new fiber's frame (fiber_prepare) has x30 = fiber_start, x19 = its
 * argument and x20 = the C function that runs it.
 */
    .section .text
    .globl fiber_switch
fiber_switch:
    sub     sp, sp, #176
    stp     x19, x20, [sp, #0]
    stp     x21, x22, [sp, #16]
    stp     x23, x24, [sp, #32]
    stp     x25, x26, [sp, #48]
    stp     x27, x28, [sp, #64]
    stp     x29, x30, [sp, #80]
    stp     d8, d9, [sp, #96]
    stp     d10, d11, [sp, #112]
    stp     d12, d13, [sp, #128]
    stp     d14, d15, [sp, #144]
    mrs     x2, fpcr
    str     x2, [sp, #160]
    mov     x3, sp
    str     x3, [x0]
    mov     sp, x1
    ldr     x2, [sp, #160]
    msr     fpcr, x2
    ldp     d14, d15, [sp, #144]
    ldp     d12, d13, [sp, #128]
    ldp     d10, d11, [sp, #112]
    ldp     d8, d9, [sp, #96]
    ldp     x29, x30, [sp, #80]
    ldp     x27, x28, [sp, #64]
    ldp     x25, x26, [sp, #48]
    ldp     x23, x24, [sp, #32]
    ldp     x21, x22, [sp, #16]
    ldp     x19, x20, [sp, #0]
    add     sp, sp, #176
    ret

    .globl fiber_start
fiber_start:
    mov     x0, x19
    blr     x20                     // fiber_main: never returns
1:  b       1b
