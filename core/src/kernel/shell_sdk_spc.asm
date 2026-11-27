; The sound driver of the SNES projects of the Shell SDK, for the SPC700. Original code.
;
; Assembled into shell_sdk_spc_bin.h by core/scripts/spc_asm.py (make test-sdk-snd checks that it is up to date).
; The program sends it with the data of the music block to the sound memory at $0200 (shell_sdk_spc.c builds that
; image) and starts it. The driver keeps time by itself: 60 ticks a second.
;
; Orders: the program writes the number in port 1 (and 2, 3), then the order in port 0: the low 4 bits say what
; (1 song, 2 effect, 3 stop the music, 4 a note: port 1 the note, 2 the instrument + 1, 3 the voice), the high 4
; bits change at each order. The driver writes the order back to port 0 when it has taken it. After the start it
; writes $EE: it listens.

.org $0200

DATA    = $0A00             ; the head of the data (shell_sdk_spc.c)
READY   = $EE
CONTROL = $F1
DSPADR  = $F2
DSPDATA = $F3
PORT0   = $F4
PORT1   = $F5
PORT2   = $F6
PORT3   = $F7
T0DIV   = $FA
T0OUT   = $FD

; ---- the direct page -------------------------------------------------------
m_on    = $10               ; a song plays
m_speed = $11
m_timer = $12
m_order = $13
m_row   = $14
m_steps = $15
m_rowlo = $16               ; the next row (high 0: an empty pattern)
m_rowhi = $17
m_song  = $18               ; 2 bytes
m_len   = $1A
m_loop  = $1B
lastcmd = $1C
konmask = $1D
kofmask = $1E
nonmask = $1F
v_note  = $20               ; the 8 voices of the music
v_ins   = $28               ; the instrument + 1
v_on    = $30
v_vph   = $38
v_arp   = $40
v_key   = $48               ; a note to key on
x_on    = $50               ; an effect has the voice
x_id    = $58
x_timer = $60
x_step  = $68
x_steps = $70
x_speed = $78
x_wave  = $80               ; the sample, or $80 for the noise
x_note  = $88
x_vol   = $90
x_fx    = $98
x_blo   = $A0               ; the slide of the pitch
x_bhi   = $A8
x_ph    = $B0
x_key   = $B8
ptr     = $C0               ; 2 bytes
ip      = $C2               ; the instrument, 2 bytes
t_note  = $C4
t_ins   = $C5
t_y     = $C6
plo     = $C7
phi     = $C8
t_a     = $C9
depth   = $CA
t_phase = $CB
reg     = $CC
ticks   = $CD
insptr  = $D0               ; the tables, copied from the head of the data
patptr  = $D2
songtab = $D4
sfxtab  = $D6
pitchtab = $D8

start:
    clrp
    di
    mov x, #$EF
    mov sp, x
    mov a, #0
    mov x, #0
@clear:
    mov $00+x, a
    inc x
    cmp x, #$F0
    bne @clear
    mov CONTROL, #$30       ; the input ports to 0
    mov x, #0
@head:
    mov a, !DATA+2+x
    mov insptr+x, a
    inc x
    cmp x, #10
    bne @head
    mov y, #0
@dsp:
    mov a, !dspinit+y
    cmp a, #$FF
    beq @ready
    mov DSPADR, a
    inc y
    mov a, !dspinit+y
    mov DSPDATA, a
    inc y
    bra @dsp
@ready:
    mov DSPADR, #$5D        ; the list of the samples
    mov a, !DATA+12
    mov DSPDATA, a
    mov T0DIV, #133         ; 8000 / 133: 60 ticks a second
    mov CONTROL, #$01
    mov PORT0, #READY
main:
    mov a, PORT0
    cmp a, lastcmd
    beq @time
    mov lastcmd, a
    call !command
    mov a, lastcmd
    mov PORT0, a
@time:
    mov a, T0OUT
    beq main
    mov ticks, a
@each:
    call !tick
    dbnz ticks, @each
    jmp !main

command:
    mov a, lastcmd
    and a, #$0F
    cmp a, #1
    bne @not_song
    jmp !cmd_song
@not_song:
    cmp a, #2
    bne @not_effect
    jmp !cmd_effect
@not_effect:
    cmp a, #3
    bne @not_stop
    jmp !halt
@not_stop:
    cmp a, #4
    bne @done
    mov a, PORT3
    and a, #7
    mov x, a
    mov a, PORT1
    mov t_note, a
    mov a, PORT2
    mov t_ins, a
    jmp !cell
@done:
    ret

