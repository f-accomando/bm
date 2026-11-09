/*
 * snes_ppu_arm.S: the colour mix of a line of the SNES picture when the colour math uses the sub screen (src/bm/snes_ppu.c, ppu_line).
 * ARMv6 (ARM1176, Raspberry Pi 1 and Zero). The C version in snes_ppu.c does the same for any other CPU and is the reference:
 * tests/snes/ppu_equiv.c runs it on the PC, and the console must give the same picture byte for byte. Original code.
 *
 *   void ppu_mix_sub_arm(uint16_t *dst, const mixbuf_t *b, const mixctx_t *c);
 *
 * For each of the 256 pixels of the line:
 *   - the pixel takes the colour math if bit (layer) of the mask is set and the sprite pixel allows it;
 *   - the main colour and the colour behind it (the sub screen, or the fixed colour for the back drop) are lanes: red, green and blue
 *     times 8 in three bytes of a word (the palette 'pal8');
 *   - one lane instruction does the maths of the three channels: UQADD8 (add, a sum above 31 stops at 31), UQSUB8 (subtract, stops at 0),
 *     UHADD8 (add and halve), or UQSUB8 and then a halve;
 *   - each channel is looked up in a table of 32 words (RGB565 of that value with the brightness: rt, gt, bt);
 *   - a pixel with no maths is a palette lookup (lut).
 *
 * mixbuf_t (the line buffers, one base register): idx[256] at +0, lay[256] at +256, om[256] at +512, sidx[256] at +768, slay[256] at +1024.
 * mixctx_t, in the order that LDM loads it: pal8, lut (halfwords), rt, gt, bt (words), fix8, mask, mode (0 sub, 1 sub and halve, 2 add, 3 add and halve).
 *
 * Registers: r0 dst, r1 buffers, r2 pixels left, r3 pal8, r4 lut, r5 rt, r6 gt, r7 bt, r8 fix8, r9 mask, r10-r12 and lr scratch.
 */
    .syntax unified
    .section .text
    .arm
    .global ppu_mix_sub_arm
    .type   ppu_mix_sub_arm, %function

/* one loop of 256 pixels; \op is the lane maths on r10 (main colour) and r12 (the colour behind it); \pre runs before it, \post after it */
    .macro MIXLOOP op, pre=, post=
10:
    ldrb    r11, [r1, #256]             @ layer of the pixel
    ldrb    r12, [r1, #512]             @ the sprite allows maths (0 or 1)
    ldrb    r10, [r1], #1               @ palette index; the next pixel
    mov     r11, r9, lsr r11            @ mask >> layer
    ands    r11, r11, r12               @ bit 0: does this pixel take maths
    beq     20f
    ldrb    r11, [r1, #1023]            @ layer behind (the buffers moved by one pixel: 1024 - 1)
    ldrb    r12, [r1, #767]             @ palette index behind
    ldr     r10, [r3, r10, lsl #2]      @ main colour as lanes
    cmp     r11, #5                     @ 5: nothing behind, the fixed colour
    ldr     r12, [r3, r12, lsl #2]
    moveq   r12, r8
    \pre
    \op     r10, r10, r12
    \post
    and     r11, r10, #0xF8             @ red: the lane is 8 * (5 bits); the tables hold one word for each of the 32 values: offset = lane / 2
    ldr     r11, [r5, r11, lsr #1]
    and     r12, r10, #0xF800           @ green: the lane in byte 1, offset = (lane << 8) / 512 = lane / 2
    ldr     r12, [r6, r12, lsr #9]
    and     r10, r10, #0xF80000         @ blue: the lane in byte 2, offset = (lane << 16) / 131072 = lane / 2
    ldr     r10, [r7, r10, lsr #17]
    orr     r11, r11, r12
    orr     r11, r11, r10
    strh    r11, [r0], #2
    subs    r2, r2, #1
    bne     10b
    b       30f
20:                                      @ no maths: the palette lookup
    add     r10, r4, r10, lsl #1        @ (LDRH takes no scaled index)
    ldrh    r10, [r10]
    strh    r10, [r0], #2
    subs    r2, r2, #1
    bne     10b
30:
    .endm

ppu_mix_sub_arm:
    push    {r4-r11, lr}
    ldmia   r2, {r3-r10}                @ pal8, lut, rt, gt, bt, fix8, mask, mode
    mov     r2, #256
    cmp     r10, #2
    beq     .Ladd
    bhi     .Laddhalf
    cmp     r10, #1
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
    MIXLOOP op=uqsub8, post="mov r11, #0 ; uhadd8 r10, r10, r11"
    pop     {r4-r11, pc}
    .size   ppu_mix_sub_arm, .-ppu_mix_sub_arm
