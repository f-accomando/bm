#!/usr/bin/env python3
"""snes_lessons.py - writes the lessons 06 to 14 of bm SNES SDK (bm/snes_sdk/lessons/06-*.txt ... 14-*.txt).

The lessons of the game course grow ONE program. Each lesson file repeats the whole program so far, so a hand edit of
an early lesson would leave the later ones wrong. Change the parts (the dict P) or the texts (the calls of lesson()) here,
run `python3 core/scripts/snes_lessons.py`, then run `make test-snes-learn` (each program assembles and runs in the core).
The hero tiles come from the ASCII art of build(). The text is ASD-STE100 and ASCII only.
"""
import os
# the hero: two poses of 16x16, 4bpp tiles in the order of the sprite (top-left, top-right; bottom-left, bottom-right)
def build(pose):
    half = ["......11","....1122","...12222","..122222","..122222","..122222","..122222","..122222",
            "..122222","...12222","....1122"]
    rows = [h + h[::-1] for h in half]
    # a face that looks right: eye (white with a blue pupil) and a mouth
    def put(r, c, ch):
        rows[r] = rows[r][:c] + ch + rows[r][c+1:]
    put(3,10,"4"); put(3,11,"4"); put(4,10,"4"); put(4,11,"5")
    put(7,9,"3"); put(7,10,"3"); put(7,11,"3")
    if pose == 0:
        rows += ["...13311113311...", "..1333111133311.", "..11111..1111111."][:0]
        rows += ["....1111111111..", "...133111111331.", "...133311113331.", "...111111111111."]
    else:
        rows += ["....1111111111..", "....13311133111.", "...133311133311.", "...111111111111."][:4]
    rows = [r.ljust(16, ".")[:16] for r in rows]
    assert len(rows) == 15 or len(rows) == 16, len(rows)
    while len(rows) < 16: rows.append("." * 16)
    return rows

def tile_bytes(rows, tx, ty):
    px = [[int(rows[ty*8+y][tx*8+x].replace(".", "0")) for x in range(8)] for y in range(8)]
    out = bytearray()
    for pair in (0, 2):
        for y in range(8):
            a = b = 0
            for x in range(8):
                v = px[y][x]
                a = (a << 1) | ((v >> pair) & 1)
                b = (b << 1) | ((v >> (pair + 1)) & 1)
            out += bytes([a, b])
    return bytes(out)

def asm_bytes(label, data, comment):
    lines = [label + ":   " + comment]
    for i in range(0, len(data), 8):
        lines.append("    .byte " + ", ".join("$%02X" % b for b in data[i:i+8]))
    return "\n".join(lines)

def pose_data(pose):
    r = build(pose)
    top = tile_bytes(r, 0, 0) + tile_bytes(r, 1, 0)
    bot = tile_bytes(r, 0, 1) + tile_bytes(r, 1, 1)
    return r, top, bot


OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "bm", "snes_sdk", "lessons")
SECTIONS = ["EQU", "SETUP", "INPUT", "LOGIC", "DRAW", "NMI", "DATA"]
MARK = {
 "EQU":   "; ===== NAMES: new lessons add names here =====",
 "SETUP": "    ; ===== SETUP: new lessons add set-up code here =====",
 "INPUT": "    ; ===== INPUT: read the pad here =====",
 "LOGIC": "    ; ===== LOGIC: the rules of the game go here =====",
 "DRAW":  "    ; ===== DRAW: copy the result to the OAM buffer here =====",
 "NMI":   "    ; ===== NMI: work for the vertical blank goes here =====",
 "DATA":  "; ===== DATA: tiles, colours and tables go here =====",
}

def dedent(s): return s.strip("\n")

