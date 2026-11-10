# bm Tape: a portable studio cartridge (design)

Name: **bm Tape** (folder `carts/tape`, project `carts/tape/tape.bme`).

bm Tape is one `.bm` app with five jobs: synth, sampler, sequencer, multitrack, and live DJ set. The references are the OP-1 (one display, four colour encoders, a tape) and the EP-133 K.O. II (pads, groups, punch-in effects; bm has `carts/ko2`). All code and all pictures are original.

Status: phase 3 (synth, drums, sequencer, song with TAPE clips, MIX, FX, punch-in effects, save and load; `carts/tape`) passes on bmhost (`make test-tape`). Test it on the Pi Zero W. DJ is still the scene of phase 1 (it shows PREVIEW). Section 10 gives the phases.

Development state (source map, build, tests, keys, what is missing): [`bm-tape-dev.md`](bm-tape-dev.md).

## 1. Principles

1. **One display, one function.** The display shows one scene at a time. Each function has its own scene. The scene is the editor: the picture moves when a value moves.
2. **Four encoders, four colours.** Each scene has at most four live values. Each value has the colour of its encoder. A drawn part that has the colour of an encoder changes when that encoder turns.
3. **Vector scenes.** A scene is lines, rectangles, and short text on a 320×160 unit grid. The cartridge draws it at scale `S` (1 on a 360×360 canvas, 2 on a 640×360 canvas). No bitmaps in the display, except the stroke font.
4. **One clock.** All timing comes from `audio_time()` and `play_at()` (1.3 ms grid in the audio interrupt). The frame rate does not move a note.
5. **One sound format.** A sound of bm Tape is a bank sound (`BMAU`, 48 bytes). bm Sound, riff, and games read the same sound.
6. **One source for typed code.** The hot loops are `.b` files. The Pi compiles them on the console to native ARMv6 code.

## 2. What the kernel has and what it lacks

Facts read on 9 Nov 2026 (`docs/progress/audio.md`, `docs/api/sound.md`, `docs/api/riff.md`, `docs/api/typed.md`, `docs/progress/typed.md`).

| Need | State | Source |
|---|---|---|
| 8 voices, 48 kHz stereo, 14 waves, filter, LFO, drive, crush, vowel, duck per voice | ready | `src/audio/synth.c` |
| Reverb, ping-pong echo, chorus (sends per voice) | ready | `reverb()`, `echo()`, `chorus()` |
| Notes on an exact time | ready | `play_at()`, `audio_time()`, `play_voices()` |
| 50 presets and an 8-drum kit | ready | `src/audio/presets.c`, `audio_samples("kit")` |
| PCM samples in the bank of a cartridge (16 MB limit) | ready, read-only at run time | `docs/progress/audio-samples.md` |
| Patterns as text, bake to a bank piece | ready | `require "riff"`, `R.bake()` |
| `.b` compiled on the console | ready on the Pi (ARMv6) | `native_compile`, `native_load`, `native_run`, `vm_buf` |
| `.b` host calls | `cls`, `pix`, `rectfill`, `spr`, `btn` only | `docs/progress/typed.md` stage 7 |
| **K1** PCM sample made at run time (from a `vm_buf` or a WAV on the SD) | missing | new API |
| **K2** Bounce: render events to a PCM sample off line | missing | new API |
| **K3** Scope tap: the last N output samples and the peak per voice | missing | new API |
| **K4** `line` (and `text`) in the `.b` host table | missing | `bmnative_set_host_fn` |
| **K5** `.b` source carried in a `.bm` | ready with a Lua long string (section 6) | `BM_SEC_DATA` |
| **K6** More than 8 voices | measured on the host only (section 3.2); 8 voices stay | `SYNTH_VOICES` |
| Audio input | no hardware on the Pi Zero W or on the RGB30 | - |

Consequence of the last row: "sampling" means three things here. Load a WAV from the SD. Resample the synth (K2). Slice and edit a sample that is in memory (K1).

## 3. Modules

