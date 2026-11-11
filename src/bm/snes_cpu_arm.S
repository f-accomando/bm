/*
 * snes_cpu_arm.S: the hot path of the 65C816 interpreter for the ARMv6 console (Pi Zero W, Pi One). Original code.
 * snes_cpu.c has the C reference (the loop cpu_run and the opcode switch exec_op); this file runs the common opcodes and hands the others to
 * exec_op (cpu_cold) with the registers written back to the struct. The golden test (tests/snes/cpu_golden.c) must give the same hashes.
 * The table "optab" at the end has 2 words an opcode (the mode routine, the operation routine); an opcode with m_cold is done by C (exec_op).
 *
 * Registers: r4 PC in the high half (an add of 0x10000 wraps in the bank), r5 PB shifted left by 16, r6 A, r7 X, r8 Y,
 *   r9 the clock minus the limit (the loop runs while it is negative), r10 N and Z lazily (bit 31 is N, Z is "the low 16 bits are 0"; an 8-bit
 *   result v is stored as v | v<<24, a 16-bit one as v | v<<16), r11 P in its real bit places (C 1, I 4, D 8, X 0x10, M 0x20, V 0x40; N and Z are
 *   not kept here), r12 the table of page pointers (snes_rtab). r0-r3 and lr are scratch. D, DB, S, E and the loop state are in the stack frame.
 * A mode routine computes the address of the operand in r0 and returns with "bx lr" to the routine of the operation (the table has 2 words an opcode).
 */
#if defined(__arm__) && !defined(__aarch64__) && !defined(SNES_CPU_C)
    .syntax unified
    .arm
    .equ LIM, 0
    .equ ST, 4
    .equ CNT, 8
    .equ PEND, 12
    .equ SLR, 16
    .equ KEEP, 20
    .equ TA, 24
    .equ TB, 28
    .equ TC, 32
    .equ OP, 36
    .equ CU, 40
    .equ DD, 44
    .equ DBK, 48
    .equ SS, 52
    .equ EE, 56
    .equ PL1, 60
    .equ PL2, 64
    .equ PL3, 68
    .equ PL4, 72
    .equ FRAME, 76

    .section .text.snes_cpu_hot, "ax", %progbits
    .global cpu_run_asm
    .type cpu_run_asm, %function
