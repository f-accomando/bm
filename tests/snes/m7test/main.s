; m7test: an original Mode 7 test ROM for the bm SNES emulator (tests/snes/m7test/main.s, built by tests/snes/build_m7test.sh).
; It also serves as an SDK example (see docs/progress/snes-gpu-pi.md, "Mode 7 test ROM").
;
; What it does: BG1 in Mode 7 shows a 128x128 map (wrap) of 16 tiles with 8 bits a pixel. The tiles and the 256-colour palette
; are made by the code below (a checker and a colour gradient). Every frame the NMI turns the matrix by one step of the angle
; (frame counter), scrolls the picture and sets the registers of the variant. The picture depends only on the frame counter
; and the variant, so a run is the same every time.
;
; Variants (latched when the key is pressed; with no input the variant stays 1):
;   1 pad B or nothing  BG1 alone, no sprite, no window, no colour math
;   2 pad Y             variant 1 plus one visible sprite (TM = $11)
;   3 pad A             BG1 alone; HDMA changes the matrix words on every line (a floor, like F-Zero)
;   4 pad X             BG1 alone with colour math (add a fixed colour)
;   5 pad L             BG1 and BG2 on (TM = $03, EXTBG on)
;
; Work RAM: $10 frame counter (word), $12 variant (byte), $14 joypad (word), $20 HDMA scale (word).
.lorom

frame   = $10
variant = $12
pad     = $14
scale   = $20
tt      = $30
yy      = $31
xx      = $32
tmp     = $33
ii      = $34
hh      = $35
ll      = $36

; write a 16-bit value in A to a register that takes two writes (low, high); A is 16 bits before and after
.macro w2 reg
    sep #$20
    .a8
    sta reg
    xba
    sta reg
    rep #$20
    .a16
.endmacro

.org $8000
reset:
    sei
    clc
    xce
    rep #$10
    sep #$20
    .a8
    .i16
    lda #$8F
    sta $2100               ; forced blank
    stz $4200
    stz $420B
    stz $420C
    ldx #$1FFF
    txs
    ; ---- state
    stz frame
    stz frame+1
    lda #1
    sta variant
    stz pad
    stz pad+1
    ; ---- Mode 7 map: the low byte of the words $0000-$3FFF holds the tile number, (mx xor my) and 15
    stz $2115               ; the address grows after the low byte
    ldx #0
    stx $2116
    ldy #0
@mrow:
    ldx #0
@mcol:
    tya
    sta tmp
    txa
    eor tmp
    and #$0F
    sta $2118
    inx
    cpx #128
    bne @mcol
    iny
    cpy #128
    bne @mrow
    ; ---- tiles: the high byte of the words from $0000 holds the pixels (8 bpp). Tile t, pixel (x,y):
    ;      t*16 | checker (x xor y, bit 2) * 8 | ((x+y)/2 and 7); never 0 (colour 0 is transparent)
    lda #$80
    sta $2115               ; the address grows after the high byte
    ldx #0
    stx $2116
    stz tt
@tile:
    stz yy
@ty:
    stz xx
@tx:
    lda xx
    eor yy
    and #4
    asl a
    sta tmp
    lda xx
    clc
    adc yy
    lsr a
    and #7
    ora tmp
    ora tt
    bne @nz
    lda #1
@nz:
    sta $2119
    inc xx
    lda xx
    cmp #8
    bne @tx
    inc yy
    lda yy
    cmp #8
    bne @ty
    lda tt
    clc
    adc #16
    sta tt
    bne @tile               ; 16 tiles, then tt wraps to 0
    ; ---- palette: colour i has red = (i>>4)*2, green = (i&15)*2, blue = ((i>>4) xor (i&15))*2
    stz $2121
    stz ii
@pal:
    lda ii
    lsr a
    lsr a
    lsr a
    lsr a
    sta hh
    asl a
    sta tmp                 ; red
    lda ii
    and #3
    asl a
    asl a
    asl a
    asl a
    asl a
    asl a                   ; the low 2 bits of i give the low bits of green
    ora tmp
    sta $2122
    lda ii
    and #15
    sta ll
    lsr a
    lsr a
    sta tmp                 ; the upper bits of green
    lda hh
    eor ll
    asl a
    asl a
    asl a                   ; blue
    ora tmp
    sta $2122
    inc ii
    bne @pal
    ; ---- sprite: 8x8, 4 bpp, tile 0 at the word $4000, all pixels colour 1 (an entry of the gradient)
    lda #$80
    sta $2115
    ldx #$4000
    stx $2116
    ldx #8
@sp1:
    lda #$FF
    sta $2118
    stz $2119
    dex
    bne @sp1
    ldx #8
