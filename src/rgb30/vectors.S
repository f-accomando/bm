/*
 * AArch64 exception vectors (EL1, SP_EL1). IRQs go to irq_dispatch() with
 * every register the interrupted code may be using saved: the caller-saved
 * general registers and all 32 SIMD registers (C code uses them freely, for
 * memcpy too). Anything else is fatal: a64_exception(type, frame) dumps the
 * registers and never returns.
 */
#include "a64.h"

.macro VENTRY label
    .balign 0x80
    b       \label
.endm

.macro FATAL type
    sub     sp, sp, #FRAME_SIZE
    stp     x0, x1, [sp, #0]
    mov     x0, #\type
    b       exc_common
.endm

    .text
    .balign 2048
    .globl a64_vectors
a64_vectors:
    /* current EL, SP_EL0 */
    VENTRY  v_sync0
    VENTRY  v_irq0
    VENTRY  v_fiq0
    VENTRY  v_serr0
    /* current EL, SP_ELx: where the kernel runs */
    VENTRY  v_sync
    VENTRY  irq_entry
    VENTRY  v_fiq
    VENTRY  v_serr
    /* lower EL, AArch64 */
    VENTRY  v_sync_low
    VENTRY  v_irq_low
    VENTRY  v_fiq_low
    VENTRY  v_serr_low
    /* lower EL, AArch32 */
    VENTRY  v_sync_low
    VENTRY  v_irq_low
    VENTRY  v_fiq_low
    VENTRY  v_serr_low

v_sync0:    FATAL EXC64_SYNC
v_irq0:     FATAL EXC64_IRQ
v_fiq0:     FATAL EXC64_FIQ
v_serr0:    FATAL EXC64_SERROR
v_sync:     FATAL EXC64_SYNC
v_fiq:      FATAL EXC64_FIQ
v_serr:     FATAL EXC64_SERROR
v_sync_low: FATAL EXC64_SYNC
v_irq_low:  FATAL EXC64_IRQ
v_fiq_low:  FATAL EXC64_FIQ
v_serr_low: FATAL EXC64_SERROR

/* x0 = type, x0/x1 of the faulting code already at [sp] */
exc_common:
    str     x0, [sp, #FRAME_TYPE]
    ldr     x0, [sp, #0]            /* the original x0 back */
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    add     x1, sp, #FRAME_SIZE     /* sp before the exception */
    stp     x30, x1, [sp, #240]
    mrs     x1, elr_el1
    mrs     x2, spsr_el1
    stp     x1, x2, [sp, #256]
    mrs     x1, esr_el1
    mrs     x2, far_el1
    stp     x1, x2, [sp, #272]
    ldr     x0, [sp, #FRAME_TYPE]
    mov     x1, sp
    bl      a64_exception
1:  wfe
    b       1b

/* IRQ frame: x0-x18 and x29 at 0..152, x30 160, ELR 168, SPSR 176;
 * q0-q31 from IRQ_GP, then FPSR and FPCR */
#define IRQ_GP      192
#define IRQ_FRAME   (IRQ_GP + 32 * 16 + 16)

irq_entry:
    sub     sp, sp, #IRQ_FRAME
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x29, [sp, #144]
    mrs     x0, elr_el1
    mrs     x1, spsr_el1
    stp     x30, x0, [sp, #160]
    str     x1, [sp, #176]
    add     x0, sp, #IRQ_GP
    stp     q0, q1, [x0, #0]
    stp     q2, q3, [x0, #32]
    stp     q4, q5, [x0, #64]
    stp     q6, q7, [x0, #96]
    stp     q8, q9, [x0, #128]
    stp     q10, q11, [x0, #160]
    stp     q12, q13, [x0, #192]
    stp     q14, q15, [x0, #224]
    stp     q16, q17, [x0, #256]
    stp     q18, q19, [x0, #288]
    stp     q20, q21, [x0, #320]
    stp     q22, q23, [x0, #352]
    stp     q24, q25, [x0, #384]
    stp     q26, q27, [x0, #416]
    stp     q28, q29, [x0, #448]
    stp     q30, q31, [x0, #480]
    mrs     x1, fpsr
    mrs     x2, fpcr
    str     x1, [x0, #512]
    str     x2, [x0, #520]

    bl      irq_dispatch

    add     x0, sp, #IRQ_GP
    ldr     x1, [x0, #512]
    ldr     x2, [x0, #520]
    msr     fpsr, x1
    msr     fpcr, x2
    ldp     q0, q1, [x0, #0]
    ldp     q2, q3, [x0, #32]
    ldp     q4, q5, [x0, #64]
    ldp     q6, q7, [x0, #96]
    ldp     q8, q9, [x0, #128]
    ldp     q10, q11, [x0, #160]
    ldp     q12, q13, [x0, #192]
    ldp     q14, q15, [x0, #224]
    ldp     q16, q17, [x0, #256]
    ldp     q18, q19, [x0, #288]
    ldp     q20, q21, [x0, #320]
    ldp     q22, q23, [x0, #352]
    ldp     q24, q25, [x0, #384]
    ldp     q26, q27, [x0, #416]
    ldp     q28, q29, [x0, #448]
    ldp     q30, q31, [x0, #480]
    ldp     x30, x0, [sp, #160]
    ldr     x1, [sp, #176]
    msr     elr_el1, x0
    msr     spsr_el1, x1
    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x29, [sp, #144]
    add     sp, sp, #IRQ_FRAME
    eret
