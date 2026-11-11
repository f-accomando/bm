# bm Tape: development state

This file is the development document of the app **bm Tape** (cartridge `carts/tape`). The design is in [`bm-tape.md`](bm-tape.md). The order for the Felucca samples is in [`bm-tape-wo-felucca.md`](bm-tape-wo-felucca.md). The audio rules are in [`../progress/audio.md`](../progress/audio.md).

## 1. Purpose and state

bm Tape is a portable studio in vector art. It has a synth, a 12-pad drum kit, a step sequencer, a 4-track song tape, a mixer, effects and punch-in effects. The cartridge is a Lua app with one typed `.b` file (`engine.b`) that the console compiles to native code.

- Target: Raspberry Pi Zero W only (`targets: pi` in `carts/tape/bm.info`). The RGB30 is not supported.
- Version: 0.3 (`bm.info`). Type: `app`. Input: `keyboard`. Resolution: 1280x720 asked with `screen()` (the header says 640x360; a console that cannot draw 720p keeps 640x360 and the layout halves).
- Cartridge: `carts/tape` builds to `tape.bme` and `tape.bm`. The app is not in `GAMES` yet (`Makefile`).

| Phase | Content | State |
|---|---|---|
| 1 | Skeleton, scenes, encoders, `engine.b` (`euclid`, `meter_fall`) | Done on the PC |
| 2 | SYNTH, DRUM, SEQ, 7 engines, 20 sounds | Done on the PC |
| Felucca samples | Engine `PCM`, 24 sounds rendered on the PC | Done on the PC |
| 3 | TAPE clips, MIX, FX, punch-in effects, save and load | Done on the PC |
| 4 | LIB, bank import and export, riff lane, live code | Not started |

"Done on the PC" means: `make test-tape` passes on bmhost. **Nothing is tested on a real Pi. Nobody has listened to the sounds.** The tests check events, times, levels and file bytes. They do not check how the sound is.

## 2. Source map

`build.py` joins `carts/tape/src/*.lua` in the order of the file names. The files share local names, so the order matters.

| File | Content |
|---|---|
| `00_core.lua` | Palette `C`, drawing helpers (`L`, `PL`, `R`, `O`, `poly`, `T`, `ground`, `sun`), unit-to-pixel scale `S`, USB key table `K`, `poll()`, `pressed`, `released`, `shift`, `ctrl` |
| `10_engine.lua` | Placeholder line `--@ENGINE_B@` inside a long string. `engine.init()` compiles and loads `engine.b`. Wrappers `euclid`, `meter_fall`, `song_window`. `engine.err` holds a failure text |
| `25_sounds.lua` | Engines and macros, 44 sounds (20 synth, 24 PCM), `load_sound`, `retone`, `set_engine`, the 12 drum pads, `pad_tone` |
| `27_seq.lua` | State `S`, step buffers `Q`, song table `SL`, demo song, clock (`tr`), `queue`, `schedule`, `transport_start`, `transport_stop`, `seq_update`. Holds the line `--@TEST_HOOK@` |
| `28_play.lua` | Live keys on voices 6 and 7, record with quantize, step edit (`grid_toggle`), octave, sound change |
| `29_mix.lua` | `MIX` (tone with pan and sends, meters) and `FX` (echo, room, chorus settings) |
| `29_punch.lua` | `PUNCH`: stutter, half speed, filter sweep, crush |
| `29_save.lua` | `SV`: snapshot, restore, `save()` and `saved()` in slot 1, Ctrl+N new song |
| `29_song.lua` | `SONG`: slot get and set, clips, write, clear, pattern select, PATTERN or SONG mode |
| `30_scenes_synth.lua` | SYNTH scenes: engine page (7 engine pictures and PCM), envelope, filter |
| `32_scenes_more.lua` | DRUM pad, SEQ grid and TEMPO scenes |
| `33_scenes_tape.lua` | TAPE scene: two reels and four track lanes |
| `34_scenes_dj.lua` | DJ scene. It stays a PREVIEW. The sequencer does not use it |
| `35_scenes_mix.lua` | MIX LEVEL and MIX PAN scenes: four towers |
| `35_scenes_fx.lua` | FX ECHO, ROOM and CHORUS scenes |
| `36_pages.lua` | The 13 pages, `MODFIRST`, `tab_page`, `module_page` |
| `38_frame.lua` | Screen layout (`layout()`): top bar with module tabs and transport, inspector (page, sister pages, 4 encoder cells, track, voices), hint line |
| `39_keys.lua` | Keyboard map: `ROWS` (the keyboard as data) and `role()` (what each key does on the open page) |
| `40_main.lua` | `_init`, `_update`, `_draw`, `_exit`, keys, encoder repeat, exit page |
| `engine.b` | Typed code: `euclid`, `meter_fall`, `song_window` |
| `../test/hook.lua` | Test hook. Build.py splices it in for `TAPE_TEST` builds only |
| `../sounds/felucca.json` | List of the 24 sounds to render |
| `../build/build.py` | Build script |
| `../bm.info` | Metadata |