cmd_song:
    mov a, PORT1
    cmp a, !DATA+0
    bcs @no
    asl a
    mov y, a
    mov a, [songtab]+y
    mov m_song, a
    inc y
    mov a, [songtab]+y
    mov m_song+1, a
    call !halt
    mov y, #0
    mov a, [m_song]+y
    mov m_speed, a
    inc y
    mov a, [m_song]+y
    mov m_loop, a
    inc y
    mov a, [m_song]+y
    mov m_len, a
    mov m_timer, #0
    mov m_order, #0
    call !pattern
    mov m_on, #1
@no:
    ret

; the music stops: its voices are released
halt:
    mov m_on, #0
    mov x, #7
@voice:
    mov a, #0
    mov v_on+x, a
    mov v_ins+x, a
    mov v_key+x, a
    mov a, x_on+x
    bne @next
    mov a, !bits+x
    or a, kofmask
    mov kofmask, a
@next:
    dec x
    bpl @voice
    mov DSPADR, #$5C
    mov a, kofmask
    mov DSPDATA, a
    ret

cmd_effect:
    mov a, PORT1
    cmp a, !DATA+1
    bcs @no
    mov t_a, a
    asl a
    mov y, a
    mov a, [sfxtab]+y
    mov ptr, a
    inc y
    mov a, [sfxtab]+y
    mov ptr+1, a
    mov y, #1
    mov a, [ptr]+y
    and a, #7
    mov x, a                ; the voice of the effect
    mov a, t_a
    mov x_id+x, a
    mov y, #0
    mov a, [ptr]+y
    mov x_speed+x, a
    mov y, #2
    mov a, [ptr]+y
    mov x_wave+x, a
    inc y
    mov a, [ptr]+y
    mov x_steps+x, a
    mov a, #0
    mov x_timer+x, a
    mov x_step+x, a
    mov x_note+x, a
    mov x_fx+x, a
    mov x_blo+x, a
    mov x_bhi+x, a
    mov x_ph+x, a
    mov x_key+x, a
    mov a, #1
    mov x_on+x, a
@no:
    ret

; the pattern of the place m_order of the song
pattern:
    mov a, m_order
    clrc
    adc a, #3
    mov y, a
    mov a, [m_song]+y
    asl a
    mov y, a
    mov a, [patptr]+y
    mov ptr, a
    inc y
    mov a, [patptr]+y
    mov ptr+1, a
    mov m_row, #0
    mov a, ptr+1
    beq @empty
    mov y, #0
    mov a, [ptr]+y
    mov m_steps, a
    mov a, ptr
    clrc
    adc a, #1
    mov m_rowlo, a
    mov a, ptr+1
    adc a, #0
    mov m_rowhi, a
    ret
@empty:
    mov m_rowhi, #0
    mov m_steps, #16
    ret

tick:
    mov a, m_on
    bne @music
    jmp !voices
@music:
    mov a, m_timer
    bne @count
    mov a, m_row
    cmp a, m_steps
    bcc @row
    inc m_order
    mov a, m_order
    cmp a, m_len
    bcc @load
    mov a, m_loop
    cmp a, #$FF
    bne @again
    call !halt
    jmp !voices
@again:
    mov m_order, a
@load:
    call !pattern
@row:
    mov a, m_rowhi
    beq @advance
    mov x, #0
    mov y, #0
@cells:
    mov t_y, y
    mov a, [m_rowlo]+y
    mov t_note, a
    inc y
    mov a, [m_rowlo]+y
    mov t_ins, a
    call !cell
    mov y, t_y
    inc y
    inc y
    inc x
    cmp x, #8
    bne @cells
    mov a, m_rowlo
    clrc
    adc a, #16
    mov m_rowlo, a
    mov a, m_rowhi
    adc a, #0
    mov m_rowhi, a
@advance:
    inc m_row
    mov a, m_speed
    mov m_timer, a
@count:
    dec m_timer
voices:
    mov konmask, #0
    mov x, #0
@voice:
    mov a, x_on+x
    beq @music_voice
    call !effect
    bra @next
@music_voice:
    call !music_voice
@next:
    inc x
    cmp x, #8
    bne @voice
    mov DSPADR, #$3D
    mov a, nonmask
    mov DSPDATA, a
    mov a, konmask
    eor a, #$FF
    and a, kofmask
    mov kofmask, a
    mov DSPADR, #$5C        ; the releases, without the voices keyed now
    mov DSPDATA, a
    mov DSPADR, #$4C
    mov a, konmask
    mov DSPDATA, a
    ret

; X the voice, t_note and t_ins: a step of the music
cell:
    mov a, t_ins
    beq @same
    mov v_ins+x, a
