# Shell SDK: sound of the NES projects

The Sounds tab (F5) of the Shell SDK writes the music of a NES project. The code holds the data: the block between
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

Tab changes the page: Instruments, SFX, Pattern, Song. Space plays or stops. The piano is `z s x d c v g b h n j m`
and, an octave higher, `q 2 w 3 e r 5 t 6 y 7 u`. `,` and `.` change the octave. `-` and `=` change a value, `_`
and `+` change it more. Ctrl+Z and Ctrl+Y are the history of the code. The last two rows of the screen show the keys
of the page.

## Code

`core/src/kernel/shell_sdk_music.c` (the block), `shell_sdk_snd.c` (the driver, 6502), `shell_sdk_sounds.c` (the
tab), `shell_sdk_play.c` (listening: the driver runs in the bm NES core). Test: `make test-sdk-music`, and
`tests/qemu_test.py -k shell_sdk_sounds`.
