; ============================================================================
; NES game template (NROM, 6502). It builds as it is and shows a blue screen.
; Build: Esc > Build ROM (Ctrl+B).   Pad 1 is read every frame (see "pad").
; Sections marked ;@name ... ;@end are where the Sprite, Sounds and Map tabs
; will read and write their data. Keep the markers.
; ============================================================================

; ---- hardware registers ----------------------------------------------------
PPUCTRL   = $2000
PPUMASK   = $2001
PPUSTATUS = $2002
OAMADDR   = $2003
PPUSCROLL = $2005
PPUADDR   = $2006
PPUDATA   = $2007
OAMDMA    = $4014
APUSTATUS = $4015
JOY1      = $4016
JOY2      = $4017          ; reads pad 2; writes the APU frame counter

; ---- pad bits (pad, pad_old, pad_new): A B Select Start Up Down Left Right -
BTN_A      = $80
BTN_B      = $40
BTN_SELECT = $20
BTN_START  = $10
BTN_UP     = $08
BTN_DOWN   = $04
BTN_LEFT   = $02
BTN_RIGHT  = $01

; ---- variables: zero page $00-$0F, sprites in page 2 -----------------------
pad       = $00            ; buttons held now
pad_old   = $01            ; buttons held in the frame before
pad_new   = $02            ; buttons that went down in this frame
frame     = $03            ; counts the frames (the NMI adds 1)
oam       = $0200          ; 256 bytes of sprites, sent to the PPU every frame

.org $8000

; ---- start -----------------------------------------------------------------
reset:
    sei
    cld
    ldx #$40
    stx JOY2               ; no APU frame IRQ
    ldx #$FF
    txs
    inx                    ; x = 0
    stx PPUCTRL            ; no NMI
    stx PPUMASK            ; picture off
    stx $4010              ; no DMC IRQ
    stx APUSTATUS          ; sound channels off
    bit PPUSTATUS
@vwait1:
    bit PPUSTATUS          ; wait for the first vertical blank
    bpl @vwait1

    lda #0                 ; clear the RAM
    tax
@clear:
    sta $0000,x
    sta $0100,x
    sta $0300,x
    sta $0400,x
    sta $0500,x
    sta $0600,x
    sta $0700,x
    lda #$FF
    sta oam,x              ; sprites off the screen
    lda #0
    inx
    bne @clear

@vwait2:
    bit PPUSTATUS          ; the second vertical blank: the PPU is ready
    bpl @vwait2

    ; ---- palette: the colour behind everything is $11 (blue) ----------------
    lda PPUSTATUS
    lda #$3F
    sta PPUADDR
    lda #$00
    sta PPUADDR
    ldx #0
@pal:
    lda palette,x
    sta PPUDATA
    inx
    cpx #32
    bne @pal

    lda #0                 ; no scroll
    sta PPUSCROLL
    sta PPUSCROLL
    lda #%10000000         ; NMI on
    sta PPUCTRL
    lda #%00011110         ; picture on: background and sprites
    sta PPUMASK

; ---- game loop: once per frame ---------------------------------------------
main:
    lda frame
@wait:
    cmp frame              ; wait until the NMI has added 1
    beq @wait

    jsr read_pad
    jsr update
    jmp main

; ---- your game: called once per frame, after the pad was read --------------
update:
    ; Example: do something when A goes down.
    ;   lda pad_new
    ;   and #BTN_A
    ;   beq @no_a
    ;   ...
    ; @no_a:
    rts

; ---- pad 1 -> pad, pad_old, pad_new ----------------------------------------
read_pad:
    lda pad
    sta pad_old
    lda #1
    sta JOY1               ; strobe on
    sta pad                ; a marker bit: the loop ends when it falls out
    lsr a
    sta JOY1               ; strobe off: the pad shifts its buttons out
@read:
    lda JOY1
    lsr a                  ; bit 0 of the pad -> carry
    rol pad
    bcc @read
    lda pad_old
    eor #$FF
    and pad
    sta pad_new
    rts

; ---- vertical blank: sprites to the PPU, then count the frame --------------
nmi:
    pha
    lda #0
    sta OAMADDR
    lda #>oam
    sta OAMDMA             ; copies page 2 to the sprite memory
    inc frame
    pla
    rti

irq:
    rti

; ---- data ------------------------------------------------------------------
palette:
    ; 4 background palettes, then 4 sprite palettes. The first colour of
    ; each row is the colour behind everything. $11 is blue.
    .byte $11,$30,$21,$01, $11,$30,$21,$01, $11,$30,$21,$01, $11,$30,$21,$01
    .byte $11,$30,$21,$01, $11,$30,$21,$01, $11,$30,$21,$01, $11,$30,$21,$01

;@sprites
; Sprite sheet: the Sprite tab writes its tiles here, a .chr block
; between ;@tiles and ;@tiles-end (8x8 tiles, 2 bits a pixel, 16 bytes a tile,
; 256 tiles in a 4 KB pattern table; the sheet is 16 tiles wide). The build puts the
; block in the CHR ROM (the board must have CHR ROM).
; A sprite lives in "oam", 4 bytes: y, tile, attribute, x.
;   lda #100
;   sta oam+0              ; y
;   lda #1
;   sta oam+1              ; tile
;   lda #0
;   sta oam+2              ; palette 0, no flip
;   lda #120
;   sta oam+3              ; x
;@end

;@music
; Music: the Sounds tab will write the songs here, one block for each song:
;   song_title:  .byte ...   (one stream for pulse 1, pulse 2, triangle and noise)
; Play it from the NMI. Until then the sound chip is off (APUSTATUS = 0).
;@end

;@map
; Levels: the Map tab will write each level as a block of nametable data (32 x 30
; tiles, then 64 bytes of attributes) and a list of objects (x, y, type, parameter):
;   level1_nam:  .incbin "level1.nam"
;   level1_obj:  .byte ...
; Load a screen: write the 960 bytes to $2000 through PPUADDR, picture off.
;@end
