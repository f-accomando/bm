/*
 * snes_gsu_arm.S: the instruction loop of the Super FX (GSU) for the ARMv6 console (Pi Zero W, Pi One). Original code.
 * snes_gsu.c has the C loop gsu_run: the reference. It is plain C, it runs on the PC and on the RGB30 (AArch64), and the test tests/snes/gsu_diff.c
 * compares both loops byte for byte (registers, flags, cart RAM, instruction count) on random programs. This file does the same work with
 * about 15 ARM instructions for a short opcode: the opcodes that need the pixel cache, the ROM buffer or the IRQ (PLOT, COLOR, GETB, GETC, RAMB, STOP)
 * call small C routines (gsu_c_*) that work on the state in the struct.
 *
 * Registers: r4 the struct gsu_t (R0-R15 are its first 16 words, each below 65536), r5 the master clocks left (the loop runs while it is positive),
 *   r6 PC, r7 the pointer to the code byte at PC in the code cache (valid when its low 4 bits are not 0: a line start goes to fetch_miss, which checks
 *   the line and fills it; after a jump r7 is 0 or the new byte), r8 the flags S and Z (lazy: Z = the low 16 bits are 0, S = bit 15 or bit 16),
 *   r9 SREG * 4, r10 DREG * 4 (byte offsets in the words of R), r11 the prefix (bit 0 ALT1, bit 1 ALT2, bit 2 B), r12 the count of instructions,
 *   lr the master clocks of one GSU cycle (1 or 2). r0 is the opcode; r0-r3 are scratch. CY, OV and the pending jump are in the struct (G_FCY, G_FOV, G_JT).
 * The next opcode is reached with one table jump (ldr pc, [pc, r0, lsl #2]: the table follows the instruction). A jump takes effect after the next opcode
 * (the delay slot): the jump opcodes store the target in G_JT and go to "slot", which runs the delay slot opcode and then moves PC.
 * A routine that calls C saves r12 and lr in the frame (CALLC); C keeps r4-r11.
 */
#if defined(__arm__) && !defined(__aarch64__) && !defined(SNES_GSU_C)
#include "snes_gsu_off.h"
    .syntax unified
    .arm
    .equ JF, 0x10000            @ a jump is pending (bit 16 of G_JT); bit 31: LJMP, the bank in bits 24-30

.macro FIN
    subs r5, r5, lr
    bgt next
    b exit
.endm

.macro CLRP
    mov r9, #0
    mov r10, #0
    mov r11, #0
.endm