# ---- the program: one skeleton with a marked place for each kind of code
def skeleton(parts):
    def sec(name):
        out = [c for (s, n, c) in parts if s == name]
        return "\n".join([dedent(c) for c in out] + [MARK[name]])
    return f"""; ---------------------------------------------------------------
; main.s - the game of the course, built one lesson at a time
; ---------------------------------------------------------------
.lorom                          ; ROM map: 32 KB banks at $8000

{sec("EQU")}

.org $8000                      ; the code starts at the start of bank 0
reset:                          ; the CPU starts here (the reset vector)
    sei                         ; block IRQ while we set up
    clc
    xce                         ; native mode (carry <-> emulation flag)
    rep #$38                    ; A, X, Y 16 bit; decimal mode off
    ldx #$1FFF
    txs                         ; the stack lives at the top of $0000-$1FFF
    sep #$20                    ; A 8 bit (X and Y stay 16 bit)
    lda #$8F
    sta INIDISP                 ; forced blank: screen off, safe to write to the PPU
    stz NMITIMEN                ; no NMI and no pad reading yet
{sec("SETUP")}
    lda #$0F
    sta INIDISP                 ; screen on, full brightness
    lda #$81
    sta NMITIMEN                ; NMI on (bit 7) and pad auto-read on (bit 0)

main_loop:                      ; this loop runs once for each frame
    wai                         ; sleep until an interrupt (the NMI)
    lda vblank
    beq main_loop               ; not the NMI yet: sleep again
    stz vblank                  ; we saw it
{sec("INPUT")}
{sec("LOGIC")}
{sec("DRAW")}
    jmp main_loop               ; next frame (jmp reaches farther than bra)

nmi:                            ; the console calls this at the start of each vertical blank
    rep #$30
    pha
    phx
    phy                         ; save A, X, Y (all 16 bit)
    sep #$20
    lda RDNMI                   ; tell the console that we answered
    lda #1
    sta vblank                  ; tell the main loop: a new frame
    inc frames
{sec("NMI")}
    rep #$30
    ply
    plx
    pla                         ; restore A, X, Y
    rti                         ; back to the main loop

{sec("DATA")}
"""

def compose(parts_upto, drops):
    parts = [p for p in parts_upto if p[1] not in drops]
    return skeleton(parts)

# ------------------------------------------------------------------ the parts of each lesson
P = {}   # lesson number -> list of (section, name, code)
DROPS = {}  # lesson number -> names removed

P[6] = [
 ("EQU", "eq6", """
INIDISP  = $2100                ; screen on/off and brightness
NMITIMEN = $4200                ; NMI enable and pad auto-read enable
RDNMI    = $4210                ; reading it tells the console "I got the NMI"
vblank   = $10                  ; RAM: the NMI sets it to 1 at each frame
frames   = $11                  ; RAM: the NMI counts the frames here
"""),
]

P[7] = [
 ("EQU", "eq7", """
WMDATA   = $2180                ; the work RAM port (DMA can write to it)
WMADDL   = $2181                ; where in the work RAM the port writes (3 bytes)
DMAP0    = $4300                ; DMA channel 0: control
BBAD0    = $4301                ; DMA channel 0: target register ($21xx)
A1T0L    = $4302                ; DMA channel 0: source address (16 bit)
A1B0     = $4304                ; DMA channel 0: source bank
DAS0L    = $4305                ; DMA channel 0: number of bytes
MDMAEN   = $420B                ; write a bit here to start a channel
"""),
 ("SETUP", "clean", """
    ; -- 1. clear the PPU registers $2101-$2133 (they hold random values at power on)
    sep #$10                    ; X and Y 8 bit for this loop
    ldx #$00
@ppu_clear:
    stz $2101,x                 ; write 0 to $2101 + X
    inx
    cpx #$33                    ; stop after $2133
    bne @ppu_clear
    rep #$10                    ; X and Y 16 bit again
    ; -- 2. clear the work RAM $0000-$1FFF with one DMA transfer
    stz WMADDL
    stz WMADDL+1
    stz WMADDL+2                ; the port starts at WRAM address $000000
    lda #$08                    ; DMA control: mode 0 (one register), fixed source address
    sta DMAP0
    lda #$80                    ; target register $2180 (WMDATA)
    sta BBAD0
    rep #$20
    lda #zero_byte              ; the source: one byte that is 0
    sta A1T0L
    lda #$2000                  ; 8 KB
    sta DAS0L
    sep #$20
    lda #^zero_byte
    sta A1B0
    lda #$01
    sta MDMAEN                  ; start channel 0
"""),
 ("DATA", "zero", """
zero_byte:
    .byte 0                     ; the source of the RAM clear
"""),
]