/* int cpu_run_asm(cpu_t *cu, int limit, int pend) */
cpu_run_asm:
    push {r4-r11, lr}
    sub sp, sp, #FRAME
    str r0, [sp, #CU]
    str r1, [sp, #LIM]
    str r2, [sp, #PEND]
    mov r2, #0
    str r2, [sp, #CNT]
    bl reload
next:
    cmp r9, #0
    bge done
    ldr r0, [sp, #CNT]
    add r0, r0, #1
    str r0, [sp, #CNT]
    str r9, [sp, #ST]
    ldr r0, [sp, #PEND]
    cmp r0, #0
    bne attn
go:
    add r9, r9, #6
    bl fetch8s
    str r0, [sp, #OP]
    ldr r1, =optab
    add r1, r1, r0, lsl #3
    ldr lr, [r1, #4]
    ldr r2, [r1]
    bx r2
done:
    bl flush
    ldr r0, [sp, #CNT]
    add sp, sp, #FRAME
    pop {r4-r11, pc}

/* an interrupt, WAI or STP is pending: C does the top of the loop */
attn:
    bl flush
    ldr r0, [sp, #CU]
    ldr r2, [sp, #LIM]
    ldr r1, [sp, #ST]
    add r1, r1, r2
    bl cpu_attn
    str r0, [sp, #TA]
    bl reload
    ldr r1, =cpu_pg
    ldr r1, [r1]
    str r1, [sp, #PEND]
    ldr r0, [sp, #TA]
    cmp r0, #0
    bne next
    b go

/* an opcode that C does: the state goes to the struct, exec_op runs, the state comes back */
m_cold:
    bl flush
    ldr r0, [sp, #CU]
    ldr r1, [sp, #OP]
    ldr r3, [sp, #LIM]
    ldr r2, [sp, #ST]
    add r2, r2, r3
    bl cpu_cold
    str r0, [sp, #PEND]
    bl reload
    b next

/* the registers from the struct */
reload:
    ldr r1, [sp, #CU]
    ldrh r4, [r1, #10]
    mov r4, r4, lsl #16
    ldr r5, [r1, #16]
    ldrh r6, [r1, #0]
    ldrh r7, [r1, #2]
    ldrh r8, [r1, #4]
    ldr r0, [sp, #LIM]
    ldr r9, [r1, #40]
    sub r9, r9, r0
    ldr r0, [r1, #20]
    mov r10, r0, lsl #31
    ldr r0, [r1, #28]
    cmp r0, #0
    orreq r10, r10, #1
    ldr r0, [r1, #32]
    ldr r2, [r1, #24]
    orr r11, r0, r2, lsl #6
    ldr r2, [r1, #36]
    and r3, r2, #1
    orr r11, r11, r3, lsl #5
    and r3, r2, #2
    orr r11, r11, r3, lsl #3
    and r3, r2, #4
    orr r11, r11, r3, lsl #1
    and r3, r2, #8
    orr r11, r11, r3, lsr #1
    and r3, r2, #16
    mov r3, r3, lsr #4
    str r3, [sp, #EE]
    ldrh r3, [r1, #8]
    str r3, [sp, #DD]
    ldr r3, [r1, #12]
    str r3, [sp, #DBK]
    ldrh r3, [r1, #6]
    str r3, [sp, #SS]
    ldr r12, =snes_rtab
    bx lr

/* the registers to the struct */
flush:
    ldr r1, [sp, #CU]
    mov r0, r4, lsr #16
    strh r0, [r1, #10]
    str r5, [r1, #16]
    strh r6, [r1, #0]
    strh r7, [r1, #2]
    strh r8, [r1, #4]
    ldr r0, [sp, #SS]
    strh r0, [r1, #6]
    ldr r0, [sp, #LIM]
    add r0, r9, r0
    str r0, [r1, #40]
    mov r0, r10, lsr #31
    str r0, [r1, #20]
    movs r0, r10, lsl #16
    moveq r0, #1
    movne r0, #0
    str r0, [r1, #28]
    and r0, r11, #1
    str r0, [r1, #32]
    mov r0, r11, lsr #6
    and r0, r0, #1
    str r0, [r1, #24]
    ldr r2, [r1, #36]
    bic r2, r2, #15
    and r0, r11, #0x20
    orr r2, r2, r0, lsr #5
    and r0, r11, #0x10
    orr r2, r2, r0, lsr #3
    and r0, r11, #8
    orr r2, r2, r0, lsr #1
    and r0, r11, #4
    orr r2, r2, r0, lsl #1
    str r2, [r1, #36]
    bx lr

/* ------------------------------------------------------------------ the bus */
/* r0 = address (24 bit): returns the byte in r0 (r1-r3 scratch) */
rd8s:
    bic r0, r0, #0xFF000000
    mov r1, r0, lsr #12
    ldr r2, [r12, r1, lsl #2]
    cmp r2, #0
    beq slow_rd
    ldr r3, =snes_ctab
    ldrb r3, [r3, r1]
    add r9, r9, r3
    mov r0, r0, lsl #20
    ldrb r0, [r2, r0, lsr #20]
    bx lr
/* r0 = address, r1 = byte (r0-r3 scratch) */
wr8s:
    bic r0, r0, #0xFF000000
    mov r2, r0, lsr #12
    ldr r3, =snes_wtab
    ldr r3, [r3, r2, lsl #2]
    cmp r3, #0
    beq slow_wr
    mov r0, r0, lsl #20
    strb r1, [r3, r0, lsr #20]
    ldr r3, =snes_ctab
    ldrb r3, [r3, r2]
    add r9, r9, r3
    bx lr
/* the page is not plain memory: C does the access with the clock of the moment; the registers r4-r11 are kept by the C ABI */
slow_rd:
    str lr, [sp, #SLR]
    ldr r2, [sp, #LIM]
    add r1, r9, r2
    ldr r3, [sp, #ST]
    add r2, r3, r2
    bl cpu_sr
    b slow_end
slow_wr:
    str lr, [sp, #SLR]
    ldr r2, [sp, #LIM]
    ldr r3, [sp, #ST]
    add r3, r3, r2
    add r2, r9, r2
    bl cpu_sw
slow_end:
    ldr r1, =cpu_rc
    ldr r1, [r1]
    ldr r2, [sp, #LIM]
    sub r9, r1, r2
    ldr r1, =cpu_pg
    ldr r1, [r1]
    str r1, [sp, #PEND]
    ldr r12, =snes_rtab
    ldr lr, [sp, #SLR]
    bx lr
.ltorg

/* the next code byte in r0 */
fetch8s:
    orr r0, r5, r4, lsr #16
    mov r1, r0, lsr #12
    ldr r2, [r12, r1, lsl #2]
    add r4, r4, #0x10000
    cmp r2, #0
    beq slow_rd
    ldr r3, =snes_ctab
    ldrb r3, [r3, r1]
    add r9, r9, r3
    mov r0, r0, lsl #20
    ldrb r0, [r2, r0, lsr #20]
    bx lr
fetch16s:
    str lr, [sp, #TC]
    bl fetch8s
    str r0, [sp, #TB]
    bl fetch8s
    ldr r1, [sp, #TB]
    orr r0, r1, r0, lsl #8
    ldr pc, [sp, #TC]
/* 16 bit reads and writes: the second byte is at address + 1 (24 bit, as in the C code) */
rd16s:
    str lr, [sp, #TC]
    str r0, [sp, #TB]
    bl rd8s
    str r0, [sp, #TA]
    ldr r0, [sp, #TB]
    add r0, r0, #1
    bl rd8s
    ldr r1, [sp, #TA]
    orr r0, r1, r0, lsl #8
    ldr pc, [sp, #TC]
/* the same in bank 0, the second byte wraps in the bank (direct page pointers) */
rd16b0s:
    str lr, [sp, #TC]
    str r0, [sp, #TB]
    bl rd8s
    str r0, [sp, #TA]
    ldr r0, [sp, #TB]
    add r0, r0, #1
    uxth r0, r0
    bl rd8s
    ldr r1, [sp, #TA]
    orr r0, r1, r0, lsl #8
    ldr pc, [sp, #TC]
wr16s:
    str lr, [sp, #TC]
    str r0, [sp, #TB]
    str r1, [sp, #TA]
    and r1, r1, #255
    bl wr8s
    ldr r0, [sp, #TB]
    add r0, r0, #1
    ldr r1, [sp, #TA]
    mov r1, r1, lsr #8
    bl wr8s
    ldr pc, [sp, #TC]
/* by the M flag (rdM, wrM) or the X flag (rdX, wrX); the value of a write is the full register in r1 */
rdM:
    tst r11, #0x20
    bne rd8s
    b rd16s
rdX:
    tst r11, #0x10
    bne rd8s
    b rd16s
wrM:
    tst r11, #0x20
    andne r1, r1, #255
    bne wr8s
    b wr16s
wrX:
    tst r11, #0x10
    andne r1, r1, #255
    bne wr8s
    b wr16s
/* the stack: S and E are in the frame */
push8s:
    str lr, [sp, #PL1]
    ldr r0, [sp, #SS]
    bl wr8s
    ldr r0, [sp, #SS]
    sub r0, r0, #1
    uxth r0, r0
    ldr r1, [sp, #EE]
    cmp r1, #0
    andne r0, r0, #255
    orrne r0, r0, #0x100
    str r0, [sp, #SS]
    ldr pc, [sp, #PL1]
pull8s:
    str lr, [sp, #PL1]
    ldr r0, [sp, #SS]
    add r0, r0, #1
    uxth r0, r0
    ldr r1, [sp, #EE]
    cmp r1, #0
    andne r0, r0, #255
    orrne r0, r0, #0x100
    str r0, [sp, #SS]
    bl rd8s
    ldr pc, [sp, #PL1]
push16s:
    str lr, [sp, #PL3]
    str r1, [sp, #PL4]
    mov r1, r1, lsr #8
    bl push8s
    ldr r1, [sp, #PL4]
    and r1, r1, #255
    bl push8s
    ldr pc, [sp, #PL3]
pull16s:
    str lr, [sp, #PL3]
    bl pull8s
    str r0, [sp, #PL4]
    bl pull8s
    ldr r1, [sp, #PL4]
    orr r0, r1, r0, lsl #8
    ldr pc, [sp, #PL3]
.ltorg

/* ------------------------------------------------------------------ the addressing modes: the address in r0 */
m_none:
    bx lr
m_immM:
    tst r11, #0x20
    orr r0, r5, r4, lsr #16
    addne r4, r4, #0x10000
    bxne lr
    cmn r4, #0x10000
    beq m_cold
    add r4, r4, #0x20000
    bx lr
m_immX:
    tst r11, #0x10
    orr r0, r5, r4, lsr #16
    addne r4, r4, #0x10000
    bxne lr
    cmn r4, #0x10000
    beq m_cold
    add r4, r4, #0x20000
    bx lr
m_dp:
    str lr, [sp, #KEEP]
    bl fetch8s
    ldr r1, [sp, #DD]
    add r0, r0, r1
    uxth r0, r0
    ldr pc, [sp, #KEEP]
m_dpx:
    str lr, [sp, #KEEP]
    bl fetch8s
    ldr r1, [sp, #DD]
    add r0, r0, r1
    add r0, r0, r7
    uxth r0, r0
    ldr pc, [sp, #KEEP]
m_dpy:
    str lr, [sp, #KEEP]
    bl fetch8s
    ldr r1, [sp, #DD]
    add r0, r0, r1
    add r0, r0, r8
    uxth r0, r0
    ldr pc, [sp, #KEEP]
m_abs:
    str lr, [sp, #KEEP]
    bl fetch16s
    ldr r1, [sp, #DBK]
    orr r0, r0, r1
    ldr pc, [sp, #KEEP]
m_absx:
    str lr, [sp, #KEEP]
    bl fetch16s
    ldr r1, [sp, #DBK]
    orr r0, r0, r1
    add r0, r0, r7
    bic r0, r0, #0xFF000000
    ldr pc, [sp, #KEEP]
m_absy:
    str lr, [sp, #KEEP]
    bl fetch16s
    ldr r1, [sp, #DBK]
    orr r0, r0, r1
    add r0, r0, r8
    bic r0, r0, #0xFF000000
    ldr pc, [sp, #KEEP]
m_long:
    str lr, [sp, #KEEP]
    bl fetch16s
    str r0, [sp, #TA]
    bl fetch8s
    ldr r1, [sp, #TA]
    orr r0, r1, r0, lsl #16
    ldr pc, [sp, #KEEP]
m_longx:
    str lr, [sp, #KEEP]
    bl fetch16s
    str r0, [sp, #TA]
    bl fetch8s
    ldr r1, [sp, #TA]
    orr r0, r1, r0, lsl #16
    add r0, r0, r7
    bic r0, r0, #0xFF000000
    ldr pc, [sp, #KEEP]
m_indy:
    str lr, [sp, #KEEP]
    bl fetch8s
    ldr r1, [sp, #DD]
    add r0, r0, r1
    uxth r0, r0
    bl rd16b0s
    ldr r1, [sp, #DBK]
    orr r0, r0, r1
    add r0, r0, r8
    bic r0, r0, #0xFF000000
    ldr pc, [sp, #KEEP]
/* ADC and SBC in decimal mode are C's: the check comes before the operand is read */
.macro DC mode
dc_\mode:
    tst r11, #8
    bne m_cold
    b m_\mode
.endm
    DC immM
    DC dp
    DC dpx
    DC abs
    DC absx
    DC absy
    DC indy
    DC long
    DC longx
.ltorg

/* ------------------------------------------------------------------ the operations; the address of the operand is in r0 */
.macro NZ8 r
    orr r10, \r, \r, lsl #24
.endm
.macro NZ16 r
    orr r10, \r, \r, lsl #16
.endm
op_lda:
    bl rdM
    tst r11, #0x20
    beq 1f
    bic r6, r6, #255
    orr r6, r6, r0
    NZ8 r0
    b next
1:  mov r6, r0
    NZ16 r0
    b next
op_sta:
    mov r1, r6
    bl wrM
    b next
op_stz:
    mov r1, #0
    bl wrM
    b next
op_stx:
    mov r1, r7
    bl wrX
    b next
op_sty:
    mov r1, r8
    bl wrX
    b next
op_ldx:
    bl rdX
    mov r7, r0
    tst r11, #0x10
    beq 1f
    NZ8 r0
    b next
1:  NZ16 r0
    b next
op_ldy:
    bl rdX
    mov r8, r0
    tst r11, #0x10
    beq 1f
    NZ8 r0
    b next
1:  NZ16 r0
    b next
.macro LOGIC name, insn
op_\name:
    bl rdM
    tst r11, #0x20
    beq 1f
    \insn r1, r0, r6
    and r1, r1, #255
    bic r6, r6, #255
    orr r6, r6, r1
    NZ8 r1
    b next
1:  \insn r6, r6, r0
    NZ16 r6
    b next
.endm
    LOGIC ora, orr
    LOGIC and, and
    LOGIC eor, eor
/* compare: the register (r6 A, r7 X, r8 Y) against the operand, at the width of the flag */
.macro CMPOP name, reg, rd, bit
op_\name:
    bl \rd
    tst r11, #\bit
    beq 1f
    and r2, \reg, #255
    subs r1, r2, r0
    bic r11, r11, #1
    orrcs r11, r11, #1
    and r1, r1, #255
    NZ8 r1
    b next
1:  subs r1, \reg, r0
    bic r11, r11, #1
    orrcs r11, r11, #1
    uxth r1, r1
    NZ16 r1
    b next
.endm
    CMPOP cmp, r6, rdM, 0x20
    CMPOP cpx, r7, rdX, 0x10
    CMPOP cpy, r8, rdX, 0x10
/* ADC (v in r0 after the read) and SBC (the complement of v): the decimal mode never gets here */
op_sbc:
    bl rdM
    tst r11, #0x20
    eorne r0, r0, #255
    mvneq r0, r0
    uxtheq r0, r0
    b adc_go
op_adc:
    bl rdM
adc_go:
    tst r11, #0x20
    beq 16f
    and r1, r11, #1
    and r2, r6, #255
    add r3, r2, r0
    add r3, r3, r1
    mov lr, r3, lsr #8
    and r3, r3, #255
    eor r1, r2, r0
    eor r2, r2, r3
    bic r1, r2, r1
    tst r1, #0x80
    bic r11, r11, #0x41
    orrne r11, r11, #0x40
    orr r11, r11, lr
    bic r6, r6, #255
    orr r6, r6, r3
    NZ8 r3
    b next
16: and r1, r11, #1
    add r3, r6, r0
    add r3, r3, r1
    mov lr, r3, lsr #16
    uxth r3, r3
    eor r1, r6, r0
    eor r2, r6, r3
    bic r1, r2, r1
    tst r1, #0x8000
    bic r11, r11, #0x41
    orrne r11, r11, #0x40
    orr r11, r11, lr
    mov r6, r3
    NZ16 r3
    b next
.ltorg

/* ------------------------------------------------------------------ shifts, rotates, increments and decrements (the kernel takes r0, gives r0; C is in r11 bit 0) */
.macro SETC cond=
    bic r11, r11, #1
    orr\cond r11, r11, #1
.endm
.macro SH8_asl
    tst r0, #0x80
    SETC ne
    mov r0, r0, lsl #1
    and r0, r0, #255
.endm
.macro SH8_lsr
    and r1, r0, #1
    bic r11, r11, #1
    orr r11, r11, r1
    mov r0, r0, lsr #1
.endm
.macro SH8_rol
    and r1, r11, #1
    tst r0, #0x80
    SETC ne
    orr r0, r1, r0, lsl #1
    and r0, r0, #255
.endm
.macro SH8_ror
    and r1, r11, #1
    and r2, r0, #1
    bic r11, r11, #1
    orr r11, r11, r2
    mov r0, r0, lsr #1
    orr r0, r0, r1, lsl #7
.endm
.macro SH8_inc
    add r0, r0, #1
    and r0, r0, #255
.endm
.macro SH8_dec
    sub r0, r0, #1
    and r0, r0, #255
.endm
.macro SH16_asl
    tst r0, #0x8000
    SETC ne
    mov r0, r0, lsl #1
    uxth r0, r0
.endm
.macro SH16_lsr
    and r1, r0, #1
    bic r11, r11, #1
    orr r11, r11, r1
    mov r0, r0, lsr #1
.endm
.macro SH16_rol
    and r1, r11, #1
    tst r0, #0x8000
    SETC ne
    orr r0, r1, r0, lsl #1
    uxth r0, r0
.endm
.macro SH16_ror
    and r1, r11, #1
    and r2, r0, #1
    bic r11, r11, #1
    orr r11, r11, r2
    mov r0, r0, lsr #1
    orr r0, r0, r1, lsl #15
.endm
.macro SH16_inc
    add r0, r0, #1
    uxth r0, r0
.endm
.macro SH16_dec
    sub r0, r0, #1
    uxth r0, r0
.endm
.macro SHOP name
op_acc_\name:
    tst r11, #0x20
    beq 16f
    and r0, r6, #255
    SH8_\name
    bic r6, r6, #255
    orr r6, r6, r0
    NZ8 r0
    b next
16: mov r0, r6
    SH16_\name
    mov r6, r0
    NZ16 r0
    b next
op_mem_\name:
    str r0, [sp, #PL2]
    bl rdM
    tst r11, #0x20
    beq 16f
    SH8_\name
    NZ8 r0
    mov r1, r0
    ldr r0, [sp, #PL2]
    bl wr8s
    add r9, r9, #6
    b next
16: SH16_\name
    NZ16 r0
    mov r1, r0
    ldr r0, [sp, #PL2]
    bl wr16s
    add r9, r9, #6
    b next
.endm
    SHOP asl
    SHOP lsr
    SHOP rol
    SHOP ror
    SHOP inc
    SHOP dec
.ltorg

/* ------------------------------------------------------------------ implied operations */
/* INX, INY, DEX, DEY: the width follows the X flag */
.macro IDX name, reg, insn
op_\name:
    \insn \reg, \reg, #1
    tst r11, #0x10
    beq 16f
    and \reg, \reg, #255
    NZ8 \reg
    b next
16: uxth \reg, \reg
    NZ16 \reg
    b next
.endm
    IDX inx, r7, add
    IDX iny, r8, add
    IDX dex, r7, sub
    IDX dey, r8, sub
/* TAX, TAY: the register is set at the width of X */
.macro TOXY name, dst, src
op_\name:
    tst r11, #0x10
    beq 16f
    and \dst, \src, #255
    NZ8 \dst
    b next
16: mov \dst, \src
    NZ16 \dst
    b next
.endm
    TOXY tax, r7, r6
    TOXY tay, r8, r6
    TOXY txy, r8, r7
    TOXY tyx, r7, r8
/* TXA, TYA: A is set at the width of M */
.macro FROMXY name, src
op_\name:
    tst r11, #0x20
    beq 16f
    and r0, \src, #255
    bic r6, r6, #255
    orr r6, r6, r0
    NZ8 r0
    b next
16: mov r6, \src
    NZ16 r6
    b next
.endm
    FROMXY txa, r7
    FROMXY tya, r8
op_clc:
    bic r11, r11, #1
    b next
op_sec:
    orr r11, r11, #1
    b next
op_nop:
    b next

/* ------------------------------------------------------------------ branches: the offset byte is read first */
.macro TAKEN
    mov r0, r0, lsl #24
    add r4, r4, r0, asr #8
    add r9, r9, #6
    b next
.endm
op_bra:
    bl fetch8s
    TAKEN
op_bne:
    bl fetch8s
    movs r1, r10, lsl #16
    beq next
    TAKEN
op_beq:
    bl fetch8s
    movs r1, r10, lsl #16
    bne next
    TAKEN
op_bpl:
    bl fetch8s
    cmp r10, #0
    bmi next
    TAKEN
op_bmi:
    bl fetch8s
    cmp r10, #0
    bpl next
    TAKEN
op_bcc:
    bl fetch8s
    tst r11, #1
    bne next
    TAKEN
op_bcs:
    bl fetch8s
    tst r11, #1
    beq next
    TAKEN
op_bvc:
    bl fetch8s
    tst r11, #0x40
    bne next
    TAKEN
op_bvs:
    bl fetch8s
    tst r11, #0x40
    beq next
    TAKEN

/* ------------------------------------------------------------------ jumps, calls and the stack */
op_jmp:
    bl fetch16s
    mov r4, r0, lsl #16
    b next
op_jsr:
    bl fetch16s
    str r0, [sp, #PL2]
    mov r1, r4, lsr #16
    sub r1, r1, #1
    uxth r1, r1
    bl push16s
    ldr r0, [sp, #PL2]
    mov r4, r0, lsl #16
    b next
op_rts:
    bl pull16s
    add r0, r0, #1
    mov r4, r0, lsl #16
    b next
op_pha:
    tst r11, #0x20
    beq 16f
    and r1, r6, #255
    bl push8s
    b next
16: mov r1, r6
    bl push16s
    b next
.macro PUSHX name, reg
op_\name:
    tst r11, #0x10
    beq 16f
    and r1, \reg, #255
    bl push8s
    b next
16: mov r1, \reg
    bl push16s
    b next
.endm
    PUSHX phx, r7
    PUSHX phy, r8
op_pla:
    tst r11, #0x20
    beq 16f
    bl pull8s
    bic r6, r6, #255
    orr r6, r6, r0
    NZ8 r0
    b next
16: bl pull16s
    mov r6, r0
    NZ16 r0
    b next
.macro PULLX name, reg
op_\name:
    tst r11, #0x10
    beq 16f
    bl pull8s
    mov \reg, r0
    NZ8 r0
    b next
16: bl pull16s
    mov \reg, r0
    NZ16 r0
    b next
.endm
    PULLX plx, r7
    PULLX ply, r8
.ltorg

op_jsl:
    str r0, [sp, #PL2]
    mov r1, r5, lsr #16
    bl push8s
    mov r1, r4, lsr #16
    sub r1, r1, #1
    uxth r1, r1
    bl push16s
    ldr r0, [sp, #PL2]
    mov r4, r0, lsl #16
    and r5, r0, #0xFF0000
    b next
op_rtl:
    bl pull16s
    add r0, r0, #1
    mov r4, r0, lsl #16
    bl pull8s
    mov r5, r0, lsl #16
    b next
.ltorg

    .section .rodata
    .balign 4
optab:
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_dp, op_ora
    .word m_dp, op_mem_asl
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_immM, op_ora
    .word m_none, op_acc_asl
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_abs, op_ora
    .word m_abs, op_mem_asl
    .word m_long, op_ora
    .word m_none, op_bpl
    .word m_indy, op_ora
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_dpx, op_ora
    .word m_dpx, op_mem_asl
    .word m_cold, m_cold
    .word m_none, op_clc
    .word m_absy, op_ora
    .word m_none, op_acc_inc
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_absx, op_ora
    .word m_absx, op_mem_asl
    .word m_longx, op_ora
    .word m_none, op_jsr
    .word m_cold, m_cold
    .word m_long, op_jsl
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_dp, op_and
    .word m_dp, op_mem_rol
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_immM, op_and
    .word m_none, op_acc_rol
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_abs, op_and
    .word m_abs, op_mem_rol
    .word m_long, op_and
    .word m_none, op_bmi
    .word m_indy, op_and
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_dpx, op_and
    .word m_dpx, op_mem_rol
    .word m_cold, m_cold
    .word m_none, op_sec
    .word m_absy, op_and
    .word m_none, op_acc_dec
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_absx, op_and
    .word m_absx, op_mem_rol
    .word m_longx, op_and
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_dp, op_eor
    .word m_dp, op_mem_lsr
    .word m_cold, m_cold
    .word m_none, op_pha
    .word m_immM, op_eor
    .word m_none, op_acc_lsr
    .word m_cold, m_cold
    .word m_none, op_jmp
    .word m_abs, op_eor
    .word m_abs, op_mem_lsr
    .word m_long, op_eor
    .word m_none, op_bvc
    .word m_indy, op_eor
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_dpx, op_eor
    .word m_dpx, op_mem_lsr
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_absy, op_eor
    .word m_none, op_phy
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_absx, op_eor
    .word m_absx, op_mem_lsr
    .word m_longx, op_eor
    .word m_none, op_rts
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_dp, op_stz
    .word dc_dp, op_adc
    .word m_dp, op_mem_ror
    .word m_cold, m_cold
    .word m_none, op_pla
    .word dc_immM, op_adc
    .word m_none, op_acc_ror
    .word m_none, op_rtl
    .word m_cold, m_cold
    .word dc_abs, op_adc
    .word m_abs, op_mem_ror
    .word dc_long, op_adc
    .word m_none, op_bvs
    .word dc_indy, op_adc
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_dpx, op_stz
    .word dc_dpx, op_adc
    .word m_dpx, op_mem_ror
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word dc_absy, op_adc
    .word m_none, op_ply
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word dc_absx, op_adc
    .word m_absx, op_mem_ror
    .word dc_longx, op_adc
    .word m_none, op_bra
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_dp, op_sty
    .word m_dp, op_sta
    .word m_dp, op_stx
    .word m_cold, m_cold
    .word m_none, op_dey
    .word m_cold, m_cold
    .word m_none, op_txa
    .word m_cold, m_cold
    .word m_abs, op_sty
    .word m_abs, op_sta
    .word m_abs, op_stx
    .word m_long, op_sta
    .word m_none, op_bcc
    .word m_indy, op_sta
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_dpx, op_sty
    .word m_dpx, op_sta
    .word m_dpy, op_stx
    .word m_cold, m_cold
    .word m_none, op_tya
    .word m_absy, op_sta
    .word m_cold, m_cold
    .word m_none, op_txy
    .word m_abs, op_stz
    .word m_absx, op_sta
    .word m_absx, op_stz
    .word m_longx, op_sta
    .word m_immX, op_ldy
    .word m_cold, m_cold
    .word m_immX, op_ldx
    .word m_cold, m_cold
    .word m_dp, op_ldy
    .word m_dp, op_lda
    .word m_dp, op_ldx
    .word m_cold, m_cold
    .word m_none, op_tay
    .word m_immM, op_lda
    .word m_none, op_tax
    .word m_cold, m_cold
    .word m_abs, op_ldy
    .word m_abs, op_lda
    .word m_abs, op_ldx
    .word m_long, op_lda
    .word m_none, op_bcs
    .word m_indy, op_lda
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_dpx, op_ldy
    .word m_dpx, op_lda
    .word m_dpy, op_ldx
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_absy, op_lda
    .word m_cold, m_cold
    .word m_none, op_tyx
    .word m_absx, op_ldy
    .word m_absx, op_lda
    .word m_absy, op_ldx
    .word m_longx, op_lda
    .word m_immX, op_cpy
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_dp, op_cpy
    .word m_dp, op_cmp
    .word m_dp, op_mem_dec
    .word m_cold, m_cold
    .word m_none, op_iny
    .word m_immM, op_cmp
    .word m_none, op_dex
    .word m_cold, m_cold
    .word m_abs, op_cpy
    .word m_abs, op_cmp
    .word m_abs, op_mem_dec
    .word m_long, op_cmp
    .word m_none, op_bne
    .word m_indy, op_cmp
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_dpx, op_cmp
    .word m_dpx, op_mem_dec
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_absy, op_cmp
    .word m_none, op_phx
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_absx, op_cmp
    .word m_absx, op_mem_dec
    .word m_longx, op_cmp
    .word m_immX, op_cpx
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_dp, op_cpx
    .word dc_dp, op_sbc
    .word m_dp, op_mem_inc
    .word m_cold, m_cold
    .word m_none, op_inx
    .word dc_immM, op_sbc
    .word m_none, op_nop
    .word m_cold, m_cold
    .word m_abs, op_cpx
    .word dc_abs, op_sbc
    .word m_abs, op_mem_inc
    .word dc_long, op_sbc
    .word m_none, op_beq
    .word dc_indy, op_sbc
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word dc_dpx, op_sbc
    .word m_dpx, op_mem_inc
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word dc_absy, op_sbc
    .word m_none, op_plx
    .word m_cold, m_cold
    .word m_cold, m_cold
    .word dc_absx, op_sbc
    .word m_absx, op_mem_inc
    .word dc_longx, op_sbc
#endif
