; testrom.s: the program of tests/snes/make_testrom.py written in 65C816 assembly.
; tests/snes/asm_rom_test.py builds both and compares the bytes. consts.inc (written by the test) gives the
; addresses and sizes of the data tables, and data.bin holds the tables.
.lorom
.include "consts.inc"

; the DMA of the original: channel ch, mode, B-bus register, source address, size
.macro dma ch, mode, breg, src, size
    lda #mode
    sta $4300+ch*16
    lda #breg
    sta $4301+ch*16
    ldx #src & $FFFF
    stx $4302+ch*16
    lda #0
    sta $4304+ch*16
    ldx #size
    stx $4305+ch*16
    lda #1 << ch
    sta $420B
.endmacro

.org $8000
reset:
cop:
brk:
abort:
irq:
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
    lda #$00
    sta $2121
    dma 0, $00, $22, pal, 512
    lda #$80
    sta $2115
    ldx #$0000
    stx $2116
    dma 0, $01, $18, tiles, tiles_len
    ldx #$0400
    stx $2116
    dma 0, $01, $18, tmap, tmap_len
    ldx #$4000
    stx $2116
    dma 0, $01, $18, spr, spr_len
    stz $2102
    stz $2103
    dma 0, $00, $04, oam, 544
    lda #$00
    sta $2105
    lda #$04
    sta $2107
    lda #$00
    sta $210B
    lda #$02
    sta $2101
    lda #$11
    sta $212C
    lda #$02
    sta $4310
    lda #$0D
    sta $4311
    ldx #hdma
    stx $4312
    lda #$00
    sta $4314
    lda #$02
    sta $420C
    lda #$0F
    sta $2100               ; screen on
    lda #$81
    sta $4200               ; NMI on, auto joypad
@loop:
    wai
    jmp @loop

.org $8200
nmi:
    pha
    lda $4210
    inc a:$0010             ; the original uses the absolute form
    lda a:$0010
    stz $2102
    stz $2103
    sta $2104
    lda #100
    sta $2104
    pla
    rti

.org $8400
data:
    .incbin "data.bin"