P[8] = [
 ("EQU", "eq8", """
CGADD    = $2121                ; colour number for the next CGDATA write
CGDATA   = $2122                ; colour data (two writes: low byte, then high byte)
BACKDROP = $2400                ; dark blue; a colour is BGR555: 0bbbbbgg gggrrrrr
"""),
 ("SETUP", "colours", """
    ; -- the backdrop colour is colour 0 of the palette (CGRAM)
    stz CGADD                   ; colour number 0
    lda #<BACKDROP
    sta CGDATA                  ; low byte
    lda #>BACKDROP
    sta CGDATA                  ; high byte
    ; -- the 16 colours of the sprites: colours 128-143 (sprites use the upper half of CGRAM)
    lda #128
    sta CGADD
    stz DMAP0                   ; DMA mode 0: bytes go one by one to one register
    lda #$22
    sta BBAD0                   ; target $2122 (CGDATA)
    rep #$20
    lda #hero_palette
    sta A1T0L
    lda #32                     ; 16 colours of 2 bytes
    sta DAS0L
    sep #$20
    lda #^hero_palette
    sta A1B0
    lda #$01
    sta MDMAEN
"""),
 ("DATA", "pal", """
hero_palette:                   ; colour 0 of a sprite palette is always transparent
    .word $0000                 ; 0: transparent
    .word $1863                 ; 1: dark outline
    .word $03FF                 ; 2: yellow
    .word $109F                 ; 3: red
    .word $7FFF                 ; 4: white
    .word $68C2                 ; 5: blue
    .res 20, 0                  ; 6-15: not used
"""),
]

r0, top0, bot0 = pose_data(0)
r1, top1, bot1 = pose_data(1)
def first_nz(b): 
    for i, x in enumerate(b):
        if x: return i, x
P[9] = [
 ("EQU", "eq9", """
VMAIN    = $2115                ; how the video address grows after a write
VMADDL   = $2116                ; video RAM address (16 bit, in words)
"""),
 ("SETUP", "tiles", """
    ; -- the hero is 16x16 pixels = 4 tiles: top-left, top-right, bottom-left, bottom-right.
    ;    A sprite uses tile N, N+1 (top row) and N+16, N+17 (bottom row).
    lda #$80                    ; VMAIN: the address grows after the HIGH byte is written
    sta VMAIN
    lda #$01                    ; DMA mode 1: two registers, $2118 then $2119
    sta DMAP0
    lda #$18                    ; target $2118 (VMDATAL)
    sta BBAD0
    ; top row: tiles 0 and 1 (2 x 32 bytes) go to video word address 0
    rep #$20
    stz VMADDL
    lda #hero_top
    sta A1T0L
    lda #64
    sta DAS0L
    sep #$20
    lda #^hero_top
    sta A1B0
    lda #$01
    sta MDMAEN
    ; bottom row: tiles 16 and 17. One tile is 16 words, so tile 16 is at word 16 * 16 = $0100
    rep #$20
    lda #$0100
    sta VMADDL
    lda #hero_bottom
    sta A1T0L
    lda #64
    sta DAS0L
    sep #$20
    lda #$01
    sta MDMAEN
"""),
 ("DATA", "tiles", asm_bytes("hero_top", top0, "; tiles 0 and 1: 4 bits a pixel, 32 bytes a tile") + "\n" + asm_bytes("hero_bottom", bot0, "; tiles 16 and 17")),
]

P[10] = [
 ("EQU", "eq10", """
OBSEL    = $2101                ; sprite sizes and where the sprite tiles are in video RAM
TM       = $212C                ; main screen: which layers show
OAMADDL  = $2102                ; OAM address (low)
OAMADDH  = $2103                ; OAM address (high)
oam      = $0200                ; RAM: the OAM buffer, 512 bytes (4 bytes for each of 128 sprites)
oamhi    = $0400                ; RAM: the OAM high table, 32 bytes (2 bits for each sprite)
px       = $14                  ; RAM: X of the hero
py       = $15                  ; RAM: Y of the hero
"""),
 ("SETUP", "sprite", """
    ; -- the OAM buffer: hide all 128 sprites. Y = $F0 is below the 224 lines of the picture.
    ldx #$0000
@hide_sprites:
    stz oam,x                   ; X = 0
    lda #$F0
    sta oam+1,x                 ; Y = $F0
    stz oam+2,x                 ; tile 0
    stz oam+3,x                 ; attributes 0
    inx
    inx
    inx
    inx                         ; the next sprite (4 bytes)
    cpx #512
    bne @hide_sprites
    ; -- the high table: bit 0 of each pair is X bit 8, bit 1 is the size (0 small, 1 large)
    ldx #$0000
@clear_high:
    stz oamhi,x
    inx
    cpx #32
    bne @clear_high
    ; -- sprite 0 is the hero: large (16x16), palette 0, priority 3
    lda #%00000010
    sta oamhi                   ; sprite 0: large size
    lda #$30                    ; attributes: priority 3 (bits 4-5), palette 0, no flip
    sta oam+3
    lda #120
    sta px                      ; start position
    lda #100
    sta py
    stz OBSEL                   ; sizes 8x8 and 16x16; sprite tiles at video word 0
    lda #$10
    sta TM                      ; main screen: sprites (bit 4)
"""),
 ("DRAW", "draw10", """
    lda px
    sta oam                     ; sprite 0: X
    lda py
    sta oam+1                   ; sprite 0: Y
"""),
 ("NMI", "oamdma", """
    ; -- copy the OAM buffer (544 bytes) to the OAM; the PPU is not drawing now
    stz OAMADDL
    stz OAMADDH                 ; OAM address 0
    stz DMAP0                   ; DMA mode 0
    lda #$04
    sta BBAD0                   ; target $2104 (OAMDATA)
    rep #$20
    lda #oam
    sta A1T0L
    lda #544                    ; 512 + 32 bytes
    sta DAS0L
    sep #$20
    stz A1B0                    ; the buffer is in bank 0
    lda #$01
    sta MDMAEN
"""),
]