@same:
    mov a, t_note
    beq @done
    cmp a, #1
    beq @off
    mov v_note+x, a
    mov a, #0
    mov v_vph+x, a
    mov v_arp+x, a
    mov a, #1
    mov v_on+x, a
    mov v_key+x, a
@done:
    ret
@off:
    mov a, #0
    mov v_on+x, a
    mov v_key+x, a
    mov a, x_on+x
    bne @done
    mov a, !bits+x
    or a, kofmask
    mov kofmask, a
    ret

music_voice:
    mov a, v_on+x
    bne @on
    ret
@on:
    mov a, v_ins+x
    bne @named
    mov a, #1               ; no instrument named: the first
@named:
    mov t_a, a
    mov a, !DATA+14         ; how many there are
    cmp a, t_a
    bcs @valid
    ret
@valid:
    mov a, t_a
    setc
    sbc a, #1
    asl a
    asl a
    asl a
    clrc
    adc a, insptr
    mov ip, a
    mov a, #0
    adc a, insptr+1
    mov ip+1, a
    mov a, v_note+x
    mov t_note, a
    mov y, #7
    mov a, [ip]+y           ; the arpeggio
    beq @pitch
    mov t_a, a
    mov a, v_arp+x
    beq @pitch
    cmp a, #1
    bne @second
    mov a, t_a
    lsr a
    lsr a
    lsr a
    lsr a
    bra @add
@second:
    mov a, t_a
    and a, #$0F
@add:
    clrc
    adc a, t_note
    mov t_note, a
@pitch:
    call !pitch
    mov y, #6
    mov a, [ip]+y           ; the vibrato
    beq @turn
    mov depth, a
    mov a, v_vph+x
    call !vibrato
@turn:
    inc v_vph+x
    inc v_arp+x
    mov a, v_arp+x
    cmp a, #3
    bcc @key
    mov a, #0
    mov v_arp+x, a
@key:
    mov a, v_key+x
    bne @keyon
    jmp !write_pitch
@keyon:
    mov a, #0
    mov v_key+x, a
    mov y, #1
    mov a, [ip]+y           ; the noise rate, or $FF: a sample
    cmp a, #$FF
    beq @sample
    call !noise_on
    bra @registers
@sample:
    call !noise_off
@registers:
    mov y, #0
    mov a, [ip]+y
    mov y, #4
    call !voice_write       ; the sample
    mov y, #2
    mov a, [ip]+y
    mov y, #5
    call !voice_write       ; the envelope
    mov y, #3
    mov a, [ip]+y
    mov y, #6
    call !voice_write
    mov y, #4
    mov a, [ip]+y
    mov y, #0
    call !voice_write       ; the volumes, left and right
    mov y, #5
    mov a, [ip]+y
    mov y, #1
    call !voice_write
    mov a, !bits+x
    or a, konmask
    mov konmask, a
    jmp !write_pitch

; the effect of the voice X takes the place of the music
effect:
    mov a, x_timer+x
    beq @step
    jmp !@run
@step:
    mov a, x_step+x
    cmp a, x_steps+x
    bcc @read
    mov a, #0               ; the end: released; the next note of the music takes the voice
    mov x_on+x, a
    mov v_on+x, a
    mov a, !bits+x
    or a, kofmask
    mov kofmask, a
    ret
@read:
    mov a, x_id+x
    asl a
    mov y, a
    mov a, [sfxtab]+y
    mov ptr, a
    inc y
    mov a, [sfxtab]+y
    mov ptr+1, a
    mov a, x_step+x
    mov t_a, a
    asl a
    clrc
    adc a, t_a
    adc a, #4
    mov y, a
    mov a, [ptr]+y          ; the note
    beq @volume
    cmp a, #1
    bne @note
    mov x_note+x, a         ; released
    mov a, !bits+x
    or a, kofmask
    mov kofmask, a
    bra @volume
@note:
    mov t_note, a
    mov a, x_note+x
    cmp a, #2
    bcs @new_pitch          ; a note sounds already: only its pitch changes
    mov a, #1
    mov x_key+x, a
@new_pitch:
    mov a, t_note
    mov x_note+x, a
    mov a, #0
    mov x_blo+x, a
    mov x_bhi+x, a
    mov x_ph+x, a
@volume:
    inc y
    mov a, [ptr]+y
    mov x_vol+x, a
    inc y
    mov a, [ptr]+y
    mov x_fx+x, a
    inc x_step+x
    mov a, x_speed+x
    mov x_timer+x, a
@run:
    dec x_timer+x
    mov a, x_note+x
    cmp a, #2
    bcs @sound
    ret
