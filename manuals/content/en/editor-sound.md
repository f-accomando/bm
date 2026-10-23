title: bm Sound
accent: Sound
lang: en
kicker: A bm editor
subtitle: Sounds, effects, patterns and songs for your game, made on the console.
footer: bm manuals - bm Sound

::: readme
**What this is.** The manual of bm Sound, the editor that makes the sound effects and the music of a bm game: a synthesizer with 8 voices, a step sequencer and a song maker.

**How to use it.** Read the chapters "What bm Sound is" and "The pages" once, then follow the tutorials in order: effect, pattern, song, import, play.

**What is missing.** Every fact not yet confirmed is marked with a highlighted TO FILL tag; nothing is invented.
:::

# What bm Sound is

bm Sound is the editor of a game's **sound bank**: the sounds, the effects, the patterns and the songs that the game plays with `sfx()` and `music()`.

| Page | Key | What you make there |
|---|---|---|
| SOUNDS | F1 | an instrument: wave, envelope, filter, room and echo |
| SFX | F2 | a short sound effect, step by step |
| PATTERN | F3 | a loop of up to 64 steps on 8 tracks |
| SONG | F4 | the order in which patterns play |

## What it does

- A **sound** is an instrument. The bank holds 32 of them.
- A **sound effect** (SFX) is a short row of up to 32 notes, each with its sound, volume and effect. The bank holds 64.
- A **pattern** is a grid of 8 tracks (one per voice) and up to 64 steps. The bank holds 64.
- A **song** is a list of up to 64 patterns, with a tempo, a swing, a loop point, an echo and a room. The bank holds 8.

## Why it matters

The sounds travel inside the game's `.bm` file, in its AUDIO section. Your game asks for them by number: `sfx(1)` plays sound effect 1, `music(0)` plays song 0. No sound files to carry around.

## Good to know

- bm Sound has 8 voices. The tracks are numbered 1 to 8 and the voices 0 to 7: track `t` of a pattern plays on voice `t - 1` (the screen says `TRACK 1 (voice 0)`).
- The F6 key opens a music assistant that makes beats, melodies and sound effects for you.
- The F7 key opens Riff, where you write a pattern as one line of text.
- Everything you change can be undone with Ctrl+Z.

# Opening bm Sound

bm Sound changes only **projects** (`.bme`). A game (`.bm`) it only reads. This is the rule of the whole suite.

## From the bm SDK

1. Start the **bm SDK** and open your project.
2. On the F1 page, in the list *OPEN IN*, choose **6 bm Sound**.
3. The SDK saves the project and bm Sound opens on the same file.
4. When you finish, press Esc and choose **Back to bm SDK**. It saves first.

## From the menu

[TO FILL: the exact menu entry that starts bm Sound on the console]

## Games and sound packs

Press Esc, then choose **Open a game or a sound pack...** (or press Ctrl+O). The list shows what is in `/carts`, in the root of the card, in `/bme` (projects, sound packs too) and in `/bm/sounds` (older sound packs), with the label "game" or "sound pack".

- If the file has no sounds yet, bm Sound says so and you start from empty banks. **Save** puts the sounds in it.
- If you open a game (`.bm`), the first save asks *Save an editable copy: name.bme?* and from then on you work on the copy.
- **New project** (Ctrl+N) starts from blank banks: sound 0 is named `SOUND` and song 0 is named `SONG`, with the order `{0}` (pattern 0). **The demo project** loads a bank with sounds and songs: press START to hear the song.

::: note
The top bar shows the four pages, `OCT` (the piano octave), the player (`STOP`, `SFX`, `PATTERN` or `SONG n`) and the name of the file. A yellow dot before the name means changes not saved yet.
:::

![The SOUNDS page: the name, the three graphs and the bars of a parameter group](shots/editor-sound-sounds.png)

# The pages

Change page with F1 to F4, with Tab (next page), or on the pad with SELECT + left/right.

## SOUNDS

Here you build an instrument. The 32 slots are in the list on the left. Pick one with PgUp / PgDn (pad: SELECT + up/down).

The parameters are in three groups. Move down past the last row to go to the next group; the third column of the last group shows the group names.

| Group | Parameters |
|---|---|
| SOUND | WAVE, DUTY, VOLUME, DETUNE; ATTACK, DECAY, SUSTAIN, RELEASE; BEND FROM, BEND TIME, VIBRATO, VIB SPEED |
| FILTER | CUTOFF, RESONANCE, MODE, KEY TRACK; FILT ENV, ENV DECAY, DRIVE, NOISE; LFO SPEED, LFO CUT, LFO DUTY, 8-BIT |
| WAVE & SPACE | two wave settings (their names change with the wave), FM FADE, FM FEEDBACK; PAN, ROOM, ECHO |