P[11] = [
 ("EQU", "eq11", """
HVBJOY   = $4212                ; bit 0 is 1 while the console reads the pads
JOY1L    = $4218                ; pad 1, 16 bits: B Y Select Start Up Down Left Right A X L R
pad      = $12                  ; RAM: the pad of this frame (16 bit, $12 low and $13 high)
"""),
 ("INPUT", "pad", """
    ; -- wait until the console has finished reading the pad, then copy it
@wait_pad:
    lda HVBJOY
    and #$01
    bne @wait_pad
    rep #$20
    lda JOY1L                   ; high byte (bit 15-8): B Y Select Start Up Down Left Right
    sta pad                     ;  low byte (bit 7-4):  A X L R
    sep #$20
"""),
]

P[12] = [
 ("LOGIC", "horizontal", """
    ; -- Right: X + 2 (the high byte of the pad has Right in bit 0)
    lda pad+1
    and #$01
    beq @no_right
    lda px
    clc
    adc #2
    cmp #241                    ; the hero is 16 wide: X must stay under 241
    bcs @no_right
    sta px
@no_right:
    ; -- Left: X - 2 (Left is bit 1)
    lda pad+1
    and #$02
    beq @no_left
    lda px
    sec
    sbc #2
    bcc @no_left                ; the carry is clear: X went below 0
    sta px
@no_left:
"""),
 ("LOGIC", "vertical", """
    ; -- Down: Y + 2 (bit 2), Up: Y - 2 (bit 3)
    lda pad+1
    and #$04
    beq @no_down
    lda py
    clc
    adc #2
    cmp #209                    ; 224 lines - 16 high + 1
    bcs @no_down
    sta py
@no_down:
    lda pad+1
    and #$08
    beq @no_up
    lda py
    sec
    sbc #2
    bcc @no_up
    sta py
@no_up:
"""),
]

