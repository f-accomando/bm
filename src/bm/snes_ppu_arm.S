/*
 * snes_ppu_arm.S: the colour mix of a line of the SNES picture when the colour math uses the sub screen (src/bm/snes_ppu.c, ppu_line).
 * ARMv6 (ARM1176, Raspberry Pi 1 and Zero). The C version in snes_ppu.c does the same for any other CPU and is the reference:
 * tests/snes/ppu_equiv.c runs it on the PC (make test-snes-ppu), and make test-snes-ppu-arm runs this file in QEMU against the reference copy.
 * Original code.
 *
 *   void ppu_mix_sub_arm(uint16_t *dst, const mixbuf_t *b, const mixctx_t *c);
 *
 * For each of the 256 pixels of the line, with no branch that depends on the data (a wrong guess costs about 8 cycles):
 *   - the pixel takes the colour math if its flag byte is 1 (the PPU made the flag: layer, sprite and window rules);
 *   - the main colour and the colour behind it (the sub screen; sub index 0 is the back drop: the fixed colour) are lanes: red, green and blue
 *     times 8 in three bytes of a word (the palette 'pal8');
 *   - one lane instruction does the maths of the three channels: UQADD8 (add, a sum above 31 stops at 31), UQSUB8 (subtract, stops at 0),
 *     UHADD8 (add and halve), or UQSUB8 and then a halve;
 *   - each channel is looked up in a table of 32 words (RGB565 of that value with the brightness: rt, gt, bt);
 *   - a pixel with no flag takes the palette lookup (lut) in place of the result.
 *
 * mixbuf_t (the line buffers, one base register): idx[256] at +0, flag[256] at +256, unused[256] at +512, sidx[256] at +768.
 * mixctx_t, in the order that LDM loads it: pal8, lut (halfwords), rt, gt, bt (words), fix8, mode (0 sub, 1 sub and halve, 2 add, 3 add and halve).
 *
 * Registers: r0 dst, r1 buffers, r2 pixels left, r3 pal8, r4 lut, r5 rt, r6 gt, r7 bt, r8 fix8, r9-r12 and lr scratch (r9 holds the mode until the loop starts).
 */
    .syntax unified
    .section .text
    .arm
    .global ppu_mix_sub_arm
    .type   ppu_mix_sub_arm, %function

/* one loop of 256 pixels; \op is the lane maths on r9 (main colour) and lr (the colour behind it); \post runs after it */
    .macro MIXLOOP op, post=
10:
    ldrb    r10, [r1, #256]             @ the maths flag
    ldrb    r11, [r1], #1               @ palette index; the next pixel
    ldrb    r12, [r1, #767]             @ sub index (the buffers moved by one pixel: 768 - 1)
    ldr     r9, [r3, r11, lsl #2]       @ main colour as lanes
    add     r11, r4, r11, lsl #1        @ (LDRH takes no scaled index)
    ldr     lr, [r3, r12, lsl #2]       @ colour behind it as lanes
    ldrh    r11, [r11]                  @ the pixel with no maths
    cmp     r12, #0                     @ sub index 0: nothing behind, the fixed colour
    moveq   lr, r8
    \op     r9, r9, lr
    \post
    and     r12, r9, #0xF8              @ red: the lane is 8 * (5 bits); the tables hold one word for each of the 32 values: offset = lane / 2
    ldr     r12, [r5, r12, lsr #1]
    and     lr, r9, #0xF800             @ green: the lane in byte 1, offset = (lane << 8) / 512 = lane / 2
    ldr     lr, [r6, lr, lsr #9]
    and     r9, r9, #0xF80000           @ blue: the lane in byte 2, offset = (lane << 16) / 131072 = lane / 2
    ldr     r9, [r7, r9, lsr #17]
    orr     r12, r12, lr
    orr     r12, r12, r9
    cmp     r10, #0
    moveq   r12, r11                    @ no flag: the lookup
    strh    r12, [r0], #2
    subs    r2, r2, #1
    bne     10b
    .endm

ppu_mix_sub_arm:
    push    {r4-r11, lr}
    ldmia   r2, {r3-r9}                 @ pal8, lut, rt, gt, bt, fix8, mode
    mov     r2, #256
    cmp     r9, #2
    beq     .Ladd
    bhi     .Laddhalf
    cmp     r9, #1
    beq     .Lsubhalf
    MIXLOOP op=uqsub8
    pop     {r4-r11, pc}
.Ladd:
    MIXLOOP op=uqadd8
    pop     {r4-r11, pc}
.Laddhalf:
    MIXLOOP op=uhadd8
    pop     {r4-r11, pc}
.Lsubhalf:
    MIXLOOP op=uqsub8, post="mov lr, #0 ; uhadd8 r9, r9, lr"
    pop     {r4-r11, pc}
    .size   ppu_mix_sub_arm, .-ppu_mix_sub_arm