| Module | Job | Model | First phase |
|---|---|---|---|
| **SYNTH** | Play and edit one melodic sound. Engine page, envelope page, filter/LFO page, send page. | OP-1 synth | P2 |
| **DRUM** | 12 pads × 4 groups. A pad is a kit drum, a synth one-shot, or a sample slice. | KO-II | P2 |
| **SEQ** | 8 patterns (A to H) of 16 to 64 steps per track. Three writers: step grid, live record with quantize, riff lane. Per-step lock of two values. | KO-II, riff | P2 (patterns P3) |
| **TAPE** | 4 tracks on a song of 64 slots. A slot is one pattern length. A clip is a run of equal slots (events); a frozen sample (K2) comes in P5. Loop points, PATTERN and SONG mode, track mute. Real in P3. | OP-1 tape | P3 |
| **MIX** | 4 track levels and pans (real in P3). The meters follow the queued notes (no K3). Master drive and master level wait for a master bus. | OP-1 mixer | P3 |
| **FX** | Echo, room and chorus pages with a send for each track (real in P3). Punch-in effects held on a key: stutter, half speed, filter sweep, crush (real in P3). | KO-II | P3 |
| **DJ** | 2 decks and a crossfader. A deck plays a frozen song or a pattern set. Pitch fader, nudge, cue points, loop, filter, brake. | live set | P6 |
| **LIB** | Sounds, kits, samples, songs. Read and write the bank, `.bms` resources, and WAV. | bm Sound | P4 |

### 3.1 The voice budget (the main constraint)

The synth has 8 voices. The modules share them with `play_voices()`.

The API gives no voices per track. `play_at` picks the voice when the note starts: a silent voice, then a tail, then the oldest note of `play_at` (`src/audio/player.c`, `pick_at_voice`). `play_voices(...)` is one mask for all notes of `play_at` (`player_at_voices`). It keeps `play_at` away from the voices outside the mask. A call `play(v, ...)` with a voice number takes that voice (`src/bm/api_sound.c`, `l_play`). Phase 2 uses this split:

| Use | Voices | Note |
|---|---|---|
| Sequencer, tracks 1 to 4 | 0 to 5 | `play_voices(0, 1, 2, 3, 4, 5)`. The player picks the voice. When six notes sound, the next note takes the oldest one. |
| Live play from the keyboard | 6 and 7 | `play(v, ...)` with 6 and 7 in turn. A held key is released with `noteoff(v)`. |

A drum pad is held 60 ms and then released, so it frees its voice early. A hat does not cut an open hat: this needs a voice per track and waits for a better API.

A frozen track (K2) plays as one sample voice. Freeze gives the voices back. This is the bm equivalent of a tape overdub: freeze track 3, then use its 3 voices for a new part.

DJ budget: deck A 1 voice, deck B 1 voice (mono bounce; 2 each if the bounce is stereo), 4 to 6 voices for live pads over the mix. A sample voice changes speed and pitch together, as a record does. `freq()` nudges, `slide()` brakes, `begin` sets a cue point, `reverse` plays backward, the voice filter is the DJ filter. The DJ module needs no time stretch.

Without K2 (phases 1 to 4) a deck is a pattern set of 4 voices. The DJ module waits for K2.

### 3.2 Measure of 12 and 16 voices (host, 9 Nov 2026)

| Voices | Plain square, µs per 64-sample block | Heavy patch, µs per block |
|---|---|---|
| 8 | 3.8 to 4.7 | 13.7 |
| 12 | 5.8 | 16.6 to 19.5 |
| 16 | 7.2 to 9.9 | 23.3 to 26.0 |

- The measure calls `synth_render` on an x86 host. The build is `gcc -O2`. There are two runs. The benchmark is a throwaway program that is not in the repository.
- The heavy patch uses supersaw, FM, saw, and square with filter, LFO, drive, room send, and echo send.
- A block lasts 1333 µs.
- The cost grows about 1.3 times for 12 voices and about 1.8 times for 16 voices. The room, the echo, and the limiter are a fixed part.
- The host numbers do not give the Pi time. Measure on the Pi Zero W before a decision.
- `AU_TRACKS` in `src/audio/player.h` equals `SYNTH_VOICES`. The register block is `SYNTH_VOICES * 32` bytes. A change of the voice count changes the bank format and the three encoders.
- Decision: keep 8 voices and use freeze.

