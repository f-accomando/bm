; ============================================================================
; SNES game template (LoROM, 65C816). It builds as it is and shows a blue screen.
; Build: Esc > Build ROM (Ctrl+B).   Pad 1 is read every frame (see "pad").
; Sections marked ;@name ... ;@end are where the Sprite, Sounds and Map tabs
; will read and write their data. Keep the markers.
; ============================================================================

; ---- hardware registers ----------------------------------------------------
INIDISP   = $2100          ; screen on/off and brightness
BGMODE    = $2105
CGADD     = $2121
CGDATA    = $2122
NMITIMEN  = $4200          ; NMI and automatic pad reading
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

; ---- variables: direct page $00-$0F ----------------------------------------
pad       = $00            ; word: buttons held now
pad_old   = $02            ; word: buttons held in the frame before
pad_new   = $04            ; word: buttons that went down in this frame
frame     = $06            ; byte: counts the frames (the NMI adds 1)

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
    stz $420B              ; no DMA
    stz $420C              ; no HDMA
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
    ; Example: do something when A goes down.
    ;   rep #$20
    ;   .a16
    ;   lda pad_new
    ;   and #BTN_A
    ;   beq @no_a
    ;   ...
    ; @no_a:
    ;   sep #$20
    ;   .a8
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

; ---- vertical blank: count the frame ---------------------------------------
; Send sprites, tiles and the map to the video memory here (or with DMA): the
; screen takes data only in the vertical blank or in forced blank.
nmi:
    pha
    lda RDNMI              ; reading it acknowledges the NMI
    inc frame
    pla
    rti

irq:
    rti

;@sprites
; Sprite sheet: the Sprite tab will write sprites.chr (4 bits a pixel, 32 bytes a 8x8
; tile) and sprites.pal (16 colours a palette, BGR555). The sheet is 16 tiles wide
; (128 px). The data goes in a bank of its own, in the native video format, so that a
; DMA sends it as it is:
;   .org $818000
;   sprites_chr: .incbin "sprites.chr"
;   sprites_pal: .incbin "sprites.pal"
; The sprites of the screen are the 544 bytes of the OAM (128 sprites).
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
