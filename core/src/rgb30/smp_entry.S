/*
 * Entry of the secondary cores (PSCI CPU_ON, x0 = core number). Like the first core:
 * EL2 to EL1 if needed, FP/SIMD on, vectors, own stack, then smp_secondary_main (smp.c).
 */
#define SCTLR_EL1_RES1  0x30d00800

    .section .text, "ax"
    .globl smp_entry
smp_entry:
    msr     daifset, #0xf
    mov     x19, x0                 /* core number */
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #2
    b.ne    sec_el1
    mov     x0, #(1 << 31)
    msr     hcr_el2, x0
    isb
    mov     x0, #3
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr
    mov     x0, #0x33ff
    msr     cptr_el2, x0
    msr     hstr_el2, xzr
    mrs     x0, pmcr_el0
    ubfx    x0, x0, #11, #5
    msr     mdcr_el2, x0
    mrs     x0, S3_4_C12_C9_5
    orr     x0, x0, #0xf
    msr     S3_4_C12_C9_5, x0
    isb
    ldr     x0, =SCTLR_EL1_RES1
    msr     sctlr_el1, x0
    mov     x0, #0x3c5
    msr     spsr_el2, x0
    adr     x0, sec_el1
    msr     elr_el2, x0
    eret
sec_el1:
    ldr     x0, =SCTLR_EL1_RES1
    msr     sctlr_el1, x0
    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0
    ldr     x0, =a64_vectors
    msr     vbar_el1, x0
    isb
    ldr     x1, =smp_stacks
    mov     x2, #16384              /* SMP_STACK */
    madd    x1, x19, x2, x1
    add     x1, x1, x2
    mov     sp, x1
    mov     x0, x19
    bl      smp_secondary_main
1:  wfe
    b       1b
    .ltorg