P[13] = [
 ("EQU", "eq13", """
facing   = $16                  ; RAM: $00 looks right, $40 looks left (the flip X bit)
anim     = $17                  ; RAM: counts frames while the hero walks
"""),
 ("SETUP", "pose2", """
    ; -- the second pose: tiles 2, 3 (word 32) and 18, 19 (word 288)
    rep #$20
    lda #$0020
    sta VMADDL
    lda #hero2_top
    sta A1T0L
    lda #64
    sta DAS0L
    sep #$20
    lda #$01
    sta MDMAEN
    rep #$20
    lda #$0120
    sta VMADDL
    lda #hero2_bottom
    sta A1T0L
    lda #64
    sta DAS0L
    sep #$20
    lda #$01
    sta MDMAEN
"""),
 ("LOGIC", "face", """
    ; -- which way does the hero look? Right: no flip. Left: flip X.
    lda pad+1
    and #$01
    beq @not_right
    stz facing
@not_right:
    lda pad+1
    and #$02
    beq @not_left
    lda #$40                    ; attribute bit 6 = flip X
    sta facing
@not_left:
    ; -- the walk animation: count frames while Left or Right is held
    lda pad+1
    and #$03
    bne @walking
    stz anim                    ; standing: pose 0
    bra @anim_done
@walking:
    inc anim
@anim_done:
"""),
 ("DRAW", "draw13", """
    lda anim
    lsr
    lsr
    lsr                         ; anim / 8: changes every 8 frames
    and #$01                    ; pose 0 or 1
    asl                         ; pose 1 uses tile 2
    sta oam+2                   ; sprite 0: tile
    lda #$30                    ; priority 3, palette 0
    ora facing                  ; plus the flip bit
    sta oam+3                   ; sprite 0: attributes
"""),
 ("DATA", "tiles2", asm_bytes("hero2_top", top1, "; pose 2, tiles 2 and 3") + "\n" + asm_bytes("hero2_bottom", bot1, "; pose 2, tiles 18 and 19")),
]
DROPS[14] = ["vertical"]
P[14] = [
 ("EQU", "eq14", """
pyf      = $18                  ; RAM: Y of the hero with a fraction (16 bit: 8.8 fixed point)
vy       = $1A                  ; RAM: vertical speed (16 bit, signed, 8.8 fixed point)
ground   = $1C                  ; RAM: 1 when the hero stands on the floor (16 bit)
FLOOR    = $B000                ; the floor: Y = $B0 (176)
"""),
 ("SETUP", "jumpinit", """
    rep #$20
    lda #FLOOR
    sta pyf                     ; the hero starts on the floor
    stz vy
    lda #1
    sta ground
    sep #$20
"""),
 ("LOGIC", "jump", """
    ; -- jump: A or B on the floor gives an upward speed
    rep #$20                    ; 16 bit A for the fixed point numbers
    lda pad                     ; B is bit 15, A is bit 7
    and #$8080
    beq @no_jump
    lda ground
    beq @no_jump                ; in the air: no second jump
    lda #$FB80                  ; -4.5 pixels a frame (two's complement of $0480)
    sta vy
    stz ground
@no_jump:
    ; -- gravity: the speed grows by 0.25 pixel each frame, the position follows the speed
    lda vy
    clc
    adc #$0040
    sta vy
    lda pyf
    clc
    adc vy
    sta pyf
    cmp #FLOOR
    bcc @in_air                 ; above the floor
    lda #FLOOR                  ; on or under the floor: stand on it
    sta pyf
    stz vy
    lda #1
    sta ground
@in_air:
    sep #$20
    lda pyf+1                   ; the whole pixels are the high byte
    sta py
"""),
]

# ------------------------------------------------------------------ the lesson texts
LESSONS = {}

def lesson(n, slug, title, goal, links, intro, add_text, after, tries, learned, run_opts, add_blocks=None):
    LESSONS[n] = dict(slug=slug, title=title, goal=goal, links=links, intro=intro, add_text=add_text,
                      after=after, tries=tries, learned=learned, run=run_opts, add_blocks=add_blocks)

SMW_RAM = "SMW RAM map (SMW Central) | https://www.smwcentral.net/?p=nmap&m=smwram"
SMW_ROM = "SMW ROM map (SMW Central) | https://www.smwcentral.net/?p=nmap&m=smwrom"
WIKI = "SNESdev Wiki | https://snes.nesdev.org/wiki/"

lesson(6, "06-start", "Start the cartridge without errors",
 "Make a cartridge that starts, runs one frame after another, and never crashes.",
 [WIKI + "Init_code", SMW_ROM, "Assembly for the SNES (ersanio) | https://ersanio.gitbook.io/assembly-for-the-snes/"],
 """
This is the first step of a small game. In each next lesson you add one part to the same program. At the end the program has a hero that you move and make jump. The code has a comment on each line.

A cartridge that does not start has almost always one of these faults:

There is no `reset` label. The builder cannot write the reset vector. The error says: reset is missing.

The CPU stays in emulation mode. Always run CLC and XCE first.

A and X have the wrong width. Use REP and SEP and say what you want.

The NMI routine does not read RDNMI ($4210), or it ends without RTI. The console calls the NMI again or the CPU runs into data.

Code goes into $FFC0-$FFFF. That area is the header. The builder writes it.

Commercial games start the same way. Super Mario World runs a reset routine that turns off the screen, sets the stack, clears the registers and the RAM, and then turns on the NMI. Open the SMW ROM map (link below) and look at the reset vector to see it.
""",
 """
The program below is the whole skeleton. It has three parts.

The reset routine runs once. It ends with the screen on and the NMI on.

The main loop runs once for each frame. It sleeps with WAI. When the NMI sets the flag `vblank`, the loop does the work of one frame. Later lessons add their code at the marks INPUT, LOGIC and DRAW.

The NMI routine runs at the start of each vertical blank. It saves the registers, sets the flag, counts the frames and restores the registers.

The marks are comment lines with the word ===== in them. The next lessons tell you the mark to use.
""",
 None,
 ["Change the value $0F in the last INIDISP write to $05. The picture gets darker. The low 4 bits are the brightness.",
  "Remove the line `lda RDNMI` and run again. Does the program still run?"],
 "A cartridge needs a reset label, native mode, a stack, forced blank and an NMI that answers. The main loop waits for the flag of the NMI.",
 dict(expect="w11>03,w10=00", frames=20, pad=None))