@sp2:
    stz $2118
    stz $2119
    dex
    bne @sp2
    lda #$02
    sta $2101               ; sprite names at the word $4000, sizes 8x8 and 16x16
    ; OAM: sprite 0 visible at (120,100) with priority 3; the other 127 below the screen (y = 240)
    stz $2102
    stz $2103
    lda #120
    sta $2104
    lda #100
    sta $2104
    stz $2104
    lda #$30
    sta $2104
    ldx #127
@oam:
    stz $2104
    lda #$F0
    sta $2104
    stz $2104
    stz $2104
    dex
    bne @oam
    ldx #32
@oamh:
    stz $2104               ; the high table: all sprites small, x bit 8 clear
    dex
    bne @oamh
    ; ---- HDMA tables in the work RAM (bank $7E): 224 lines of 1 + 4 bytes
    ;      channel 0 ($211B, $211C): a = scale, b = 0     channel 1 ($211D, $211E): c = 0, d = scale
    ;      scale = $0400 - 4 * line (zoomed out at the top, like a floor going to the horizon)
    ldx #$0400
    stx scale
    ldx #0
    ldy #224
@hl:
    lda #1
    sta $7EA000,x
    sta $7EA500,x
    lda scale
    sta $7EA001,x
    sta $7EA503,x
    lda scale+1
    sta $7EA002,x
    sta $7EA504,x
    lda #0
    sta $7EA003,x
    sta $7EA004,x
    sta $7EA501,x
    sta $7EA502,x
    rep #$20
    .a16
    lda scale
    sec
    sbc #4
    sta scale
    sep #$20
    .a8
    inx
    inx
    inx
    inx
    inx
    dey
    bne @hl
    lda #0
    sta $7EA000,x           ; end of the tables
    sta $7EA500,x
    lda #$03
    sta $4300               ; mode 3: two registers, two writes each
    sta $4310
    lda #$1B
    sta $4301
    lda #$1D
    sta $4311
    ldx #$A000
    stx $4302
    ldx #$A500
    stx $4312
    lda #$7E
    sta $4304
    sta $4314
    ; ---- fixed state of the picture
    lda #7
    sta $2105               ; Mode 7
    stz $211A               ; wrap
    stz $2130               ; colour math: fixed colour, never prevented
    stz $2131
    lda #$2A
    sta $2132               ; red 10
    lda #$45
    sta $2132               ; green 5
    lda #$85
    sta $2132               ; blue 5
    lda #$0F
    sta $2100               ; screen on
    lda #$81
    sta $4200               ; NMI on, auto joypad
@idle:
    wai
    jmp @idle

nmi:
    rep #$30
    .a16
    .i16
    pha
    phx
    phy
    sep #$20
    .a8
    lda $4210               ; acknowledge
@jw:
    lda $4212
    and #1
    bne @jw                 ; wait for the auto read
    rep #$20
    .a16
    lda $4218
    sta pad
    bit #$8000
    beq @n1
    ldy #1
    sty variant
@n1:
    lda pad
    bit #$4000
    beq @n2
    ldy #2
    sty variant
@n2:
    lda pad
    bit #$0080
    beq @n3
    ldy #3
    sty variant
@n3:
    lda pad
    bit #$0040
    beq @n4
    ldy #4
    sty variant
@n4:
    lda pad
    bit #$0020
    beq @n5
    ldy #5
    sty variant
@n5:
    ; ---- the matrix: a = cos, b = sin, c = -sin, d = cos of the angle (the frame counter, 256 steps a turn)
    lda frame
    and #$00FF
    asl a
    tax
    lda sintab,x
    tay                     ; sin
    lda frame
    clc
    adc #64
    and #$00FF
    asl a
    tax
    lda sintab,x            ; cos
    w2 $211B
    w2 $211E
    tya
    w2 $211C
    eor #$FFFF
    inc a
    w2 $211D
    lda #128
    w2 $211F                ; centre
    w2 $2120
    lda frame
    asl a
    clc
    adc frame               ; frame * 3
    w2 $210D
    lda frame
    asl a
    w2 $210E
    ; ---- the registers of the variant
    sep #$20
    .a8
    lda #$01
    sta $212C               ; BG1 only
    stz $2131
    stz $2133
    stz $420C
    lda variant
    cmp #2
    bne @v3
    lda #$11
    sta $212C               ; plus the sprites
    bra @done
@v3:
    cmp #3
    bne @v4
    lda #$03
    sta $420C               ; HDMA on channels 0 and 1
    bra @done
@v4:
    cmp #4
    bne @v5
    lda #$01
    sta $2131               ; add the fixed colour to BG1
    bra @done
@v5:
    cmp #5
    bne @done
    lda #$03
    sta $212C               ; BG1 and BG2
    lda #$40
    sta $2133               ; EXTBG
@done:
    rep #$20
    .a16
    inc frame
    ply
    plx
    pla
    rti

.org $8800
.include "sintab.inc"