@sound:
    mov t_note, a
    call !pitch
    mov a, x_fx+x
    and a, #$F0
    cmp a, #$10
    beq @up
    cmp a, #$20
    beq @fall
    bra @bent
@up:
    call !slide_step
    mov a, x_blo+x
    clrc
    adc a, t_a
    mov x_blo+x, a
    mov a, x_bhi+x
    adc a, #0
    mov x_bhi+x, a
    bra @bent
@fall:
    call !slide_step
    mov a, x_blo+x
    setc
    sbc a, t_a
    mov x_blo+x, a
    mov a, x_bhi+x
    sbc a, #0
    mov x_bhi+x, a
@bent:
    mov a, plo
    clrc
    adc a, x_blo+x
    mov plo, a
    mov a, phi
    adc a, x_bhi+x
    mov phi, a
    cmp a, #$40             ; the pitch stays in 0..$3FFF
    bcc @ranged
    and a, #$80
    bne @below
    mov phi, #$3F
    mov plo, #$FF
    bra @ranged
@below:
    mov phi, #0
    mov plo, #0
@ranged:
    mov a, x_fx+x
    and a, #$F0
    cmp a, #$30
    bne @shape
    mov a, x_fx+x
    and a, #$0F
    mov depth, a
    mov a, x_ph+x
    call !vibrato
@shape:
    inc x_ph+x
    mov a, x_key+x
    beq @level
    mov a, #0
    mov x_key+x, a
    mov a, x_wave+x
    bmi @keyed
    call !noise_off
    mov a, x_wave+x
    mov y, #4
    call !voice_write
@keyed:
    mov a, #$FF             ; the envelope of an effect: at once, and it holds
    mov y, #5
    call !voice_write
    mov a, #$E0
    mov y, #6
    call !voice_write
    mov a, !bits+x
    or a, konmask
    mov konmask, a
@level:
    mov a, x_wave+x
    bpl @volumes
    mov a, x_note+x         ; the noise: N0..N15, the higher the faster
    setc
    sbc a, #24
    asl a
    inc a
    call !noise_on
@volumes:
    mov a, x_vol+x
    asl a
    asl a
    asl a
    mov y, #0
    call !voice_write
    mov a, x_vol+x
    asl a
    asl a
    asl a
    mov y, #1
    call !voice_write
    jmp !write_pitch

slide_step:                 ; t_a: the amount of the slide in a tick
    mov a, x_fx+x
    and a, #$0F
    asl a
    asl a
    asl a
    asl a
    mov t_a, a
    ret

; A the rate: the noise in the voice X
noise_on:
    and a, #$1F
    or a, #$20              ; the echo writes nothing
    mov DSPADR, #$6C
    mov DSPDATA, a
    mov a, !bits+x
    or a, nonmask
    mov nonmask, a
    ret
noise_off:
    mov a, !bits+x
    eor a, #$FF
    and a, nonmask
    mov nonmask, a
    ret

; t_note -> plo, phi
pitch:
    mov a, t_note
    cmp a, #24
    bcs @low
    mov a, #24
@low:
    cmp a, #108
    bcc @high
    mov a, #107
@high:
    setc
    sbc a, #24
    asl a
    mov y, a
    mov a, [pitchtab]+y
    mov plo, a
    inc y
    mov a, [pitchtab]+y
    mov phi, a
    ret

; A the phase, depth: the pitch goes up and down
vibrato:
    mov t_phase, a
    and a, #7
    mov y, a
    mov a, !wave+y
    mov y, a
    mov a, depth
    mul ya
    asl a
    asl a
    mov t_a, a
    mov a, t_phase
    and a, #8
    bne @down
    mov a, plo
    clrc
    adc a, t_a
    mov plo, a
    mov a, phi
    adc a, #0
    mov phi, a
    ret
@down:
    mov a, plo
    setc
    sbc a, t_a
    mov plo, a
    mov a, phi
    sbc a, #0
    mov phi, a
    ret

write_pitch:
    mov a, plo
    mov y, #2
    call !voice_write
    mov a, phi
    mov y, #3
    jmp !voice_write

; A the value, Y the register 0..7 of the voice X
voice_write:
    push a
    mov a, x
    xcn a
    mov reg, y
    or a, reg
    mov DSPADR, a
    pop a
    mov DSPDATA, a
    ret

bits:
    .db 1,2,4,8,16,32,64,128
wave:
    .db 0,1,2,3,3,3,2,1
dspinit:                    ; register, value; $FF the end
    .db $6C,$20, $0C,127, $1C,127, $2C,0, $3C,0, $0D,0
    .db $2D,0, $3D,0, $4D,0, $5C,0, $4C,0, $FF
