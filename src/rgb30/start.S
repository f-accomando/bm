/*
 * Entry point of bm on the RGB30 (RK3566, Cortex-A55) and on QEMU's virt
 * machine. U-Boot's booti starts us like a Linux kernel: EL2, MMU and
 * caches off, x0 = its device tree, anywhere in RAM (the arm64 Image
 * header below says so). QEMU starts the ELF at EL1.
 *
 *   1. moves the image to its link address if it was loaded elsewhere;
 *   2. drops from EL2 to EL1 (timer, FP/SIMD and GIC registers allowed);
 *   3. FP/SIMD on, vectors, stack, bss cleared, TLS (picolibc's errno);
 *   4. kernel_main(device tree).
 */

#define SCTLR_EL1_RES1  0x30d00800      /* RES1 bits, MMU/caches off, LE */

    .section .text.boot, "ax"
    .globl _start
_start:
    b       entry                   /* code0 */
    .long   0                       /* code1 */
    .quad   0                       /* text_offset */
    .quad   __image_size            /* image + bss: memory U-Boot must leave free */
    .quad   0xa                     /* flags: little endian, 4K pages, anywhere */
    .quad   0, 0, 0
    .ascii  "ARM\x64"               /* magic */
    .long   0

entry:
    msr     daifset, #0xf
    mov     x19, x0                 /* device tree from U-Boot */

    /* 1. at the link address? otherwise copy text+data there and jump */
    adr     x1, _start
    ldr     x2, =_start
    cmp     x1, x2
    b.eq    relocated
    ldr     x3, =__load_end
    sub     x3, x3, x2              /* bytes to copy (multiple of 8) */
    add     x3, x3, #7
    and     x3, x3, #~7
    cmp     x2, x1
    b.hi    copy_down
copy_up:                            /* destination below the source: forward */
    mov     x4, #0
1:  ldr     x5, [x1, x4]
    str     x5, [x2, x4]
    add     x4, x4, #8
    cmp     x4, x3
    b.lo    1b
    b       copied
copy_down:                          /* destination above: backward (overlap) */
    mov     x4, x3
1:  sub     x4, x4, #8
    ldr     x5, [x1, x4]
    str     x5, [x2, x4]
    cbnz    x4, 1b
copied:
    dsb     sy
    ic      iallu
    dsb     sy
    isb
    ldr     x4, =relocated
    br      x4

relocated:
    /* 2. EL2 -> EL1 */
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #2
    b.ne    at_el1
    mov     x0, #(1 << 31)          /* HCR_EL2.RW: EL1 runs AArch64 */
    msr     hcr_el2, x0
    isb                             /* CNTHCTL_EL2's layout depends on HCR_EL2.E2H */
    mov     x0, #3                  /* CNTHCTL_EL2: EL1 may use the physical counter/timer */
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr
    mov     x0, #0x33ff             /* CPTR_EL2: no FP/SIMD traps */
    msr     cptr_el2, x0
    msr     hstr_el2, xzr
    mrs     x0, S3_4_C12_C9_5       /* ICC_SRE_EL2: GICv3 system registers for EL1 */
    orr     x0, x0, #0xf
    msr     S3_4_C12_C9_5, x0
    isb
    ldr     x0, =SCTLR_EL1_RES1
    msr     sctlr_el1, x0
    mov     x0, #0x3c5              /* EL1h, D A I F masked */
    msr     spsr_el2, x0
    adr     x0, at_el1
    msr     elr_el2, x0
    eret

at_el1:
    /* 3. */
    ldr     x0, =SCTLR_EL1_RES1
    msr     sctlr_el1, x0
    mov     x0, #(3 << 20)          /* CPACR_EL1.FPEN: FP/SIMD at EL0 and EL1 */
    msr     cpacr_el1, x0
    isb
    ldr     x0, =__stack_top
    mov     sp, x0
    ldr     x0, =a64_vectors
    msr     vbar_el1, x0
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
1:  cmp     x0, x1
    b.hs    2f
    stp     xzr, xzr, [x0], #16
    b       1b
2:  ldr     x0, =__tls_base         /* TPIDR_EL0 = TLS block - TCB (16 bytes) */
    ldr     x1, =__arm64_tls_tcb_offset
    sub     x0, x0, x1
    msr     tpidr_el0, x0
    isb

    /* 4. */
    mov     x0, x19
    bl      kernel_main
3:  wfe
    b       3b

    .ltorg