The waves are: SQUARE, TRIANGLE, SAW, NOISE, SINE, METAL, FM, PLUCK, SUPERSAW, ORGAN, SAMPLE, PINK, BROWN, CRACKLE. The filter modes are LOW PASS, BAND PASS, HIGH PASS and NOTCH.

- The row above the bars is the **name** (8 characters). Press Enter on it to rename the sound.
- Each bar changes with `-` and `=`; `_` and `+` change it by more.
- Pressing a piano key plays the sound at that note.
- START plays the sound as a short chord.
- The Backspace key puts the chosen parameter back to its default.
- In the Esc menu, **Instrument...** puts a ready-made instrument into the sound (the list plays each one as you choose it), and **Samples...** puts a sample into it: the bank's samples and the console's kit (`bd`, `sd`, `hh`, `oh`, `cp`, `rim`, `tom`, `cb`). This sets the wave to SAMPLE.

## SFX

Here you write a sound effect as a row of steps. The header has five fields: NAME, SPEED (milliseconds per step, 5 to 2000), STEPS (1 to 32), LOOP FROM and LOOP TO. The loop repeats the steps between the two points while the effect plays; LOOP TO `OFF` means no loop.

Below the header the 32 steps are drawn as lanes: the height of a bar is the note, the white bar under it is the volume, and a letter shows the effect. The box at the bottom says what the chosen step holds. Editing a step past the end of the effect makes the effect longer.

Every step has a note, a sound, a volume and an effect. The effects are the same in SFX and in PATTERN:

| Letter | Effect | What the amount means |
|---|---|---|
| G | GLIDE | how many steps the slide takes |
| U | BEND UP | semitones |
| D | BEND DOWN | semitones |
| V | VIBRATO | width, in eighths of a semitone |
| T | TREMOLO | depth, in percent |
| C | CHORD | the chord (OCTAVE, MAJOR, MINOR, SUS2, SUS4, MAJ7, MIN7, DOM7, DIM, AUG, POWER and more) |
| A | ARPEGGIO | the chord that is run through |
| O | FADE OUT | how many steps |
| I | FADE IN | how many steps |
| R | RETRIGGER | how many hits |
| L | DELAY | the start, in sixteenths of a step |
| X | CUT | the end, in sixteenths of a step |

![The SFX page: the header fields, the lanes and the step box](shots/editor-sound-sfx.png)

## PATTERN

A pattern is a grid: 8 rows (the tracks, numbered 1 to 8) and up to 64 steps, shown 16 at a time. The boxes at the top right show the pages of 16 steps.

The header has three fields: STEPS (1 to 64), TEMPO and SWING. Tempo and swing belong to the **song** you have chosen, not to the pattern: the header shows the ones of the current song.

- Each track has a header at the left with its number and the sound new notes get. On the header, Enter **mutes** the track (an `M` appears) and `-` / `=` change the sound of the track.
- A note is a coloured cell; the colour is stronger when the volume is higher. `--` in red is a note OFF (the note is released). A white corner marks a step with an effect.
- While a song or the pattern plays, the step being played is lit.

![The PATTERN page: eight tracks, sixteen steps](shots/editor-sound-pattern.png)

## SONG

A song is a row of up to 64 slots; each slot holds a pattern number. The 8 songs are in the list on the left.

The header has six fields:

| Field | What it does |
|---|---|
| NAME | the song's name (8 characters) |
| TEMPO | beats per minute, 40 to 255 |
| SWING | 0 to 100%: the first step of each pair is longer |
| LOOP | where the song goes on after the end (`OFF`: it stops) |
| ECHO | the echo's time, in steps, from `AS IS` to 16 |
| ROOM | the size of the room, from `AS IS` to a hall |

A sound's own ECHO and ROOM say how much of it goes to the song's echo and room.

Under the slots a box shows the chosen pattern and a small picture of its notes.

![The SONG page: the order of the patterns](shots/editor-sound-song.png)

## The assistant (F6)

F6 (or **Compose with the assistant...** in the Esc menu) opens a panel that makes rhythms, backing tracks, bass lines, arpeggios, melodies and sound effects. Each one is heard while you choose it, and A (Enter on the keyboard) puts it in the bank. On the keyboard the letter `a` is typed into the search text.

- Rhythms and backing tracks go into new patterns, with a song of their own.
- Bass lines, arpeggios and melodies go over the pattern on the page and follow its key and chords.
- A sound effect goes into the first free slot.
- The instruments they use become sounds of the bank; a sound with the same name is used again.
- Ctrl+Z takes it back.