## 2.1 The demo song

`load_demo` in `27_seq.lua` makes a multitrack of 8 slots in C minor on the four tracks: drums (kit pads), DX BASS (FM), JUNO PAD (chords) and PULSE LEAD. Pattern A is the verse (slots 1 to 4), pattern B is the lift (slots 5 to 8). The drums start alone, the bass comes in at slot 2, the pad and the lead at slot 3. The loop is 1 to 8, the pad is panned left and the lead right, with a chorus send on the pad and an echo send on the lead. Press `\` (SONG) and Space to hear it. The first start is in PATTERN mode: it loops pattern A of each track.

## 3. Build and test

Commands:

```
make test-tape                           # all tests below
python3 carts/tape/build/build.py [dir]  # default build/tape
python3 scripts/felpack.py [dir]         # render the 24 samples, write bank.json
```

`build.py` does these steps:

1. It joins `src/*.lua` in sorted order.
2. It replaces the line `--@ENGINE_B@` with the text of `src/engine.b`. It stops if `engine.b` holds `]=====]`, or if the line is not there exactly once.
3. It replaces the line `--@TEST_HOOK@` with `test/hook.lua` when the variable `TAPE_TEST=N` is set. Otherwise it drops the line.
4. It writes a blank 16x16 sheet and an 88x88 cover.
5. It calls `scripts/felpack.py` to make `bank.json` and the WAV files.
6. It calls `scripts/mkbm.py` twice, for `tape.bme` and `tape.bm`.

The project file is `carts/tape/tape.bme`. The build writes `build/tape/tape.bme`. I found no make rule that copies it to `carts/tape/`. Copy it by hand after a change.

`felpack.py` makes the engine tables (`--fs 32000`), compiles `tools/felrender/felrender.c` with `third_party/felucca-engines/`, renders one note per sound, and writes the bank input. It stops if the total is over 2.5 MB. The kernel does not link Felucca-Engines.

`TAPE_TEST=N` builds a test cartridge. The sequencer updates every N-th frame. The hook logs each queued note as `TAPE_AT time track note velocity tick level pan room echo chorus`. F9 builds a test song. F10 logs the song state. The Makefile builds N = 1, 3 and 8.

| Test file | What it checks |
|---|---|
| `tests/tape/engine_test.py` | `engine.b` on the stack VM and as Lua against a Python reference. 300 random inputs per function, 500 cases for `song_window` |
| `tests/tape/render_test.py` | Cartridge in bmhost (native code runs there). Pictures of 13 pages and 8 engines, keys, record, TAPE keys, all 44 sounds, broken `engine.b` |
| `tests/tape/timing_test.py` | Queued note times equal grid times within 1.5 ms at 60, 20 and 7.5 frames a second. PATTERN and SONG mode. Stop and start |
| `tests/tape/sound_test.py` | WAV from bmhost: sound in each bar, no clipping, 20 sounds and 24 PCM sounds not silent. Level 0, pan, echo tail, punch-in effects |
| `tests/tape/song_test.py` | Save, load and play give the same events and state. Save by `_exit()`. Refused saves. Ctrl+N asks twice |
| `tests/tape/felucca_test.py` | 24 WAV files: format, peak, SHA-256 against `felucca_golden.txt`, total 2.5 MB or less, names in Lua and in the bank |

After a new commit of the engines, make `felucca_golden.txt` again and review the change.

## 4. Architecture rules

**Voices.** The sequencer uses voices 0 to 5 (`play_voices(0, 1, 2, 3, 4, 5)` in `_init`). Live keys use voices 6 and 7 in turn (`live_voice` in `28_play.lua`). A live key must not take a voice of the sequencer.

**Scheduling.** The clock is `audio_time()`. `transport_start` sets `tr.t0` to `audio_time() + LEAD` (0.06 s). Each frame, `seq_update` asks `song_window` for the ticks up to `audio_time() + LOOK` (0.15 s). `queue` sends each event with `play_at(when, tone, note, gate_ms, volume, 1)`. The frame rate does not move a note. 24 ticks make a beat and 6 ticks make a step. At most 128 events (`CAP`) come from one window. Notes pass 1 as the last argument of `play_at` and metronome clicks pass 2. Stop calls `play_cancel(1)` and `play_cancel(2)`.

**Where code runs.** `engine.b` holds `euclid`, `meter_fall` and `song_window`. They use integers and `vm_buf` arrays. Lua holds the UI state, the sound API calls, `play_at`, the files and the text. Without native code (`engine.mod` is nil), `engine.err` shows on the status strip, the scenes draw, and Space does nothing.

**Data.** The step buffers are flat `i32` arrays: 4 tracks x 8 patterns (A to H) x 64 steps x 4 slots. The song is 64 slots for each track. A slot is one pattern length.

**Save.** `29_save.lua` calls `save(table)` into slot 1 (the default slot). The table has `magic = "BMTAPE"` and `ver = 1`. A reader refuses another magic, another version or a wrong shape, and starts the demo song. A save slot holds 32 KB at most (`docs/api/system.md`). The design gives about 3 KB for a typical song. The save runs on Ctrl+S, on `_exit()`, and when the exit page closes with Enter. The fields are in design section 9.

**Display.** The screen is 1280x720 (`screen(1280, 720)` in `_init`) and halves at 640x360. `layout()` (in `38_frame.lua`) sets `SCL` (pixels per scene unit: 3, or 1.5) and the origin `OY` of the scene. The scenes keep the 320x160 unit grid and fill a window of 960x360 pixels at the left (`clipu(0, 14, 320, 122)` in `_draw`). The top bar is 44 pixels. The inspector is at the right. The keyboard map is below y = 404. A scene must not draw outside units y 14 to 136. Use `X()`, `Y()`, `RF()` and `clipu()` for pixels in a scene, not `px()` alone.

**Colours.** One colour for each encoder: `e1` cyan, `e2` magenta, `e3` amber, `e4` white. Other colours in `C`: `ink`, `grid`, `hot`, `dim`, `mute`, and for the keyboard map `nav`, `pad`, `key`. The palette in code has twelve entries. The design says eight. The comment in `00_core.lua` says seven.

**Bank format.** bm Tape adds no encoder of the bank format. The tape bank holds samples only (see `build.py`). The sounds are `tone()` tables in the Lua source.

## 5. Keys

The cartridge reads USB keys with `rawkeys(true)`. The keys are checked in `40_main.lua`, `28_play.lua`, `29_punch.lua` and `36_pages.lua`.

| Key | Action |
|---|---|
| `9 0 - =` / `O P [ ]` | Encoder 1 to 4: the upper key turns it up, the key below it turns it down. Hold to repeat (after 24 frames, then every 3). Shift gives the fine step. Ignored while Ctrl is held |
| `F1` to `F8` | SYNTH, DRUM, SEQ, TAPE, MIX, FX, DJ, TEMPO |
| `Tab` | Next page of the same module |
| Left, Right | Previous or next of the 13 pages |
| Up, Down | Engine of the synth track (SYNTH ENGINE page only) |
| `1` to `4` | Select track. Track 1 is the drum track |
| Shift + `1` to `4` | Mute or unmute the track |
| `Z S X D C V G B H N J M , L . ; /` | The piano, 17 semitones: the bottom row is the white keys (C D E F G A B C D E), the row above is the black keys. Track 1: pads 1 to 12 on the first 12 keys. Tracks 2 to 4: semitones from the root. The note holds while the key is down |
| `F`, `K` | Octave down or up of the synth track (-3 to 3) |
| Left and right Option | Previous or next of the 44 sounds |
| Space | Play or stop |
| Return | Arm or disarm record (`REC`). In the exit page: save and quit |
| `A` | SEQ GRID: set or remove the brush note. TAPE: write the pattern into the cursor slot |
| Delete | SEQ GRID: clear the step. TAPE: clear the slot. Shift: clear the track (SEQ GRID) or the song row (TAPE) |
| Left and right Command | Previous or next pattern (A to H) of the track |
| `\` | PATTERN or SONG mode |
| `5 6 7 8` | Hold: stutter, half speed, filter sweep, crush |
| Ctrl + `S` | Save (the piano is off while Ctrl is held) |
| Ctrl + `N` | New song. Press twice within 2 s |
| Esc | Exit page (it reaches the cartridge as Start). Esc again: stay. Return: save and quit. Ctrl + Esc: freeze and leave (`_exit` saves) |

The map is made for an Apple Magic Keyboard (ANSI, no numpad). `ROWS` in `39_keys.lua` holds the keyboard as data.

## 6. What is missing

### 6.1 Phase 4 (planned, not started)

From design section 10 and section 7:

- LIB module: sounds, kits, samples and songs. It reads and writes the bank, `.bms` resources and WAV.
- Import and export of the bm Sound bank. The Lua encoder of bm Sound goes into a module that both carts `require`.
- Export a tape as a bank song so a game plays it with `music()`. Import a bank song as 4 tracks.
- Riff lane: a track holds a riff line. `R.play`, `R.voices`, `R.setcpm(bpm / 4)` and `R.bake()`.
- Live code: a SEQ page takes `R.code(text)` from the USB keyboard.
- Exit test: a song made in bm Tape plays in a game with `music()`.
- Open decision: a song file of its own (section layout in `docs/BM-FORMAT.md`, then `python scripts/consistency.py check`).

### 6.2 Kernel gaps

| Gap | Need | State |
|---|---|---|
| K1 | PCM sample made at run time (from a `vm_buf` or a WAV on the SD) | Missing. The sampler cannot record. Samples are read-only at run time |
| K2 | Bounce: render events to a PCM sample off line | Missing. No freeze, so no DJ decks and no overdub |
| K3 | Scope tap: last N output samples and the peak per voice | Missing. The meters follow the queued notes, not the signal. There is no master bus, so no master drive or master level |
| K4 | `line` and `text` in the `.b` host table | Not needed in phase 3. Scenes draw in Lua |
| K5 | `.b` source in a `.bm` | Ready (Lua long string) |
| K6 | More than 8 voices | Measured on the host only. 8 voices stay; use freeze |
| Audio input | Microphone | No hardware on the Pi Zero W or the RGB30 |

Phase 5 (run-time samples, bounce, scope) needs K1, K2 and K3. Phase 6 (DJ) needs K2. Each kernel API gets a work order before its phase.

### 6.3 Known limits of the present code

- Punch-in acts on queued notes. The first note that feels a key is the first one that is not queued yet, up to 150 ms (`LOOK`) ahead. The progress sheet gives 150 to 185 ms before stutter and half speed start. `sound_test.py` accepts 0.19 s.
- Stutter repeats the events of the last 6 ticks (one step, 1/16 note) from the queue position.
- Filter sweep and crush write voice registers with `apu()` while the key is down. No code restores them on release. A note that already sounds keeps the registers until it ends. A new note uses its own tone.
- PCM samples have no loop points (`"loop": "off"` in `felpack.py`) and one root note. A held key stops at the end of the sample.
- The macros of a PCM sound change the bm voice (START, CUTOFF, RESO, DRIVE). They do not change the Felucca timbre.
- No undo. The code has no undo function.
- A step holds 4 notes at most. A fifth note replaces the last slot (`put` in `27_seq.lua`).
- The length of a pattern is one value for all tracks (`S.len`, 16 to 64). Save rounds it to a multiple of 16.
- The save error path is not tested. `SV.save` shows `SAVE <reason>` on the status strip, but no test makes `save()` fail. `song_test.py` tests refused loads only.
- DJ is a preview scene. It makes no sound.
- A drum pad is held 60 ms. A hat does not cut an open hat.
- The design text is old in two places: it says Left and Right go through ten pages (the code has 13), and it lists eight colours (the code has nine).

### 6.4 Not verified on hardware

- Frame time of each scene at scale 2 on the Pi Zero W. The design limit is 4 ms for a scene fill; if it is more, draw the ground once.
- `native_compile` time of `engine.b`.
- The keys on a real USB keyboard, and the Esc exit page.
- Save time and file size on the SD card.
- Load time and memory of the bank (2.35 MB of PCM).
- The sound by ear: 20 synth sounds, 24 PCM sounds, MIX, FX and punch-in effects.
- The timing by ear at the real frame rate. The PC test checks queued times, not audio onsets.
- Whether six sequencer voices are enough for drums with a bass and a pad.

### 6.5 Later candidates

- Kernel port of an engine: PHASE, or the 6-operator FM (`fm6_core.c` of Felucca, Apache-2.0). Measure the cost on the Pi Zero W first.
- RGB30 support. The scenes would draw at `S` = 1.
- More than 8 voices. Host numbers are in design section 3.2 (12 voices: about 1.3 times the cost of 8; 16 voices: about 1.8 times). They are not Pi numbers.
- Synth work of design section 8, layer 3: second oscillator, wavetable wave, master bus insert.

## 7. Next steps

1. Test on the Pi Zero W: build, copy to the SD card with `./easy_install.sh`, and check every item of section 6.4. Write the result into `docs/progress/audio.md`.
2. Fix what the Pi test shows (frame time, compile time, keys).
3. Phase 4: the shared bank encoder module, LIB, import and export, riff lane, live code.
4. Kernel orders: K1, K2 and K3 for phase 5, each with a work order and a Pi measure.
5. Phase 6 (DJ) and phase 7 (synth work) after the kernel orders.