## 4. The display

### 4.1 Frame

```
+--------------------------------------------------------------+
| SYNTH . FM            A1      120.0 BPM     03.2.1    [####] |  status strip, 12 units
|                                                              |
|                         the scene                            |  320 x 124 units
|                                                              |
| RATIO 2.00     DEPTH 3.4     FEEDBACK 0.20     DECAY 180 ms  |  value strip, 24 units
+--------------------------------------------------------------+
```

- Status strip: module and page, track, tempo, bar.beat.step, voice lights (8 dots).
- Value strip: four cells, one per encoder, in the colour of the encoder.
- Scene: all that is between the strips.

### 4.2 Palette

| Token | Use | Colour |
|---|---|---|
| `ink` | background | deep violet-black |
| `grid` | horizon grid, idle parts | dim violet |
| `e1` | encoder 1 | cyan |
| `e2` | encoder 2 | magenta |
| `e3` | encoder 3 | amber |
| `e4` | encoder 4 | white |
| `hot` | play head, record, clip | red-orange |
| `mute` | secondary text | grey-violet |

Eight colours. Lines are 1 unit wide. The sun is the only filled shape with bands. Retro mode (`retro(true)`) changes no colour: the look is the same.

### 4.3 Scenes

The common ground is a horizon at 60% of the height with a perspective grid below it. Each scene puts one object on this ground.

| Function | Scene | e1 | e2 | e3 | e4 |
|---|---|---|---|---|---|
| SYNTH / FM | Two orbits around a planet. The moon is the modulator. | ratio: radius of the moon orbit | depth: size of the moon | feedback: tilt of the orbit | mod decay: length of the trail |
| SYNTH / STRING (pluck) | A string between two pylons. | bright: number of nodes | ring: amplitude decay | cutoff: sun height | drive: line thickness |
| SYNTH / PULSE (square) | A skyline. The width of the towers is the duty. | duty | PWM depth: towers breathe | sub noise: stars | cutoff: sun height |
| SYNTH / CLUSTER (supersaw) | Three laser beams from one point. | spread: angle | cutoff | resonance: glow ring | drive |
| SYNTH / ORGAN | Four neon pillars. | bar 1 | bar 2 | bar 3 | bar 4 |
| SYNTH / VOX (vowel) | A vector mouth. | vowel: mouth shape | cutoff | LFO rate | tremolo |
| SYNTH / DIGITAL (crush, fold) | A wave that breaks into blocks. | crush: block height | coarse: block width | curve: fold shape | drive |
| ENVELOPE | A mountain ridge. The ridge is the ADSR curve. | attack | decay | sustain | release |
| FILTER / LFO | The sun. The cut line of the sun is the cutoff. Water below shows the LFO. | cutoff | resonance | LFO rate | LFO depth |
| DRUM / pad | The waveform of the pad as a ridge, with start and end gates. 12 pad lights. | start | end / length | pitch | level |
| SEQ / grid | A road to the horizon with 16 gates. A lit gate is a step. The play head is a light that runs to the viewer. | step cursor | note / pad | velocity | gate length |
| SEQ / riff | The text of the riff line. Sounding words light up, as in bm Code. | cycle length | swing | transpose | level |
| TAPE | Two reels above 4 track lanes with clips. The reels turn with the song. | scroll | loop start | loop end | track select |
| MIX | Four towers with a level cap and a moving meter. The sun is the master. | level 1 | level 2 | level 3 | level 4 |
| FX / echo | Suns that go back to the horizon, one per repeat. | time | feedback | wet | send of the track |
| FX / room | A wire room. The depth is the size. | size | damp | wet | send of the track |
| FX / chorus | Two palms that sway out of phase. | rate | depth | wet | send of the track |
| DJ | Two discs with a phase mark, two wave strips, a crossfader on the horizon. | deck A pitch | deck B pitch | crossfader | filter of the active deck |
| TEMPO | A pendulum under the sun. | BPM | swing | metronome level | pattern length (count-in bars wait for the record modes) |
| LIB | A shelf of cassettes. The label is the name. | scroll | kind | - | - |

