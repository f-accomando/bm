/*
 * fiber_switch(uint32_t **save, uint32_t *load): the context switch of the
 * fibers (src/kernel/fiber.c). Pushes what a C call must keep (r4-r11,
 * the VFP d8-d15, FPSCR) on the current stack, stores sp in *save, takes
 * load as the stack and pops the same frame from it: the call returns in
 * the other context. Frame, from sp up (112 bytes, 8-byte aligned):
 *   fpscr, pad, d8-d15, r4-r12, lr
 * A new fiber's frame (fiber_prepare in fiber.c) has lr = fiber_start,
 * r4 = its argument and r5 = the C function that runs it.
 */
    .section .text
    .arm
    .globl fiber_switch
fiber_switch:
    push    {r4-r12, lr}
    vpush   {d8-d15}
    vmrs    r2, fpscr
    push    {r2, r3}
    str     sp, [r0]
    mov     sp, r1
    pop     {r2, r3}
    vmsr    fpscr, r2
    vpop    {d8-d15}
    pop     {r4-r12, pc}

    .globl fiber_start
fiber_start:
    mov     r0, r4
    blx     r5                      @ fiber_main: never returns
1:  b       1b
