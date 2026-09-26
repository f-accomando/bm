/*
 * Exception vector table (copied to 0x0 by start.S) and entry stubs.
 *
 * Each stub builds an exc_frame_t on the current mode's stack:
 *   [sp, lr of interrupted mode] [spsr] [r0..r12] [pc of faulting insn]
 * and calls exception_handler(type, frame), which never returns.
 */

#include "exceptions.h"

    .section .text
    .arm
    .balign 32
    .globl exception_vectors
exception_vectors:
    ldr     pc, v_reset
    ldr     pc, v_undef
    ldr     pc, v_swi
    ldr     pc, v_pabt
    ldr     pc, v_dabt
    ldr     pc, v_unused
    ldr     pc, v_irq
    ldr     pc, v_fiq
v_reset:    .word _start
v_undef:    .word exc_undef
v_swi:      .word exc_swi
v_pabt:     .word exc_pabt
v_dabt:     .word exc_dabt
v_unused:   .word exc_unused
v_irq:      .word irq_entry
v_fiq:      .word exc_fiq

/* lr_adj turns the banked LR into the address of the faulting instruction. */
.macro EXC_STUB name, type, lr_adj
\name:
    sub     lr, lr, #\lr_adj
    stmfd   sp!, {r0-r12, lr}
    mrs     r0, spsr
    stmfd   sp!, {r0}
    mov     r0, #\type
    b       exc_common
.endm

    EXC_STUB exc_undef,  EXC_UNDEF,  4
    EXC_STUB exc_swi,    EXC_SWI,    4
    EXC_STUB exc_pabt,   EXC_PABT,   4
    EXC_STUB exc_dabt,   EXC_DABT,   8
    EXC_STUB exc_unused, EXC_UNUSED, 4
    EXC_STUB exc_fiq,    EXC_FIQ,    4

/* r0 = type. Fetch banked SP/LR of the interrupted mode (User -> System). */
exc_common:
    ldr     r1, [sp]                @ spsr
    and     r2, r1, #0x1F
    cmp     r2, #0x10
    moveq   r2, #0x1F
    mrs     r3, cpsr
    bic     r4, r3, #0x1F
    orr     r4, r4, r2
    orr     r4, r4, #0xC0           @ keep IRQ/FIQ masked
    msr     cpsr_c, r4
    mov     r5, sp
    mov     r6, lr
    msr     cpsr_c, r3
    stmfd   sp!, {r5, r6}
    mov     r1, sp
    bl      exception_handler
1:  b       1b

/*
 * IRQ entry: runs the C handler in SVC mode (IRQs still masked) and returns
 * to the interrupted code. Saves the AAPCS caller-saved registers, including
 * VFP d0-d7 and FPSCR, since C code built for hard-float may use them.
 */
    .globl irq_entry
irq_entry:
    sub     lr, lr, #4
    srsdb   sp!, #0x13              @ push lr_irq, spsr_irq on the SVC stack
    cps     #0x13
    push    {r0-r3, r12, lr}        @ 8 words total with the SRS frame
    vpush   {d0-d7}
    vmrs    r0, fpscr
    and     r1, sp, #4              @ realign the stack to 8 bytes (AAPCS)
    sub     sp, sp, r1
    push    {r0, r1}
    bl      irq_handler
    pop     {r0, r1}
    add     sp, sp, r1
    vmsr    fpscr, r0
    vpop    {d0-d7}
    pop     {r0-r3, r12, lr}
    rfeia   sp!                     @ restore pc and cpsr from the SRS frame