A picture of nine of these scenes is at https://claude.ai/artifact/9hAxdY3ARgj9XHU6moybsX (private page of the user). Port the nine drawings to Lua in phase 1.

### 4.4 Screen layout

| Device | Canvas | Display | Rest of the screen |
|---|---|---|---|
| Pi | 640×360 | 640×320 at (0, 0), `S` = 2 | a 40-pixel strip: mode keys and key hints |

The scenes use a 320×160 unit grid. A later RGB30 layout draws them at `S` = 1.

Measure the fill cost of `S` = 2 on the Pi in phase 1. If a scene costs more than 4 ms, draw the ground once and the moving parts each frame.

## 5. Controls

Keys as of phase 3. The cartridge reads USB keys with `rawkeys(true)`.

| Action | USB keyboard (Pi) |
|---|---|
| Encoder 1 to 4 | `Q`/`A`, `W`/`S`, `E`/`D`, `R`/`F` (up / down). Hold to repeat. |
| Fine step | add Shift |
| Module | `F1` SYNTH, `F2` DRUM, `F3` SEQ, `F4` TAPE, `F5` MIX, `F6` FX, `F7` DJ, `F8` TEMPO |
| Page | `Tab` goes to the next page of the module. Left and Right go through all ten pages in order. |
| Engine (SYNTH ENGINE page) | Up / Down |
| Track 1 to 4 | `1` to `4`. Track 1 is the drum track. `F2` selects track 1. `F1` selects the last synth track. |
| Mute a track | Shift + `1` to `4` |
| Pads and notes | the KO-II block `7 8 9 / U I O / J K L / M , .`. Track 1: pads 1 to 12. Tracks 2 to 4: 12 semitones from the root. The note holds while the key is down. |
| Octave of a synth track | `-` / `=` |
| Sound of the synth track | `[` / `]` (previous / next of the 20 sounds) |
| Play / stop | Space |
| Record | Enter arms and disarms. `REC` shows on the status strip. |
| SEQ GRID: toggle a slot | `T` (a step you enter also sounds once) |
| SEQ GRID: clear | Backspace clears the step, Shift + Backspace the track |
| Pattern of the current track | `;` previous, `'` next (A to H). SEQ GRID and recording edit the selected pattern. The status strip shows its letter after the track. |
| Transport mode | `P`: PATTERN (`PAT`, every track loops its pattern) or SONG (`SONG`, the slots between LOOP IN and LOOP OUT). While the transport plays, it starts again from the loop start. |
| TAPE: write, clear | `T` writes the selected pattern of the current track at the cursor slot; Backspace clears the slot; Shift + Backspace clears the song row of the track |
| Punch-in effect (hold) | `Z` stutter (the events of the last 1/16 note repeat every 1/16), `X` half speed (the clock runs at half tempo; on release it goes on from where it is), `C` filter sweep (a low pass that falls on every track), `V` crush. A `hot` label on the status strip shows the active effect. |
| Save, new song | Ctrl + `S` saves. The cartridge also saves on `_exit()` and when the exit page closes it. Ctrl + `N` asks; a second Ctrl + `N` within 2 s starts the demo song. `SAVED` or the error shows on the status strip for 2 s. |
| Exit page | Esc |

Pages: SYNTH (ENGINE, ENVELOPE, FILTER), DRUM (PAD), SEQ (GRID), TAPE, MIX (LEVEL, PAN), FX (ECHO, ROOM, CHORUS), DJ, TEMPO.

Encoders per page in phase 2:

| Page | e1 | e2 | e3 | e4 |
|---|---|---|---|---|
| SYNTH ENGINE | the four macros of the engine (4.3). A macro that is a filter value (cutoff, resonance, LFO rate) is the same value as on the FILTER page. | | | |
| SYNTH ENVELOPE | attack | decay | sustain | release |
| SYNTH FILTER | cutoff | resonance | LFO rate | LFO depth |
| DRUM PAD (last pad hit) | start | decay (the release: the API has no sample end) | pitch (semitones) | level |
| SEQ GRID | step cursor | note or pad | velocity | gate (1 to 16 steps) |
| TAPE | cursor slot | LOOP IN | LOOP OUT | track |
| MIX LEVEL | level of track 1 | level of track 2 | level of track 3 | level of track 4 |
| MIX PAN | pan of track 1 (-1 to 1) | pan of track 2 | pan of track 3 | pan of track 4 |
| FX ECHO | time (50 to 680 ms) | feedback | wet | echo send of the current track |
| FX ROOM | size | damp | wet | room send of the current track |
| FX CHORUS | rate | depth | wet | chorus send of the current track |
| TEMPO | BPM (60 to 200, Shift: 0.1) | swing (0 to 3 ticks) | metronome level | pattern length (16, 32, 48, 64) |

Record: while the transport plays and record is armed, a pad or note key writes into the selected track at the nearest step. A hit in the last half step of the loop goes to step 1. A synth note takes the time the key is held as its gate.

## 6. The `.b` integration (first cartridge with `.b`)

Facts: `native_compile(src)` compiles `.b` text on the Pi into ARMv6 code in RAM. The Lua output of a `.b` file is the source minus the types (`tools/bml/bml.py --lua`), and it is the reference of every test.

Decided plan:

1. The project holds `src/engine.b`.
2. The build puts the `.b` text into the Lua source as a long string. A Lua cartridge has no API to read a DATA blob (`bm_data_find` is used only by the kernel in `src/kernel/appdata.c`).
3. At `_init`, the cartridge calls `native_compile`, then `native_load`, then `native_run` with `vm_buf` arguments.
4. `targets: pi` is set in `bm.info`. This agrees with the rule in `docs/progress/typed.md`.
5. The `.bm` carries source, not machine code. The "one cartridge type" rule holds.
6. Each `.b` function has a host test. The test compares the native result with the Lua output of `tools/bml/bml.py`.

### 6.1 What goes into `.b`

Rule of `docs/design/bml-manual.md` 9.7: one call per frame, all data in `vm_buf`.

| Function | Job | Data |
|---|---|---|
| `song_window(t0, t1, ...)` | Scan the song slots and the pattern step arrays of all tracks for the ticks in a time window (PATTERN mode: one slot for each track). Write the events to an output buffer. Lua sends them with `play_at`. | slots, steps, mutes, out |
| `euclid(k, n, r, out)`, `arp_fill(...)` | Pattern generators | out |
| `wave_peaks(pcm, n, cols, out)` | Minimum and maximum per column for a waveform picture | PCM, out |
| `scene_ground(t, s)` | The horizon grid with perspective (needs K4) | - |
| `scope_draw(buf, n, ...)` | The oscilloscope and the meters (needs K3, K4) | scope |
| `meter_fall(levels, peaks, dt)` | Meter ballistics | levels |
| `pcm_gain`, `pcm_fade`, `pcm_reverse`, `pcm_normalize` | Sample edits (needs K1) | PCM |
| `slice_find(pcm, n, out)` | Transient detection for slices (needs K1) | PCM, out |

Lua keeps: the state of the UI, the files, riff, the calls of the sound API, text.

Each `.b` function gets a test in `tests/bml/` that compares native, `bmvm`, and Lua, as the rule of the typed area says.

## 7. riff and bm Sound