![The assistant panel on top of the PATTERN page](shots/editor-sound-assist.png)

Keys inside the panel: Up and Down choose, Left and Right change the variant, Enter puts it in the bank, Esc closes, Tab goes to the next mode (pad: X), PgUp and PgDn scroll, typing sets the request, Ctrl+U clears it.

## Riff (F7)

F7 (or **Riff...** in the menu) opens a box where you write a pattern in one line, for example `s "kick*4, ~ snare, hat*8"`. Enter plays it with the bank's sounds and the ready-made instruments. Ctrl+Enter puts it into the bank as a song. See the manual `riff` for the language.

# Controls

The pad and the keyboard do the same things. The bottom line of the screen shows the keys of the page you are on. The bar ends with the chip F12 keys. Click it, or hold F12, to see every key of the page.

| Action | Pad | Keyboard |
|---|---|---|
| Move | D-pad | Arrow keys |
| Add or remove a note, choose | A | Enter |
| Note or value -1 / +1 | A + up / down | `-` / `=` |
| Octave, or value -10 / +10 | A + left / right | `_` / `+` |
| Sound of the step | Y + up / down | `[` / `]` |
| Volume of the step | Y + left / right | `;` / `'` |
| Effect | B + up / down | `k` / `l` |
| Amount of the effect | B + left / right | `o` / `p` |
| Clear the step | X | Backspace |
| Listen to the step, or insert a copy (SONG) | Y | `i` |
| Play / stop | START | Space |
| Change page | SELECT + left / right | F1 to F4, Tab |
| Next or previous sound, effect, pattern, song | SELECT + up / down | PgUp / PgDn |
| Menu | SELECT | Esc |
| Piano, lower row | - | `z s x d c v g b h n j m` |
| Piano, upper row (an octave up) | - | `q 2 w 3 e r 5 t 6 y 7 u` |
| Piano octave | - | `,` / `.` |
| Save | - | Ctrl+S |
| Save as a new sound pack | - | Ctrl+Shift+S |
| Open | - | Ctrl+O |
| New project | - | Ctrl+N |
| Undo | - | Ctrl+Z |
| Copy / paste the item | - | Ctrl+C / Ctrl+V |
| Export to a game | - | Ctrl+E |
| Import a WAV or MIDI file | - | Ctrl+W |
| Try it in the game | - | F5 or Ctrl+R |
| Assistant | - | F6 |
| Riff | - | F7 |
| Leave | START + SELECT | Esc, then **Exit** |

The mouse works too: a click chooses, a double click does what A does, the wheel changes the value under the pointer, and a bar dragged left or right changes it.

In the name editor a click on a letter box chooses that letter. In the riff line a click puts the caret there.

::: note
A short tap on SELECT opens the menu. If you press SELECT with left/right or up/down, it changes page or item and the menu does not open.
:::

[TO FILL: check these pad keys on the PowKiddy RGB30, where B confirms and A cancels in the rest of bm; not tried on the console yet]

## The Esc menu

| Entry | What it does |
|---|---|
| Play / Stop | plays or stops the music or the effect (START) |
| Undo | takes back the last edit (Ctrl+Z) |
| Save | writes into the open file; with no file, asks for a pack name |
| Save as a new sound pack... | asks for 8 characters and writes `/bme/name.bme` |
| Open a game or a sound pack... | opens another file (Ctrl+O) |
| Import from... | takes a sound, an effect, a pattern or a song from another file |
| Export to a game... | writes this bank into a game |
| Instrument... | SOUNDS page only: a ready-made sound |
| Samples... | a sample into the sound |
| Import WAV or MIDI... | a WAV file as a sample, a MIDI file as a song, from `/docs` (Ctrl+W) |
| Compose with the assistant... | opens the assistant (F6) |
| Riff... | opens Riff (F7) |
| Try it in the game | with a game open: saves and runs it |
| Copy / Paste over / Clear this item | the sound, effect, pattern or song of the page |
| New project, The demo project | start again; load the demo |
| Controls | shows the key list |
| Back to bm SDK | only if the SDK opened bm Sound; saves first |
| Exit | asks first if there are changes not saved |

# Tutorials

## Make a sound effect

You will make a short "coin" sound. Start in bm Sound with a new project (Esc, **New project**).