lesson(7, "07-clean", "A clean start",
 "Clear the PPU registers and the work RAM so that the game never sees random values.",
 [WIKI + "Init_code", SMW_ROM],
 """
When the console starts, the PPU registers and the RAM hold random values. A game that reads a RAM cell before it writes it can behave in a different way on each start. So every game begins with a clean-up. Super Mario World does this, too.

This lesson does two things. First, a loop writes 0 to the PPU registers $2101 to $2133. Second, one DMA transfer clears the work RAM from $0000 to $1FFF.

The DMA transfer uses a trick. The control byte $08 sets bit 3: the source address does not move. So the DMA reads the same zero byte 8192 times and writes each copy to the work RAM port $2180. The port moves its own address after each write.
""",
 """
You add three things. The names go at the mark NAMES. The clean-up code goes at the mark SETUP. The zero byte goes at the mark DATA.
""",
 None,
 ["Put a value into $0020 with `lda #$55` and `sta $20` before the clean-up. Run to the end. What is in $0020?",
  "Use the Run tab (F5) and look at the work RAM with the hex view."],
 "Clear the registers and the RAM at the start. A DMA with a fixed source is the fast way to fill memory.",
 dict(expect="w10=00,w11>03", frames=20, pad=None))

lesson(8, "08-colours", "Colours",
 "Set the backdrop colour and load a palette for the sprites.",
 [WIKI + "CGRAM", SMW_RAM],
 """
The SNES has 256 colours in a memory called CGRAM. A colour has 15 bits. The bits are 0bbbbbgg gggrrrrr: 5 bits of blue, green and red.

Colour 0 is the backdrop. It shows where no layer draws. The sprites use the upper half of CGRAM: colours 128 to 255. Each sprite chooses one of 8 palettes of 16 colours. Colour 0 of a sprite palette is transparent.

You write a colour to CGDATA ($2122) in two steps: low byte, then high byte. CGADD ($2121) is the colour number. The address grows after each pair. So you can write many colours in a row. A DMA with mode 0 does that quickly.
""",
 """
The names go at NAMES. The code goes at SETUP, after the clean-up. The palette goes at DATA.
""",
 None,
 ["Change BACKDROP to $0016. What colour is it? (Blue is the top 5 bits, red the low 5.)",
  "Change colour 2 of the palette to $001F (red) and look at the hero in the next lessons."],
 "A colour is 15 bits. Sprites use CGRAM colours 128-255. A DMA fills the palette.",
 dict(expect="cg0=2400,cg81=1863,cg82=03ff", frames=10, pad=None))

lesson(9, "09-tiles", "Graphics in video RAM",
 "Copy the tiles of the hero to the video RAM with DMA.",
 [WIKI + "Tiles", WIKI + "Tiles"],
 """
The graphics live in the video RAM (VRAM), 64 KB that only the PPU reads. You cannot write to it when the screen draws, so you do it in forced blank, as we do in the SETUP code.

A tile is 8x8 pixels. With 4 bits a pixel a tile is 32 bytes. The bits of a pixel are in four planes, not in one nibble. The Sprites tab does this conversion for you and exports a .chr file.

A sprite of 16x16 pixels is 4 tiles: N, N+1 on the top row and N+16, N+17 on the bottom row. The tiles are 16 on a row because the PPU thinks of the sprite sheet as 16 tiles wide. So we send the top row to tile 0 and the bottom row to tile 16.

The VRAM address is in words. One 4 bit tile is 16 words. Tile 16 is at word 16 x 16 = 256 = $0100.

Super Mario World keeps its graphics in compressed form in the ROM and unpacks them to the VRAM. We use plain data.
""",
 """
The names go at NAMES. The upload code goes at SETUP. The tile bytes go at DATA.
""",
 None,
 ["Draw your own hero in the Sprites tab, export the .chr, and replace the bytes. Use `.incbin` to read the file.",
  "Change `lda #$80` to `lda #$00` for VMAIN. The tiles look wrong. Why? (The address grows after the low byte.)"],
 "Tiles are 32 bytes with 4 bits a pixel. A 16x16 sprite uses tiles N, N+1, N+16, N+17. DMA copies them to VRAM in forced blank.",
 dict(expect="v%X=%02X,v%X=%02X" % (first_nz(top0)[0], first_nz(top0)[1], 512 + first_nz(bot0)[0], first_nz(bot0)[1]), frames=10, pad=None))