- **Sounds.** A sound slot is a bank sound. LIB reads the sounds of a `.bms` or of a bank and writes them back. The three encoders of the bank format (`au_parse`, `carts/sound/main.lua`, `scripts/bmaudio.py`) stay the only writers of the format: bm Tape calls shared code, it does not add a fourth encoder. Put the Lua encoder of bm Sound in a module that both carts `require`.
- **Songs.** Export a tape as a bank song (patterns and sequence), so a game plays it with `music()`. Import a bank song as 4 tracks. A bank pattern has one track per voice: the import maps voices to tracks with the budget of 3.1.
- **riff lane.** A track can hold a riff line and not a step grid. The cartridge plays it with `R.play(track_name, pattern)` and gives riff the voices of that track with `R.voices()`. `R.setcpm(bpm / 4)` ties riff to the tempo: one cycle is one bar. "Bake" turns the lane into steps with `R.bake()`.
- **Live code.** A page of SEQ takes `R.code(text)` from the USB keyboard, as bm Code does.

## 8. A better sound library

Three layers. Only the third changes the kernel.

A fourth source is built (work order [`bm-tape-wo-felucca.md`](bm-tape-wo-felucca.md)): the engine `PCM`. It plays 24 presets of the MIT engines of Felucca-Engines, rendered on the PC into bank samples.

- **Render.** `scripts/felpack.py` makes the tables (`--fs 32000`), builds `tools/felrender/felrender.c` with `third_party/felucca-engines/`, and renders one note of each preset of `carts/tape/sounds/felucca.json`. It cuts the tail at -60 dB, fades the last 10 ms and sets the peak to -1 dB. Each sample is 32000 Hz, mono, 16 bits. The root is C2 for a bass and C3 for the others. The total is 2.35 MB. The build is deterministic: `tests/tape/felucca_golden.txt` holds the SHA-256 of each WAV.
- **Engine.** The base tone is `{ wave = "sample", sample = <name> }`. The four macros are START (`begin`, 0 to 0.9), CUTOFF, RESO, and DRIVE. They change the bm voice. They do not change the timbre of the Felucca engine.
- **Sounds.** The sound table has 44 entries: 20 synth sounds, then 24 PCM sounds. The keys `[` and `]` reach them in that order. The 24 sounds: PHASE (BRASS, ORGAN, STRING, RESO, BELL, WIRE), TRIO (FAT BASS, ARP LEAD, SYNC LEAD, RING BELL, CHIP CHOIR), WHEEL (FULL ORGAN, JAZZ PERC, GOSPEL, SOFT FLUTE, ROCK DRIVE), ANALOG (RAVE, PWM STR, SUB BASS, BRASS), LOFI (PULSE LD, WAVE BASS, WAVE LEAD), NOISE (METAL).
- **Scene.** The PCM scene draws the sample as a ridge from the 32 peaks of `audio_samples()`. Encoder 1 moves the start gate. The sun shows the cutoff.
- **Limits.** A held key stops at the end of the sample: the order has no loop points. The envelope of the host in `felrender.c` is a bm choice (the release is at most 1.5 s), made to keep the total under 2.5 MB. Nobody has judged the sounds by ear on the Pi yet.

1. **Engines (cartridge, Lua tables).** An engine is a `tone()` table and four macros. A macro maps one encoder to one or more registers with a curve. The seven engines of 4.3 use the waves that exist today.
2. **The "wave" pack (cartridge first, then `presets.c`).** About 40 sounds made with the engines: gated snare, tom fill, DX bass, pick bass, Juno-style pad, brass stab, PWM strings, sync-style lead, glass bell, arp pluck, tape noise, vinyl crackle, risers. Each sound has a name, a kind, and four macro defaults. Move the best ones to `src/audio/presets.c` after the tests on both consoles.
3. **Synth work (kernel, each one a separate decision).**
   - A second oscillator per voice (detune, octave, hard sync). It gives the sync lead and thick basses with one voice.
   - A wavetable wave (a 64-frame table, position as a register). It gives moving timbres at a low cost.
   - A master bus insert: tape saturation with wow and flutter, and a master filter for the DJ module.
   - A new register changes `synth.h`, the tone bytes, and the three encoders (rule of the audio area).