1. Press F1 to go to SOUNDS. Open the Esc menu, choose **Instrument...** and pick one of the instruments. The list plays each one as you move on it. The one you choose goes into sound 0.
2. Press a piano key such as `z`. You hear sound 0 at that note.
3. Press F2 to go to SFX. The cursor is on step 1 of effect 0.
4. Press Enter. A note appears at step 1 and you hear it. It uses the sound that was chosen on the SOUNDS page.
5. Press the right arrow and Enter again to add step 2. Press `=` to raise it by one semitone; press `_` or `+` to move it an octave down or up.
6. Press Up on the first step row to reach the header. Move to SPEED with the right arrow and press `-` to make the steps shorter (fewer ms) or `=` to make them longer (more ms). `_` and `+` change it by 10 ms.
7. Press Space (START on the pad). You hear the effect. Press Space again to stop.
8. To give the effect a slide, go back to a step with the arrow keys and press `l` twice: the step box says BEND UP. Then press `p` for a bigger amount.
9. Go to the header, choose NAME, press Enter and type a name of up to 8 characters. Press Enter to confirm.

::: try
Make step 1 a low note and step 2 an octave above it, with SPEED at 60 ms. That is a coin.
:::

If you hear nothing, check that the volume of the console is up and that the step has a note (a lane without a bar is empty).

## Make a pattern

1. Press F3 to go to PATTERN. Choose a free pattern with PgUp / PgDn.
2. Move to the header with Up (from track 1) and set STEPS to 16 with `-` / `=`. Set TEMPO in the same way.
3. Go back to the header of track 1 (the number at the left): from the STEPS / TEMPO / SWING header press Down first, then press the left arrow. Press `-` / `=` to choose the sound of that track. You hear it.
4. Move to the first cell and press Enter. A note is placed. Press `=` to change it. Press Y or `i` to hear it.
5. Fill other steps. With the piano keys (`z s x d c v`) a note is placed and the cursor moves to the next step.
6. Do the same on track 2 with another sound, for a bass or a drum.
7. Press Space. The pattern plays and the step being played is lit.
8. To mute a track while you listen, go to its header and press Enter. An `M` appears. Press Enter again to unmute.

::: try
Put a short noise sound on track 1 at steps 1, 5, 9 and 13, and a bass on track 2 at steps 1 and 9.
:::

## Build a song

1. Press F4 to go to SONG. Choose a free song with PgUp / PgDn.
2. Move to the first slot. It shows a `+`. Press Enter: slot 1 gets a pattern (pattern 0 for the first one; for the next ones, a copy of the last).
3. Press `=` or `-` on the slot to choose the pattern number. Press `_` / `+` to jump by 10.
4. Press Enter on a slot that already has a pattern: bm Sound goes to the PATTERN page on that pattern. Edit it, then press F4 to come back.
5. To repeat a pattern, press `i` on its slot: a copy is inserted after it. To remove a slot, press Backspace.
6. Go up to the header to set TEMPO, SWING and LOOP. With LOOP set to `FROM 1` the song starts again from the first slot at the end.
7. Press Space (START). The song plays from the chosen slot, and a white bar shows the slot being played.

::: try
Make a song of four slots: pattern 0 twice, then pattern 1, then pattern 0 again. Set LOOP to `FROM 1`.
:::

## Import a sound from another game

You can take a sound, an effect, a pattern or a song from another game or sound pack.

1. Open the Esc menu and choose **Import from...**.
2. In the list, choose the file. The list shows games and sound packs. While the cursor rests on a file, its content is read: how many sounds, effects, patterns and songs it has.
3. Choose what to take: *a sound*, *a sound effect*, *a pattern*, *a song* or *everything*.
4. Choose the item from the list that follows.
5. A message says where it went: for example `imported as sound effect 5`. The sounds that an effect, a pattern or a song uses come with it, in free slots. A sound that is already the same in your bank is not copied twice. A SAMPLE sound brings its sample too (a sample with the same bytes is not copied twice).
6. Press Ctrl+S to save.

::: warn
*everything* replaces the whole bank you are working on, after a question. If there is no free slot, the import stops with a message such as `no free sound slots`. Undo is cleared after an import.
:::

## Import a WAV or MIDI file

You can bring a sound recording or a song from the SD card into the bank. Put the file in the `/docs` folder of the card, with a name of the 8.3 kind (`kick.wav`, `tune.mid`).

