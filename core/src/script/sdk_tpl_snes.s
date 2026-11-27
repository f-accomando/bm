; ============================================================================
; SNES game template (LoROM, 65C816). It builds as it is: a blue screen and a
; ball (the tile 1 of the sprite sheet) that the pad moves.
; Build: Esc > Build ROM (Ctrl+B).   Pad 1 is read every frame (see "pad").
; Sections marked ;@name ... ;@end are where the Sprite, Sounds and Map tabs
; will read and write their data. Keep the markers.
; ============================================================================

; ---- hardware registers ----------------------------------------------------
INIDISP   = $2100          ; screen on/off and brightness
OBSEL     = $2101          ; the sizes of the sprites and where their tiles are
OAMADDL   = $2102
OAMADDH   = $2103
BGMODE    = $2105
VMAIN     = $2115          ; how the video memory address goes on
VMADDL    = $2116
VMADDH    = $2117
CGADD     = $2121
CGDATA    = $2122
TM        = $212C          ; what shows on the screen
NMITIMEN  = $4200          ; NMI and automatic pad reading
MDMAEN    = $420B          ; starts a DMA
HDMAEN    = $420C
DMAP0     = $4300          ; the DMA channel 0: mode, port, address, bank, size
BBAD0     = $4301
A1T0L     = $4302
A1B0      = $4304
DAS0L     = $4305
RDNMI     = $4210
HVBJOY    = $4212
JOY1L     = $4218          ; pad 1, read by the machine in the vertical blank

; ---- pad bits (the 16 bits of JOY1L and JOY1H: pad, pad_old, pad_new) ------
BTN_B      = $8000
BTN_Y      = $4000
BTN_SELECT = $2000
BTN_START  = $1000
BTN_UP     = $0800
BTN_DOWN   = $0400
BTN_LEFT   = $0200
BTN_RIGHT  = $0100
BTN_A      = $0080
BTN_X      = $0040
BTN_L      = $0020
BTN_R      = $0010

; ---- variables: direct page $00-$0F, the sprites at $0200 ------------------
pad       = $00            ; word: buttons held now
pad_old   = $02            ; word: buttons held in the frame before
pad_new   = $04            ; word: buttons that went down in this frame
frame     = $06            ; byte: counts the frames (the NMI adds 1)
ball_x    = $07            ; where the ball is on the screen (256 x 224)
ball_y    = $08
oam       = $0200          ; the 544 bytes of the sprites, sent at each frame

.lorom
.org $8000

; ---- start -----------------------------------------------------------------
reset:
    sei
    clc
    xce                    ; native mode
    rep #$10               ; X and Y 16 bits
    sep #$20               ; A 8 bits
    .a8
    .i16
    ldx #$1FFF
    txs                    ; the stack at the top of the first WRAM page
    lda #$8F
    sta INIDISP            ; screen off (forced blank) while we set up
    stz NMITIMEN           ; no NMI, no pad reading yet
    stz MDMAEN             ; no DMA
    stz HDMAEN             ; no HDMA
    stz BGMODE             ; mode 0
    ldx #0                 ; clear the variables
@clear:
    stz $00,x
    inx
    cpx #$10
    bne @clear

    ; ---- palette: colour 0 is the colour behind everything; $7C00 is blue ----
    stz CGADD
    stz CGDATA             ; low byte
    lda #$7C
    sta CGDATA             ; high byte

    ; ---- the sprite sheet to the video memory at $0000, with a DMA -----------
    lda #$80
    sta VMAIN
    stz VMADDL
    stz VMADDH
    lda #$01
    sta DMAP0              ; two bytes at a time, to $2118 and $2119
    lda #$18
    sta BBAD0
    ldx #sprites_chr
    stx A1T0L
    stz A1B0
    ldx #sprites_chr_end-sprites_chr
    stx DAS0L
    beq @no_tiles          ; a size of 0 would send 64 KB
    lda #1
    sta MDMAEN
@no_tiles:
    lda #128               ; the colours 128 to 143: the sprite palette 0
    sta CGADD
    ldx #0
@colours:
    lda sprites_pal,x
    sta CGDATA
    inx
    cpx #32
    bne @colours
    stz OBSEL              ; sprites of 8x8 and 16x16, tiles from $0000

    ; ---- every sprite below the screen, the extra bits 0 ---------------------
    ldx #0
@hide:
    stz oam,x
    stz oam+2,x
    stz oam+3,x
    lda #$F0
    sta oam+1,x
    inx
    inx
    inx
    inx
    cpx #512
    bne @hide