Phase 2 has layer 1 and 20 sounds of layer 2 in `carts/tape/src/25_sounds.lua`, and the engine `PCM` with its 24 sounds (above): three FM, three STRING, three PULSE, four CLUSTER, three ORGAN, two VOX, two DIGITAL. Nobody has judged them by ear on the Pi yet.

Measure each kernel item on the Pi Zero W before the decision: the budget is 8 voices in a 64-sample block.

## 9. Project and files

```
carts/tape/
  tape.bme          the project (the source of the build)
  bm.info           name, type: app, targets, resolution
  src/main.lua      loop, input, module switch
  src/ui/           display frame, stroke font, one file per scene
  src/mod/          synth, drum, seq, tape, mix, fx, dj, lib
  src/engine.b      typed hot loops
  sounds/wave.json  the sound pack (bmaudio.py input)
tests/tape/         host tests with bmhost: sequencer timing, voice budget, save and load
```

A song is saved under `/bm/save` first. A song file of its own (and its section layout) is a decision for phase 4, with `docs/BM-FORMAT.md` and `python scripts/consistency.py check`.

**Song file (phase 3).** The cartridge writes the song state with `save()` into save slot 1 (`carts/tape/src/29_save.lua`, as `carts/ko2` does). It is a Lua table with `magic = "BMTAPE"` and `ver = 1`. A reader refuses another magic, another version or a wrong shape, and the demo song starts. The fields: `bpm`, `swing`, `len`, `metro`, `song` (mode), `lin`, `lout` (loop slots); `lvl`, `pan`, `send` (room, echo and chorus of each track), `mute`, `sel` (selected pattern of each track); `snd` (the three synth tracks: sound number, engine, edited flag, octave, sample, macros, envelope, filter); `pads` (start, release, pitch and level of the 12 pads); `fx` (room, echo, chorus); `pats` (a text `step,slot,note,velocity,gate;` for each used pattern only); `rows` (a string of digits for each track that stops at the last used slot). A typical song of four tracks is about 3 KB; a slot holds 32 KB at most.

## 10. Phases

| Phase | Content | Kernel work | Exit test |
|---|---|---|---|
| P0 | This document and the picture of the scenes | none | the user accepts the scope |
| P1 | Skeleton: display frame, nine static scenes, encoders, module switch, `engine.b` with `euclid` and `meter_fall`, Lua path | none | the same frame on bmhost and on the Pi; `.b` result equal to Lua |
| P2 | SYNTH, DRUM, SEQ with `seq_window`; seven engines and 20 sounds | none | a 4-bar pattern gives the same events, on their grid times, at 60, 20 and 7.5 frames a second (`tests/tape/timing_test.py`). bmhost has no frame rate option: the test builds update the sequencer every N-th frame. |
| P3 | TAPE with event clips, MIX, FX, punch-in effects, save and load. Kernel work: none (K4 was not needed). | none | a 4-track song saves, loads, and plays the same (`tests/tape/song_test.py`) |
| P4 | LIB, bank import and export, riff lane, live code | shared bank encoder module | a song made here plays in a game with `music()` |
| P5 | Run-time samples, WAV load, bounce, scope | K1, K2, K3 | freeze of a track gives its voices back; the frozen track sounds the same |
| P6 | DJ: two decks, crossfader, cue, loop, brake | master filter (optional) | two songs mixed for 3 minutes with no late note |
| P7 | Synth work of section 8, layer 3 | per item | measure on the Pi before and after |

## 11. Decisions

Closed (9 Nov 2026):

1. The name is bm Tape.
2. The target is the Pi only. The RGB30 is a later project.
3. The synth keeps 8 voices. Use freeze (section 3.2).
4. `carts/ko2` stays a separate cartridge. bm Tape reuses its recorder logic and its key block. `carts/ko2` has no `.bme`: make one when the code moves.

Open:

1. Kernel APIs K1 to K4: each one gets a work order before its phase.