1. Open the Esc menu and choose **Import WAV or MIDI...** (or press Ctrl+W).
2. In the list, choose the file. The list shows the `.wav` and `.mid` files of `/docs` with their size.
3. For a WAV file, choose how to keep it: *as it is*, *mono*, *mono, 22050 Hz* or *mono, 11025 Hz*. The last ones make the sample smaller. A message says the result, for example `sample 2 TONE: 22050 Hz mono, 0.5 s, in sound 00`.
4. The sample goes to the end of the bank's samples, and the sound you are on plays it (the wave becomes SAMPLE). The root note and the loop in the file are kept, when the file has them. A sample with the same bytes is not added twice.
5. For a MIDI file, a new song appears with the file's name. Its patterns have 64 steps (or whole bars of the file's time signature), and the song has the file's tempo. A message says what was made, for example `song 3: 1 patterns, 120 BPM, 4 new sounds`.
6. Press Ctrl+S to save.

What the WAV import reads: 8, 16, 24 and 32-bit whole numbers and 32 and 64-bit floats, mono or stereo, up to 4 MiB. A rate above 48000 Hz is brought down to 48000 Hz.

What the MIDI import does:
- One step is a sixteenth note. Notes are put on the nearest step.
- Each MIDI channel gets a sound. Its wave follows the General MIDI program (a bass is a SAW, a piano a PLUCK). The drum channel (10) plays the console's drum kit: `bd`, `sd`, `hh`, `oh`, `cp`, `rim`, `tom`, `cb`.
- A pattern has 8 tracks, one note on a track at a time. Chords go to more tracks. A note that ends before the next one gets an OFF step.
- Only the first tempo and the first time signature are used.

::: warn
A MIDI note over the 8 tracks is dropped, and the message tells how many. If the bank has fewer free pattern slots than the song needs, the song is cut to the slots there are. A pattern that a song of the bank already plays is not taken. The history of undo is cleared. A sample over the 64-sample limit or over the bank's memory is refused. A file with 12-bit samples, more than 2 channels or a rate under 1000 Hz is refused with a message. Not verified on the Pi and on the RGB30.
:::

## Play it in your game

1. In bm Sound, with your project open (see "Opening bm Sound"), press Ctrl+S. The sounds are written into the project.
2. In your game's Lua code, call the bank by number:

```lua
function _init()
  music(0)                         -- song 0, looping as set in LOOP
end
function _update()
  if btnp(4) then sfx(1) end       -- sound effect 1
  if btnp(8) then music(-1, 500) end
end
```

3. To hear it in the game at once, press F5 in bm Sound (Esc, **Try it in the game**). It saves first and then runs the game.
4. When you are happy, build the game from the SDK with Ctrl+B. This writes the final `.bm`.

::: copy
sfx(n)            -- play sound effect n of the bank
sfx(-1)           -- stop the effects
music(n)          -- play song n
music(-1, 500)    -- stop the music, fading it out in 500 ms
:::

An effect uses a free voice and the music's track on that voice is silent while it plays. A song that uses only tracks 1 to 6 leaves voices 6 and 7 free for effects. See the manual `api-sound` for all the calls.

# Saving and exporting

- **Save** (Ctrl+S) writes the bank into the open file. With a game open, the first save asks for the editable copy `name.bme`. After a save the bar says the game plays the new sounds.
- **Save as a new sound pack...** (Ctrl+Shift+S) asks for a name of up to 8 characters and writes `/bme/name.bme`. A sound pack is a small program that plays your songs and effects; you can open it later in bm Sound.
- **Export to a game...** (Ctrl+E) lists the games and, after a question, writes this bank into the one you choose. Its sounds and music are replaced by these.
::: warn
Export replaces the sounds of the game you choose. The question names the file: read it before you confirm.
:::

# Tips and limits

## Limits

| What | Limit |
|---|---|
| Sounds | 32 |
| Sound effects | 64, up to 32 steps each |
| Patterns | 64, up to 64 steps, 8 tracks |
| Songs | 8, up to 64 slots each |
| Names | 8 characters |
| SFX speed | 5 to 2000 ms per step |
| Tempo | 40 to 255 BPM |
| Notes | 1 to 127 (piano octave 0 to 8) |

## Tips

- Keep effects short and put the sounds the music does not use on high tracks (voices 6 and 7), so effects do not cut the music.
- Use the NOISE, METAL and CRACKLE waves for hits and explosions; PLUCK and ORGAN for melodies.
- Mute tracks to hear the others while you write a bass or a lead.
- Copy a pattern or a sound with Ctrl+C and paste it with Ctrl+V to make a variation without losing the first.
- The notes of the piano rows play on the page's sound; change the octave with `,` and `.`.
- Ctrl+Z undoes the last edit, also a paste, a clear or an assistant piece.
- If **Import WAV or MIDI...** says `no .WAV or .MID file in /docs on the SD card`, the files are not in the `/docs` folder, or their names are longer than the 8.3 kind.
- When you choose a file in **Import from...**, a line may say `a sound bank this version cannot read`: that file cannot be imported.
