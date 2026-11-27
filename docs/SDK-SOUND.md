# Shell SDK: sound of the NES and SNES projects

The Sounds tab (F5) of the Shell SDK writes the music of a NES project. A SNES project has the same block, which you
write by hand for now ([The SNES](#the-snes)). The code holds the data: the block between
`;@music` and `;@end` (assembly) or `//@music` and `//@end` (bm C). The tab reads the block and writes it again at
each change. The tab keeps no other copy.

## Use

| Step | Assembly | bm C |
|---|---|---|
| Start the sound | `jsr snd_init` before the NMI is on | done by the runtime |
| Each frame | `jsr snd_frame` in `nmi` | done by the runtime |
| A song | `lda #song_title` and `jsr play_song` | `play_song(song_title);` |
| An effect | `lda #sfx_jump` and `jsr play_sfx` | `play_sfx(sfx_jump);` |
| Silence the music | `jsr stop_music` | `stop_music();` |

- The routines keep A, X and Y.
- `play_song`, `play_sfx` and `stop_music` leave a request. `snd_frame` does the work.
- An effect takes one channel for its length. The music has the channel again after it.
- The build adds the driver when the block has data or the program calls the driver.
- The driver uses the zero page `$FC-$FF` and the RAM `$0700-$077F`. Keep your variables out of these places in
  assembly. bm C keeps out of them by itself.
- The periods are the NTSC ones.

## The block

The tab writes the lists. You can also write them by hand: labels and `.byte` lines in assembly,
`const byte name[] = { }` in bm C. The tab writes the block again in its own layout and drops your comments in it.

| List name | Values, then `END` |
|---|---|
| any other name (an instrument) | `PULSE`, `TRIANGLE` or `NOISE`; wave (`DUTY12` to `DUTY75`; noise: 0 to 15, `+SHORT`); vibrato 0 to 15; arpeggio (`$47`: 4 and 7 semitones); volumes 0 to 15, one a frame, with `LOOP` and `RELEASE` |
| `pattern00` to `pattern31` | a step is 7 values: note and instrument for pulse 1, pulse 2 and triangle, then the noise instrument |
| `sfx_NAME` | frames a step; `PU1`, `PU2`, `TRI` or `NOI`; wave; then note, volume and effect (`__`, `UP+n`, `DOWN+n`, `VIB+n`) a step |
| `song_NAME` | frames a step; the place to loop to (from 0, or `END`); pattern numbers |

- Notes are `C1` to `B7` (`CS4` is C sharp). `__` is nothing. `OFF` releases the note.
- The noise of an effect takes `N0` to `N15`.
- An instrument 0 in a pattern keeps the instrument of the channel.
- A held note goes round the volumes from `LOOP` to `RELEASE`. `OFF` jumps to `RELEASE`.
- Limits: 16 instruments, 32 patterns of 64 steps, 16 effects of 32 steps, 8 songs of 64 places, 64 volumes.

```asm
;@music
lead:
    .byte PULSE, DUTY50, 0, $00
    .byte 15, 12, LOOP, 10, RELEASE, 6, 2, 0, END
pattern00:
    .byte C4,LEAD,  __,0,  __,0,  __
    .byte OFF,0,    __,0,  __,0,  __
    .byte END
song_title:
    .byte 6, 0
    .byte 0, END
;@end
```

## The keys of the tab

Tab changes the page: Instruments, SFX, Pattern, Song. Space plays or stops. Ctrl+Z and Ctrl+Y are the history of
the code. The screen shows the piano. Hold F12 to see the keys of the page.

| Keys | Use |
|---|---|
| `q w e r t y u i o p` | the white keys: C D E F G A B, then C D E of the octave above |
| `2 3 5 6 7 9 0` | the black keys (sharps) |
| `z`, `x` | octave down, octave up (1 to 6) |
| `-`, `=` | the note or the value under the cursor: a half tone down (flat) or up (sharp) |
| `_`, `+` | a larger step (a note: one octave) |
| `1` | release (`OFF`) |

## The SNES

The sound chip of the SNES (SPC700) is a second computer. `snd_init` sends it our driver, the data of the block and
six samples. Then it keeps time by itself, 60 ticks a second.

| Step | Assembly | bm C |
|---|---|---|
| Send the driver and start it | `jsr snd_init` before the NMI is on | done by the runtime |
| A song | `lda #song_title` and `jsr play_song` | `play_song(song_title);` |
| An effect | `lda #sfx_jump` and `jsr play_sfx` | `play_sfx(sfx_jump);` |
| Silence the music | `jsr stop_music` | `stop_music();` |

- There is no `snd_frame`. The routines keep the registers. Call them from the main loop, not from the NMI.
- Without `snd_init` the other routines do nothing.
- The assembly routines use the byte `$0F` of the direct page. Registers: A of 8 bits, X and Y of 16.
- The build adds the sound when the block has data or the program calls a routine. The driver and the data go in the
  first bank of 32 KB: about 3 KB, and the patterns (16 bytes a step).

The lists of a SNES block:

| List name | Values, then `END` |
|---|---|
| an instrument | wave (`SQUARE12`, `SQUARE25`, `SQUARE50`, `TRIANGLE`, `SAW`, `SINE`, `NOISE`); noise rate 0 to 31; attack 0 to 15; decay 0 to 7; sustain 0 to 7; release 0 to 31; volume 0 to 127; pan 0 to 16 (8 is the middle); vibrato 0 to 15; arpeggio |
| `pattern00` to `pattern31` | a step is a note and an instrument for each of the 8 voices |
| `sfx_NAME` | frames a step; the voice `V1` to `V8`; the wave; then note, volume 0 to 15 and effect a step |
| `song_NAME` | as on the NES |

- The attack, decay, sustain and release are those of the chip: a larger attack is faster; the release is how fast a
  held note fades (0: it holds).
- On a `NOISE` effect the notes are `N0` to `N15`.
- An effect takes its voice for its length.

```c
//@music
const byte lead[] = {
    SQUARE25, 0,              // wave, noise rate
    15, 7, 7, 0,              // attack, decay, sustain, release
    100, 8, 0, 0x00, END };   // volume, pan, vibrato, arpeggio
const byte pattern00[] = {
    C4,LEAD, __,0, __,0, __,0,   __,0, __,0, __,0, __,0,
    END };
const byte song_title[] = { 12, 0, 0, END };
//@end
```

The driver is `core/src/kernel/shell_sdk_spc.asm`. After a change run
`python core/scripts/spc_asm.py core/src/kernel/shell_sdk_spc.asm core/src/kernel/shell_sdk_spc_bin.h`. Test:
`make test-sdk-snd`.

## Code

`core/src/kernel/shell_sdk_music.c` (the block), `shell_sdk_snd.c` (the driver, 6502), `shell_sdk_sounds.c` (the
tab), `shell_sdk_play.c` (listening: the driver runs in the bm NES core). Test: `make test-sdk-music`, and
`tests/qemu_test.py -k shell_sdk_sounds`.