@high:
    stz oam,x              ; 512 to 543
    inx
    cpx #544
    bne @high
    lda #124
    sta ball_x
    lda #108
    sta ball_y
    lda #$10
    sta TM                 ; the sprites on the screen

    lda #$0F
    sta INIDISP            ; screen on, full brightness
    lda #$81
    sta NMITIMEN           ; NMI on, and the machine reads pad 1 every frame

; ---- game loop: once per frame ---------------------------------------------
main:
    lda frame
@wait:
    wai                    ; sleep until the next interrupt
    cmp frame              ; the NMI added 1: a new frame
    beq @wait

    jsr read_pad
    jsr update
    bra main

; ---- your game: called once per frame, after the pad was read --------------
update:
    ; The example moves the ball with the pad. pad+1 has B Y Select Start Up
    ; Down Left Right; pad_new has the buttons that went down now.
    lda pad+1
    bit #>BTN_LEFT
    beq @right
    dec ball_x
@right:
    bit #>BTN_RIGHT
    beq @up
    inc ball_x
@up:
    bit #>BTN_UP
    beq @down
    dec ball_y
@down:
    bit #>BTN_DOWN
    beq @draw
    inc ball_y
@draw:
    lda ball_x
    sta oam+0              ; the sprite 0: x, y, tile, attribute
    lda ball_y
    sta oam+1
    lda #1
    sta oam+2              ; the tile 1 of the sheet
    lda #$30
    sta oam+3              ; in front, palette 0
    rts

; ---- pad 1 -> pad, pad_old, pad_new ----------------------------------------
read_pad:
@ready:
    lda HVBJOY
    and #$01
    bne @ready             ; wait while the machine is still reading the pad
    rep #$20
    .a16
    lda pad
    sta pad_old
    lda JOY1L
    sta pad
    eor pad_old
    and pad                ; bits that are 1 now and were 0 before
    sta pad_new
    sep #$20
    .a8
    rts

; ---- vertical blank: the sprites to the picture unit, then count the frame --
; Send sprites, tiles and the map to the video memory here (or with DMA): the
; screen takes data only in the vertical blank or in forced blank.
nmi:
    rep #$30
    .a16
    pha
    phx
    sep #$20
    .a8
    lda RDNMI              ; reading it acknowledges the NMI
    stz OAMADDL
    stz OAMADDH
    stz DMAP0              ; one byte at a time, to $2104
    lda #$04
    sta BBAD0
    ldx #oam
    stx A1T0L
    stz A1B0
    ldx #544
    stx DAS0L
    lda #1
    sta MDMAEN
    inc frame
    rep #$30
    .a16
    plx
    pla
    rti
    .a8

irq:
    rti

;@sprites
; Sprite sheet: the Sprite tab writes its tiles here, bytes after the
; label sprites_chr between ;@tiles and ;@tiles-end (4 bits a pixel, 32 bytes a 8x8
; tile, the native video format, so that a DMA sends them as they are). The sheet is
; 16 tiles wide (128 px). A palette is 16 colours, BGR555.
; The sprites of the screen are the 544 bytes of the OAM (128 sprites).
; The colours below are the sprite palette 0; colour 0 is transparent.
;@tiles  from the Sprite tab: 4bpp, 32 bytes a tile
sprites_chr:
    .res 32,0   ; $000-$000
    .db $3C,$3C,$72,$4E,$E1,$9F,$C1,$BF,$81,$FF,$83,$FF,$46,$7E,$3C,$3C ; $001
    .db $00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00
sprites_chr_end:
;@tiles-end
sprites_pal:
    .dw $0000,$7FFF,$7ECC,$3840,$56B5,$001F,$02BF,$03FF
    .dw $03E0,$01E0,$7FE0,$7C00,$3C00,$7C1F,$1CF7,$0DB1
;@end

;@music
; Music: the Sounds tab will write the songs and the sound effects as entries of the
; BMAP table (the one table of all the resources of the ROM, at the start of a bank):
;   .song "title"
;   .sfx  "jump"
; The SDK routines snd_init, play_song, play_sfx and stop play them through the ports
; $2140-$2143. Until then the sound chip is not started.
;@end

;@map
; Levels: the Map tab will write each level as a file (level1.map) and the assembler
; will include it with
;   .map "level1"
; which adds the level to the BMAP table: tilemap words in screens of 32x32, 16x16
; blocks, the CHR tiles and the palette, a collision byte for each block, and the
; objects (x, y, type, parameter). The routine load_map will load a level by number.
;@end