/* the byte at PC into \rd (r0 is changed by a miss; r1-r3 are kept) and PC + 1 */
.macro FETCH rd
    tst r7, #15
    ldrbne \rd, [r7], #1
    addne r6, r6, #1
    bne 9f
    str lr, [sp, #4]
    bl fetch_miss
    ldr lr, [sp, #4]
    .ifnc \rd, r0
    mov \rd, r0
    .endif
    add r6, r6, #1
9:
.endm

/* a call to C: r12 and lr are saved; r0-r3 are lost */
.macro CALLC fn
    str r12, [sp, #0]
    str lr, [sp, #4]
    bl \fn
    ldr r12, [sp, #0]
    ldr lr, [sp, #4]
.endm

/* \out = the cart RAM word at \addr (16 bits); \t1 and \t2 are lost */
.macro RDW addr, out, t1, t2
    ldr \t1, [r4, #G_RM]
    ldr \t2, [r4, #G_RBASE]
    and \t1, \addr, \t1
    ldrb \out, [\t2, \t1]
    eor \t1, \t1, #1
    ldrb \t1, [\t2, \t1]
    orr \out, \out, \t1, lsl #8
.endm

/* the cart RAM word \val at \addr; \val, \t1 and \t2 are lost; the dirty flag is set */
.macro WRW addr, val, t1, t2
    ldr \t1, [r4, #G_RM]
    ldr \t2, [r4, #G_RBASE]
    and \t1, \addr, \t1
    strb \val, [\t2, \t1]
    eor \t1, \t1, #1
    mov \val, \val, lsr #8
    strb \val, [\t2, \t1]
    mov \val, #1
    strb \val, [r4, #G_DIRTY]
.endm

    .section .text.snes_gsu_hot, "ax", %progbits
    .global gsu_run_asm
    .type gsu_run_asm, %function
/* void gsu_run_asm(gsu_t *g, int clocks): clocks > 0, the program runs */
gsu_run_asm:
    push {r4-r11, lr}
    sub sp, sp, #20
    mov r4, r0
    mov r5, r1
    ldr r6, [r4, #(G_R + 60)]
    mov r7, #0
    ldr r8, [r4, #G_SZ]
    ldrb r9, [r4, #G_SREG]
    ldrb r10, [r4, #G_DREG]
    ldrb r11, [r4, #G_ALT]
    ldrb r0, [r4, #G_B]
    mov r9, r9, lsl #2
    mov r10, r10, lsl #2
    orr r11, r11, r0, lsl #2
    mov r12, #0
    ldr lr, [r4, #G_MUL]
    ldr r0, [r4, #G_JT]
    cmp r0, #0
    bne slot
next:
    tst r7, #15
    ldrbne r0, [r7], #1
    beq top_miss
top_cont:
    add r6, r6, #1
    add r12, r12, #1
dispatch:
    strh r6, [r4, #(G_R + 60)]
    ldr pc, [pc, r0, lsl #2]
    nop
optab:
    .word h_stop, h_nop, h_cache, h_lsr, h_rol, h_bra, h_bge, h_blt, h_bne, h_beq, h_bpl, h_bmi, h_bcc, h_bcs, h_bvc, h_bvs
    .rept 16
    .word h_to
    .endr
    .rept 16
    .word h_with
    .endr
    .rept 12
    .word h_st
    .endr
    .word h_loop, h_alt1, h_alt2, h_alt3
    .rept 12
    .word h_ld
    .endr
    .word h_plot, h_swap, h_color, h_not
    .rept 16
    .word h_add
    .endr
    .rept 16
    .word h_sub
    .endr
    .word h_merge
    .rept 15
    .word h_and
    .endr
    .rept 16
    .word h_mult
    .endr
    .word h_sbk
    .rept 4
    .word h_link
    .endr
    .word h_sex, h_asr, h_ror
    .rept 6
    .word h_jmp
    .endr
    .word h_lob, h_fmult
    .rept 16
    .word h_ibt
    .endr
    .rept 16
    .word h_from
    .endr
    .word h_hib
    .rept 15
    .word h_or
    .endr
    .rept 15
    .word h_inc
    .endr
    .word h_getc
    .rept 15
    .word h_dec
    .endr
    .word h_getb
    .rept 16
    .word h_iwt
    .endr

exit:
    strh r6, [r4, #(G_R + 60)]
    str r5, [r4, #G_BUDGET]
    str r8, [r4, #G_SZ]
    and r0, r11, #3
    strb r0, [r4, #G_ALT]
    mov r0, r11, lsr #2
    strb r0, [r4, #G_B]
    mov r9, r9, lsr #2
    strb r9, [r4, #G_SREG]
    mov r10, r10, lsr #2
    strb r10, [r4, #G_DREG]
    ldr r0, [r4, #G_INSNS]
    add r0, r0, r12
    str r0, [r4, #G_INSNS]
    add sp, sp, #20
    pop {r4-r11, pc}

/* the byte at PC when PC is at a line start of the cache or not in it: r0, r7 (the pointer after the byte); the clocks of a line fill come off r5 */
top_miss:
    str lr, [sp, #4]
    bl fetch_miss
    ldr lr, [sp, #4]
    b top_cont

fetch_miss:
    push {r1-r3, r12, lr}
    ldrh r1, [r4, #G_CBR]
    sub r1, r6, r1
    uxth r1, r1                 @ the offset in the 512 bytes of the cache
    cmp r1, #512
    bhs fm_c
    ldr r2, [r4, #G_CVALID]
    mov r3, r1, lsr #4
    mov r2, r2, lsr r3
    tst r2, #1
    beq fm_c
    add r7, r4, r1
    add r7, r7, #G_CACHE
    ldrb r0, [r7], #1
    pop {r1-r3, r12, pc}
fm_c:
    sub sp, sp, #4
    uxth r0, r6
    bl gsu_fetch_slow
    add sp, sp, #4
    sub r5, r5, r0, lsr #8
    uxtb r0, r0
    ldr r7, [r4, #G_IPN]
    pop {r1-r3, r12, pc}

/* ------------------------------------------------------------------ the common ends */
/* r1 = the result, r2 = n: store it in R(n) (a jump when n is 15), clear the prefix, take the cycle */
wr_n:
    cmp r2, #15
    beq wrn_j
    mov r2, r2, lsl #2
    strh r1, [r4, r2]
    CLRP
    FIN
wrn_j:
    uxth r1, r1
    orr r1, r1, #JF
    str r1, [r4, #G_JT]
    b clr_jfin
/* r1 = the result: store it in R(dreg) (a jump when dreg is 15), clear the prefix, take the cycle */
wr_d:
    cmp r10, #60
    beq wrd_j
    strh r1, [r4, r10]
    CLRP
    FIN
wrd_j:
    uxth r1, r1
    orr r1, r1, #JF
    str r1, [r4, #G_JT]
clr_jfin:
    CLRP
jfin:
    subs r5, r5, lr
    ble exit
/* a jump is pending: the opcode at PC is the delay slot; then PC is the target (and the cache, when the bank changes) */
slot:
    tst r7, #15
    ldrbne r0, [r7], #1
    beq slot_miss
slot_cont:
    add r6, r6, #1
    add r12, r12, #1
    ldr r1, [r4, #G_JT]
    uxth r6, r1
    cmp r1, #0
    blt slot_lj
    ldrh r2, [r4, #G_CBR]
    sub r2, r6, r2
    uxth r2, r2
    cmp r2, #512
    bhs slot_no
    ldr r3, [r4, #G_CVALID]
    mov r1, r2, lsr #4
    mov r3, r3, lsr r1
    tst r3, #1
    beq slot_no
    add r7, r4, r2
    add r7, r7, #G_CACHE
    b slot_done
slot_miss:
    str lr, [sp, #4]
    bl fetch_miss
    ldr lr, [sp, #4]
    b slot_cont
slot_lj:
    mov r2, r1, lsr #24
    and r2, r2, #0x7F
    strb r2, [r4, #G_PBR]
    bic r2, r6, #15
    strh r2, [r4, #G_CBR]
    mov r2, #0
    str r2, [r4, #G_CVALID]
slot_no:
    mov r7, #0
slot_done:
    mov r1, #0
    str r1, [r4, #G_JT]
    b dispatch

clr_fin:
    CLRP
    FIN

/* ------------------------------------------------------------------ the opcodes */
h_nop:
    b clr_fin
h_alt1:
    orr r11, r11, #1
    FIN
h_alt2:
    orr r11, r11, #2
    FIN
h_alt3:
    orr r11, r11, #3
    FIN
h_with:
    and r9, r0, #15
    mov r9, r9, lsl #2
    mov r10, r9
    orr r11, r11, #4
    FIN
h_to:
    tst r11, #4
    bne 1f
    and r10, r0, #15
    mov r10, r10, lsl #2
    FIN
1:  ldr r1, [r4, r9]
    and r2, r0, #15
    b wr_n
h_from:
    tst r11, #4
    bne 1f
    and r9, r0, #15
    mov r9, r9, lsl #2
    FIN
1:  and r2, r0, #15
    ldr r1, [r4, r2, lsl #2]
    mov r2, r1, lsr #7
    and r2, r2, #1
    str r2, [r4, #G_FOV]
    uxth r8, r1
    b wr_d

h_stop:
    CALLC gsu_c_stop
    add r6, r6, #1
    CLRP
    mov r5, #0
    b exit
h_cache:
    sub r1, r6, #1
    bic r1, r1, #15
    uxth r1, r1
    ldrh r2, [r4, #G_CBR]
    cmp r1, r2
    beq clr_fin
    strh r1, [r4, #G_CBR]
    mov r2, #0
    str r2, [r4, #G_CVALID]
    mov r7, #0
    b clr_fin

/* shifts and the one-register opcodes: r1 = RS */
h_lsr:
    ldr r1, [r4, r9]
    and r2, r1, #1
    str r2, [r4, #G_FCY]
    mov r1, r1, lsr #1
    mov r8, r1
    b wr_d
h_rol:
    ldr r1, [r4, r9]
    ldr r2, [r4, #G_FCY]
    mov r3, r1, lsr #15
    str r3, [r4, #G_FCY]
    orr r1, r2, r1, lsl #1
    uxth r1, r1
    mov r8, r1
    b wr_d
h_ror:
    ldr r1, [r4, r9]
    ldr r2, [r4, #G_FCY]
    and r3, r1, #1
    str r3, [r4, #G_FCY]
    mov r1, r1, lsr #1
    orr r1, r1, r2, lsl #15
    mov r8, r1
    b wr_d
h_asr:
    ldr r1, [r4, r9]
    and r2, r1, #1
    str r2, [r4, #G_FCY]
    mov r2, r1, lsl #16
    mov r2, r2, asr #17
    uxth r2, r2
    tst r11, #1
    beq 1f
    add r3, r1, #1
    cmp r3, #0x10000
    moveq r2, #0
1:  mov r8, r2
    mov r1, r2
    b wr_d
h_swap:
    ldr r1, [r4, r9]
    rev16 r1, r1
    mov r8, r1
    b wr_d
h_sex:
    ldr r1, [r4, r9]
    sxtb r1, r1
    uxth r1, r1
    mov r8, r1
    b wr_d
h_not:
    ldr r1, [r4, r9]
    mvn r1, r1
    uxth r1, r1
    mov r8, r1
    b wr_d
h_lob:
    ldr r1, [r4, r9]
    and r1, r1, #255
    b flag8
h_hib:
    ldr r1, [r4, r9]
    mov r1, r1, lsr #8
flag8:                          @ sz = r1 | (bit 7 of r1 ? 0x10000 : 0)
    mov r8, r1
    tst r1, #0x80
    orrne r8, r8, #0x10000
    b wr_d
h_merge:
    ldr r1, [r4, #(G_R + 28)]
    ldr r2, [r4, #(G_R + 32)]
    and r1, r1, #0xFF00
    orr r1, r1, r2, lsr #8
    tst r1, #0x80
    tsteq r1, #0x8000
    movne r8, #0x10000
    moveq r8, #0
    tst r1, #0xF0
    tsteq r1, #0xF000
    orreq r8, r8, #1
    tst r1, #0xC0
    tsteq r1, #0xC000
    movne r2, #1
    moveq r2, #0
    str r2, [r4, #G_FOV]
    tst r1, #0xE0
    tsteq r1, #0xE000
    movne r2, #1
    moveq r2, #0
    str r2, [r4, #G_FCY]
    b wr_d

/* ADD, ADC, ADD #, ADC # */
h_add:
    ldr r1, [r4, r9]
    and r2, r0, #15
    tst r11, #2
    ldreq r2, [r4, r2, lsl #2]
    tst r11, #1
    ldrne r3, [r4, #G_FCY]
    moveq r3, #0
    add r0, r1, r2
    add r0, r0, r3
    eor r3, r1, r2
    eor r1, r1, r0
    bic r1, r1, r3
    mov r1, r1, lsr #15
    and r1, r1, #1
    str r1, [r4, #G_FOV]
    mov r1, r0, lsr #16
    str r1, [r4, #G_FCY]
    uxth r8, r0
    mov r1, r0
    b wr_d
/* SUB, SBC, SUB #, CMP */
h_sub:
    ldr r1, [r4, r9]
    and r2, r0, #15
    and r3, r11, #3
    cmp r3, #2
    ldrne r2, [r4, r2, lsl #2]
    sub r0, r1, r2
    cmp r3, #1
    bne 1f
    ldr r3, [r4, #G_FCY]
    eor r3, r3, #1
    sub r0, r0, r3
1:  eor r3, r1, r2
    eor r1, r1, r0
    and r1, r1, r3
    mov r1, r1, lsr #15
    and r1, r1, #1
    str r1, [r4, #G_FOV]
    mov r1, r0, lsr #31
    eor r1, r1, #1
    str r1, [r4, #G_FCY]
    uxth r8, r0
    and r3, r11, #3
    cmp r3, #3
    beq clr_fin
    mov r1, r0
    b wr_d
h_and:
    ldr r1, [r4, r9]
    and r2, r0, #15
    tst r11, #2
    ldreq r2, [r4, r2, lsl #2]
    tst r11, #1
    bicne r1, r1, r2
    andeq r1, r1, r2
    mov r8, r1
    b wr_d
h_or:
    ldr r1, [r4, r9]
    and r2, r0, #15
    tst r11, #2
    ldreq r2, [r4, r2, lsl #2]
    tst r11, #1
    eorne r1, r1, r2
    orreq r1, r1, r2
    mov r8, r1
    b wr_d
h_mult:
    ldr r1, [r4, r9]
    and r2, r0, #15
    tst r11, #2
    ldreq r2, [r4, r2, lsl #2]
    tst r11, #1
    bne 1f
    sxtb r1, r1
    sxtb r2, r2
    b 2f
1:  uxtb r1, r1
    uxtb r2, r2
2:  mul r1, r1, r2
    uxth r1, r1
    mov r8, r1
    ldrb r2, [r4, #G_CFGR]
    tst r2, #0x20
    subeq r5, r5, lr
    b wr_d
h_fmult:
    ldr r1, [r4, r9]
    ldr r2, [r4, #(G_R + 24)]
    smulbb r3, r1, r2
    mov r1, r3, lsr #16
    tst r11, #1
    strhne r3, [r4, #(G_R + 16)]
    mov r2, r3, lsr #15
    and r2, r2, #1
    str r2, [r4, #G_FCY]
    mov r8, r1
    ldrb r2, [r4, #G_CFGR]
    tst r2, #0x20
    addne r2, lr, lr, lsl #1
    rsbeq r2, lr, lr, lsl #3
    sub r5, r5, r2
    b wr_d
h_link:
    and r2, r0, #15
    add r1, r6, r2
    uxth r1, r1
    str r1, [r4, #(G_R + 44)]
    b clr_fin
h_inc:
    and r2, r0, #15
    ldr r1, [r4, r2, lsl #2]
    add r1, r1, #1
    uxth r1, r1
    mov r8, r1
    b wr_n
h_dec:
    and r2, r0, #15
    ldr r1, [r4, r2, lsl #2]
    sub r1, r1, #1
    uxth r1, r1
    mov r8, r1
    b wr_n

/* LOOP, JMP, LJMP */
h_loop:
    ldr r1, [r4, #(G_R + 48)]
    sub r1, r1, #1
    uxth r1, r1
    str r1, [r4, #(G_R + 48)]
    mov r8, r1
    cmp r1, #0
    beq clr_fin
    ldr r2, [r4, #(G_R + 52)]
    orr r2, r2, #JF
    str r2, [r4, #G_JT]
    b clr_jfin
h_jmp:
    and r2, r0, #15
    ldr r2, [r4, r2, lsl #2]
    tst r11, #1
    beq 1f
    ldr r1, [r4, r9]
    and r2, r2, #0x7F
    orr r1, r1, r2, lsl #24
    orr r1, r1, #0x80000000
    orr r1, r1, #JF
    str r1, [r4, #G_JT]
    b clr_jfin
1:  orr r2, r2, #JF
    str r2, [r4, #G_JT]
    b clr_jfin

/* branches: r3 = 1 when taken; the offset byte is fetched either way. A branch keeps the prefix. */
h_bra:
    mov r3, #1
    b branch
h_bne:
    lsls r1, r8, #16
    movne r3, #1
    moveq r3, #0
    b branch
h_beq:
    lsls r1, r8, #16
    moveq r3, #1
    movne r3, #0
    b branch
h_bpl:
    tst r8, #0x18000
    moveq r3, #1
    movne r3, #0
    b branch
h_bmi:
    tst r8, #0x18000
    movne r3, #1
    moveq r3, #0
    b branch
h_bcc:
    ldr r2, [r4, #G_FCY]
    cmp r2, #0
    moveq r3, #1
    movne r3, #0
    b branch
h_bcs:
    ldr r2, [r4, #G_FCY]
    cmp r2, #0
    movne r3, #1
    moveq r3, #0
    b branch
h_bvc:
    ldr r2, [r4, #G_FOV]
    cmp r2, #0
    moveq r3, #1
    movne r3, #0
    b branch
h_bvs:
    ldr r2, [r4, #G_FOV]
    cmp r2, #0
    movne r3, #1
    moveq r3, #0
    b branch
h_bge:                          @ S == OV
    ldr r2, [r4, #G_FOV]
    tst r8, #0x18000
    movne r1, #1
    moveq r1, #0
    cmp r1, r2
    moveq r3, #1
    movne r3, #0
    b branch
h_blt:
    ldr r2, [r4, #G_FOV]
    tst r8, #0x18000
    movne r1, #1
    moveq r1, #0
    cmp r1, r2
    movne r3, #1
    moveq r3, #0
branch:
    FETCH r1
    cmp r3, #0
    beq 1f
    sxtb r1, r1
    add r1, r1, r6
    uxth r1, r1
    orr r1, r1, #JF
    str r1, [r4, #G_JT]
    b jfin
1:  FIN

/* RAM: STW/STB (n = 0-11), LDW/LDB, SBK; R(n) is the address */
h_st:
    and r2, r0, #15
    ldr r2, [r4, r2, lsl #2]
    strh r2, [r4, #G_RAMADDR]
    ldr r1, [r4, r9]
    tst r11, #1
    bne 1f
    WRW r2, r1, r0, r3
    b 2f
1:  ldr r3, [r4, #G_RM]
    ldr r0, [r4, #G_RBASE]
    and r2, r2, r3
    strb r1, [r0, r2]
    mov r1, #1
    strb r1, [r4, #G_DIRTY]
2:  sub r5, r5, lr
    b clr_fin
h_sbk:
    ldrh r2, [r4, #G_RAMADDR]
    ldr r1, [r4, r9]
    WRW r2, r1, r0, r3
    sub r5, r5, lr
    b clr_fin
h_ld:
    and r2, r0, #15
    ldr r2, [r4, r2, lsl #2]
    strh r2, [r4, #G_RAMADDR]
    ldr r3, [r4, #G_RM]
    ldr r0, [r4, #G_RBASE]
    and r2, r2, r3
    ldrb r1, [r0, r2]
    tst r11, #1
    bne 1f
    eor r2, r2, #1
    ldrb r3, [r0, r2]
    orr r1, r1, r3, lsl #8
1:  ldr r3, [r4, #G_RAMCYC]
    sub r5, r5, r3
    b wr_d

/* IBT, LMS, SMS: the operand byte; R(n) */
h_ibt:
    and r2, r0, #15
    FETCH r1
    and r3, r11, #3
    cmp r3, #1
    beq ibt_lms
    cmp r3, #2
    beq ibt_sms
    sxtb r1, r1
    uxth r1, r1
    b wr_n
ibt_lms:
    mov r1, r1, lsl #1
    strh r1, [r4, #G_RAMADDR]
    RDW r1, r1, r3, r0
    ldr r3, [r4, #G_RAMCYC]
    add r3, r3, lr, lsl #1
    sub r5, r5, r3
    b wr_n
ibt_sms:
    mov r1, r1, lsl #1
    strh r1, [r4, #G_RAMADDR]
    ldr r3, [r4, r2, lsl #2]
    WRW r1, r3, r0, r2
    sub r5, r5, lr, lsl #1
    b clr_fin
/* IWT, LM, SM: the operand word */
h_iwt:
    and r2, r0, #15
    FETCH r1
    FETCH r3
    orr r1, r1, r3, lsl #8
    and r3, r11, #3
    cmp r3, #1
    beq iwt_lm
    cmp r3, #2
    beq iwt_sm
    b wr_n
iwt_lm:
    strh r1, [r4, #G_RAMADDR]
    RDW r1, r1, r3, r0
    ldr r3, [r4, #G_RAMCYC]
    add r3, r3, lr, lsl #1
    add r3, r3, lr
    sub r5, r5, r3
    b wr_n
iwt_sm:
    strh r1, [r4, #G_RAMADDR]
    ldr r3, [r4, r2, lsl #2]
    WRW r1, r3, r0, r2
    add r3, lr, lr, lsl #1
    sub r5, r5, r3
    b clr_fin

/* the opcodes done by C routines (they work on the struct) */
h_plot:
    tst r11, #1
    bne plot_rpix
    CALLC gsu_c_plot
    sub r5, r5, lr
    b clr_fin
plot_rpix:
    CALLC gsu_c_rpix
    mov r1, r0
    mov r8, r0
    ldr r3, [r4, #G_RAMCYC]
    add r3, r3, r3, lsl #1
    sub r5, r5, r3
    b wr_d
h_color:
    ldr r0, [r4, r9]
    mov r1, r11
    CALLC gsu_c_color
    b clr_fin
h_getc:
    and r3, r11, #3
    cmp r3, #2
    beq getc_ramb
    cmp r3, #3
    beq getc_romb
    CALLC gsu_c_getc
    sub r5, r5, r0
    b clr_fin
getc_ramb:
    ldr r0, [r4, r9]
    CALLC gsu_c_ramb
    b clr_fin
getc_romb:
    ldr r0, [r4, r9]
    CALLC gsu_c_romb
    b clr_fin
h_getb:
    mov r0, r11
    ldr r1, [r4, r9]
    CALLC gsu_c_getb
    sub r5, r5, r0, lsr #16
    uxth r1, r0
    b wr_d

#endif