lesson(10, "10-sprite", "Show a sprite",
 "Put the hero on the screen with the OAM buffer.",
 [WIKI + "OAM", SMW_RAM],
 """
The OAM holds the data of 128 sprites: 4 bytes each (X, Y, tile, attributes) in a table of 512 bytes, then a table of 32 bytes with 2 bits for each sprite (bit 8 of X and the size).

The program does not write to the OAM when it likes. It keeps a copy in the RAM, the OAM buffer, at $0200. The NMI copies the buffer to the OAM with a DMA in each vertical blank. Super Mario World does the same: its sprite code writes to a buffer at $0200 and a routine in the NMI sends the buffer.

All 128 sprites exist. A sprite you do not use must be hidden. We set its Y to $F0: that is under the bottom of the picture.

The attribute byte is vhoopppc: v flip Y, h flip X, oo priority, ppp palette, c bit 8 of the tile.
""",
 """
The names go at NAMES. The set-up goes at SETUP. The copy of the position goes at DRAW. The DMA goes at NMI.
""",
 None,
 ["Change `lda #120` to `lda #200`. The hero starts at the right.",
  "Change the priority in `lda #$30` to $00. Priority 0 is behind the background layers."],
 "Keep a copy of the OAM in RAM and send it with DMA in the NMI. Hide the sprites you do not use.",
 dict(expect="o0=78,o1=64,pic=1", frames=10, pad=None))

lesson(11, "11-pad", "Read the joypad",
 "Read the buttons of the pad in each frame and keep them in the RAM.",
 [WIKI + "Controller_reading", SMW_RAM],
 """
Bit 0 of NMITIMEN ($4200) turns on the auto-read. In each vertical blank the console reads pad 1 and puts the 16 buttons in JOY1L ($4218) and JOY1H ($4219). The read takes about 3 lines. So the program waits until bit 0 of HVBJOY ($4212) is 0.

The 16 bits are, from bit 15 to bit 0: B, Y, Select, Start, Up, Down, Left, Right, A, X, L, R and 4 zero bits. In the high byte $13 the bits are: Right 0, Left 1, Down 2, Up 3, Start 4, Select 5, Y 6, B 7.

The program copies the pad to the RAM at $0012 and uses the copy in the whole frame. Super Mario World keeps copies of the pad in RAM, too, and has one cell for the buttons that are pressed in this frame only. See the RAM map.

Nothing changes on the screen in this lesson. Use the Run tab and the hex view of the RAM to see the value at $0012.
""",
 """
The names go at NAMES. The code goes at INPUT.
""",
 None,
 ["Press Right in the Run tab (the arrow key). Look at $0013: it changes to $01.",
  "Hold A. Which byte changes? (A is bit 7 of the low byte.)"],
 "Wait for the auto-read, then copy JOY1L to a variable. The high byte has the directions.",
 dict(expect="w13=01", frames=20, pad="0100"))

lesson(12, "12-move", "Move the hero",
 "Move the hero with the d-pad and keep it on the screen.",
 [WIKI + "OAM", SMW_RAM],
 """
Now the joypad moves the hero. Moving a sprite means a new X and Y in each frame.

The pad changes the position. For each direction the code tests a bit of the high byte of the pad, then changes X or Y by 2 pixels.

The limits need care. X is a byte, so 0 minus 2 would wrap to 254. We test the carry flag after SBC: if the subtract borrowed, the result is below 0 and we do not store it. For the right edge, CMP tells us when X + 2 is 241 or more. The hero is 16 pixels wide, so 240 is the last good position.

The position lives in the variables px and py. The DRAW code from lesson 10 copies them to the OAM buffer. So the logic and the screen are separate. This is the pattern that games use.
""",
 """
Both parts go at LOGIC. The marks name the parts: horizontal and vertical.
""",
 None,
 ["Change the speed 2 to 1 (all four places). Change it to 4.",
  "Add a wall: do not let X go under 32."],
 "Test a bit of the pad, change the variable, check the limits with the carry. The DRAW code shows the result.",
 dict(expect="w14>78,o0>78", frames=40, pad="0100"))

