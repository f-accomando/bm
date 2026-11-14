/*
 * snes_ppu_arm.S: the colour mix of a line of the SNES picture when the colour math uses the sub screen (core/src/bm/snes_ppu.c, ppu_line).
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
 * Mode bit 2 (brightness 15): the loop with four pixels a turn and no tables (PIX, FASTLOOP below).
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

/* The loop for brightness 15 (mode bit 2): four pixels a turn. A group with no flag is four palette lookups. In a group with a flag the maths
 * runs for the four pixels and the flag selects the lookup (no branch on the data). The result is packed from the lanes with shifts (no tables):
 * red = lane bits 3-7 to 11-15, green = bits 11-15 to 6-10 and its top bit also to bit 5, blue = bits 19-23 to 0-4. r5 holds the four flags. */
    .macro PIX k, op, post=
    ldrb    r11, [r1, #\k]
    ldrb    r12, [r1, #768+\k]
    ldr     r9, [r3, r11, lsl #2]
    ldr     lr, [r3, r12, lsl #2]
    add     r11, r4, r11, lsl #1
    cmp     r12, #0
    moveq   lr, r8
    ldrh    r11, [r11]
    \op     r9, r9, lr
    \post
    lsr     r12, r9, #19
    and     r6, r9, #0xF8
    orr     r12, r12, r6, lsl #8
    and     r6, r9, #0xF800
    orr     r12, r12, r6, lsr #5
    and     r6, r9, #0x8000
    orr     r12, r12, r6, lsr #10
    tst     r5, #(1 << (8 * \k))
    moveq   r12, r11
    strh    r12, [r0, #2 * \k]
    .endm

    .macro FASTLOOP op, post=
    add     r2, r1, #256                @ end of the line
10:
    ldr     r5, [r1, #256]              @ the flags of 4 pixels
    cmp     r5, #0
    bne     12f
    ldrb    r9, [r1]
    ldrb    r10, [r1, #1]
    ldrb    r11, [r1, #2]
    ldrb    r12, [r1, #3]
    add     r9, r4, r9, lsl #1
    add     r10, r4, r10, lsl #1
    add     r11, r4, r11, lsl #1
    add     r12, r4, r12, lsl #1
    ldrh    r9, [r9]
    ldrh    r10, [r10]
    ldrh    r11, [r11]
    ldrh    r12, [r12]
    strh    r9, [r0]
    strh    r10, [r0, #2]
    strh    r11, [r0, #4]
    strh    r12, [r0, #6]
    b       13f
12:
    PIX     0, \op, "\post"
    PIX     1, \op, "\post"
    PIX     2, \op, "\post"
    PIX     3, \op, "\post"
13:
    add     r0, r0, #8
    add     r1, r1, #4
    cmp     r1, r2
    bne     10b
    .endm

ppu_mix_sub_arm:
    push    {r4-r11, lr}
    ldmia   r2, {r3-r9}                 @ pal8, lut, rt, gt, bt, fix8, mode
    mov     r2, #256
    tst     r9, #4
    bne     .Lfast
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
.Lfast:
    and     r9, r9, #3
    cmp     r9, #2
    beq     .Fadd
    bhi     .Faddhalf
    cmp     r9, #1
    beq     .Fsubhalf
    FASTLOOP op=uqsub8
    pop     {r4-r11, pc}
.Fadd:
    FASTLOOP op=uqadd8
    pop     {r4-r11, pc}
.Faddhalf:
    FASTLOOP op=uhadd8
    pop     {r4-r11, pc}
.Fsubhalf:
    FASTLOOP op=uqsub8, post="mov lr, #0 ; uhadd8 r9, r9, lr"
    pop     {r4-r11, pc}
    .size   ppu_mix_sub_arm, .-ppu_mix_sub_arm

/*
 * uint8_t *ppu_bg_cols_arm(uint8_t *ti, uint8_t *end, const bgctx_t *c);
 *
 * The tile columns of one line of a background layer with 8x8 tiles (snes_ppu.c, bg_row; the C loop is the reference). Each column of 8 pixels:
 * the tile map entry, the decoded row of the tile (tc), the flips, the palette and the priority; ti gets the colour indexes, ti + c->tpd the
 * priorities, 8 bytes a column, nothing for a clear row (the caller cleared the buffers). It stops at end, or at a column whose tile is not
 * decoded yet: the return value is where it stopped (the caller decodes that tile and calls again from there).
 *
 * bgctx_t: +0 vram, +4 base_a, +8 tcol (in and out), +12 wide (0x800 or 0), +16 chunk base, +20 chunk mask, +24 tc, +28 tv,
 * +32 row offset, +36 row offset of a vertical flip, +40 tpd (tp - ti), +44 any (out), +48 pal words[8], +80 priority words[2].
 *
 * Registers: r0 ti, r1 end, r2 tcol, r3 ctx, r4 vram, r5 base_a, r6 tc, r7 tv, r8 tpd, r9-r12 and lr scratch.
 */
    .global ppu_bg_cols_arm
    .type   ppu_bg_cols_arm, %function
ppu_bg_cols_arm:
    push    {r4-r11, lr}
    mov     r3, r2
    ldr     r4, [r3, #0]
    ldr     r5, [r3, #4]
    ldr     r2, [r3, #8]
    ldr     r6, [r3, #24]
    ldr     r7, [r3, #28]
    ldr     r8, [r3, #40]
    cmp     r0, r1
    beq     .Bdone
.Bloop:
    and     r9, r2, #31
    add     r9, r5, r9, lsl #1          @ the entry of tile column tcol
    tst     r2, #32
    ldrne   r10, [r3, #12]
    addne   r9, r9, r10                 @ the second screen of a wide map
    uxth    r9, r9
    ldrh    r10, [r4, r9]               @ e: the tile map entry
    lsl     r11, r10, #22
    ldr     r12, [r3, #16]
    add     r11, r12, r11, lsr #22      @ chunk base + tile
    ldr     r12, [r3, #20]
    and     r11, r11, r12               @ the chunk (the decoded tile)
    ldrb    r12, [r7, r11]
    cmp     r12, #0
    beq     .Bdone                      @ not decoded: back to C
    tst     r10, #0x8000
    ldreq   r12, [r3, #32]
    ldrne   r12, [r3, #36]
    add     r12, r12, r11, lsl #6
    add     r12, r6, r12
    ldm     r12, {r11, r12}             @ lo, hi: the row of 8 pixels
    orrs    lr, r11, r12
    beq     .Bnext                      @ a clear row writes nothing
    tst     r10, #0x4000                @ horizontal flip: swap and byte-reverse
    revne   lr, r12
    revne   r12, r11
    movne   r11, lr
    lsr     r9, r10, #10
    and     r9, r9, #7
    add     r9, r3, r9, lsl #2
    ldr     r9, [r9, #48]               @ the palette base as four bytes
    tst     r10, #0x2000
    ldreq   r10, [r3, #80]
    ldrne   r10, [r3, #84]              @ the priority as four bytes
    mvn     lr, #0
    uadd8   lr, r11, lr                 @ GE: the pixels that are not clear
    mov     lr, #0
    add     r11, r11, r9
    sel     r11, r11, lr
    str     r11, [r0]
    sel     r11, r10, lr
    str     r11, [r0, r8]
    mvn     lr, #0
    uadd8   lr, r12, lr
    mov     lr, #0
    add     r12, r12, r9
    sel     r12, r12, lr
    str     r12, [r0, #4]
    sel     r12, r10, lr
    add     r9, r0, r8
    str     r12, [r9, #4]
    mov     lr, #1
    str     lr, [r3, #44]
.Bnext:
    add     r0, r0, #8
    add     r2, r2, #1
    and     r2, r2, #127
    cmp     r0, r1
    bne     .Bloop
.Bdone:
    str     r2, [r3, #8]
    pop     {r4-r11, pc}
    .size   ppu_bg_cols_arm, .-ppu_bg_cols_arm