lesson(13, "13-walk", "Face the way you walk",
 "Flip the hero when it goes left and play a walk animation with two poses.",
 [WIKI + "OAM", SMW_RAM],
 """
The hero looks right. When it goes left we set the flip X bit (bit 6) in the attribute byte. No second picture is needed.

The walk animation has two poses. The second pose is in tiles 2, 3, 18 and 19. The variable `anim` counts frames while Left or Right is held. We divide it by 8 with three LSR. The low bit says which pose to show. A 16x16 sprite uses tile 0 for pose 0 and tile 2 for pose 1.

Super Mario World does the same with a frame counter and a table of tile numbers for each pose of Mario.
""",
 """
The names go at NAMES. The upload of the second pose goes at SETUP. The rules go at LOGIC. The tile and the attributes go at DRAW. The bytes go at DATA.
""",
 None,
 ["Change the 8 (three LSR) to 4 (two LSR). The hero walks faster.",
  "Add a third pose in tiles 4, 5, 20 and 21."],
 "Flip X is one bit of the attributes. An animation is a counter that picks a tile.",
 dict(expect="o3=70,o2=02", frames=46, pad="0200"))

lesson(14, "14-jump", "Jump with gravity",
 "Make the hero jump with a speed and a gravity, using fixed point numbers.",
 [WIKI.rstrip("/"), SMW_RAM],
 """
A jump needs a speed that changes. Whole pixels are too coarse. We use 8.8 fixed point: a 16 bit number where the high byte is the whole pixels and the low byte is 1/256 of a pixel.

Each frame the gravity adds $0040 (a quarter pixel) to the vertical speed, and the speed is added to the position. The jump gives the speed -4.5 pixels a frame. A negative number is the two's complement: $FB80. The hero goes up, slows down, stops, and falls. When the position reaches the floor the speed is set to 0 and the hero stands again.

Super Mario World also keeps a speed and a position with a fraction for Mario, and it adds the gravity in each frame. The RAM map names these cells.

Now Up and Down do not move the hero. So you remove the part named vertical from lesson 12.
""",
 """
Delete the part named vertical in LOGIC. Then add the names, the start values and the jump code.
""",
 None,
 ["Make the jump higher: use $FA00 for the speed.",
  "Make the gravity stronger: $0060. The jump feels heavier.",
  "Let the hero jump only when the button is pressed this frame, not held. Keep the old pad in a variable."],
 "Fixed point gives small steps. Speed plus gravity plus a floor is a whole jump.",
 dict(expect="w1c=00", frames=6, pad="0080"))

def parts_upto(n):
    out, drops = [], set()
    for k in sorted(P):
        if k <= n:
            out += P[k]
            drops |= set(DROPS.get(k, []))
    return out, drops

def write_lesson(n):
    L = LESSONS[n]
    parts, drops = parts_upto(n)
    program = compose(parts, drops)
    # the part of this lesson alone
    chunks = []
    for (sec, name, code) in P[n]:
        head = {"EQU": "NAMES", "SETUP": "SETUP", "INPUT": "INPUT", "LOGIC": "LOGIC", "DRAW": "DRAW", "NMI": "NMI", "DATA": "DATA"}[sec]
        chunks.append(f"; >>> add at the mark {head} (part: {name})\n" + dedent(code))
    if n in DROPS:
        chunks.insert(0, "; >>> first delete these parts from LOGIC: " + ", ".join(DROPS[n]))
    part_text = "\n\n".join(chunks) + "\n"
    ro = L["run"]
    fence = "```asm expect=%s frames=%d" % (ro["expect"], ro["frames"]) + (" pad=%s" % ro["pad"] if ro["pad"] else "")
    head = ["title: " + L["title"], "goal: " + L["goal"]]
    for l in L["links"]: head.append("link: " + l)
    head.append("---")
    body = L["intro"].strip("\n") + "\n\n## What this lesson adds\n" + L["add_text"].strip("\n") + "\n\n"
    if n == 6:
        body += "## The program\nThis program runs by itself. Use Open (o) to start your game file from it. Use Run (r) to try it.\n\n"
    else:
        body += "## The part to add\nUse Insert (i) for the text below. Then put each piece at its mark. Or open the whole program below.\n\n"
        body += "```text\n" + part_text + "```\n\n"
        body += "## The whole program so far\nThis program runs by itself. Use Run (r) to try it, Open (o) to start a new file from it.\n\n"
    body += fence + "\n" + program + "```\n\n"
    for t in L["tries"]: body += "try: " + t + "\n"
    body += "\n## What you learned\n" + L["learned"] + "\n"
    # lesson 6 and 7: wrap the explanation so that no line is long; the parser joins lines
    text = "\n".join(head) + "\n" + body
    open(os.path.join(OUT, L["slug"] + ".txt"), "w").write(text)

if __name__ == "__main__":
    for n in sorted(LESSONS): write_lesson(n)
    print("ok", sorted(LESSONS))
