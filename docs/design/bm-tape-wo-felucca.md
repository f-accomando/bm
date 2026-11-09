# Work order: Felucca-Engines sounds for bm Tape (rendered samples)

Status: ready for the user to accept. No code exists. Parent design: [`bm-tape.md`](bm-tape.md), section 8.

## 1. Objective

Render 24 presets of the MIT synthesis engines of Felucca-Engines on the PC. Put them in the sound bank of `carts/tape` as PCM samples. Add one engine, `PCM`, to bm Tape that plays them. The kernel does not change.

## 2. Sources and licences (read on 9 Nov 2026)

| Source | Licence | Use in this order |
|---|---|---|
| `hugelton/Felucca-Engines`, commit `d0260074ce100a47675814b7f38d61f11176c926` (28 Oct 2026): ANALOG, LOFI, TRIO, WHEEL, NOISE, PHASE, `dsp.c`, `felucca_engine.h`, `tools/gen_engine_tables.py`, `tests/` | MIT | Copy into `third_party/felucca-engines/`. Compile on the PC only. |
| `hugelton/Felucca` (the firmware): drums, effects, reverb, sequencer, formant, grain, user interface, patch tools | GPL-3.0-only | Do not copy. Do not read it for this order. |
| `firmware/src/fm6_core.c` in Felucca (6-operator FM, port of msfa) | Apache-2.0 | Out of scope. It is a candidate for a later kernel order. |
| `firmware/src/phys_dsp.c`, `phys_symp.c` in Felucca | MIT (DaisySP, Rings) | Out of scope. |
| `assets/samples-cc0/` in Felucca (piano, flute, sax, 4.5 MB) | CC0 1.0 | Out of scope. |

The engines are plain C99 with fixed point, no float, and no allocation. They total about 2250 lines. The sample rate is a build-time choice from 32 kHz to 96 kHz. The repository has 37 presets: ANALOG 12, PHASE 6, TRIO 5, WHEEL 5, LOFI 5, NOISE 4.

## 3. Permission needed before the start

The session of 9 Nov 2026 could read the Felucca-Engines code. The permission system refused to compile and run it ("code from external"). The worker needs the permission to compile and run `third_party/felucca-engines/` on the PC. Give this permission before you start the order, or the order stops at step 2 of section 5.

## 4. Fixed choices

1. **Path: render on the PC.** The timbre of a rendered sound is fixed. The encoders of the `PCM` engine change the sample start, the filter, and the drive of the bm voice. They do not change the parameters of the Felucca engine.
2. **The 24 presets.** PHASE: BRASS, ORGAN, STRING, RESO, BELL, WIRE. TRIO: FAT BASS, ARP LEAD, SYNC LEAD, RING BELL, CHIP CHOIR. WHEEL: FULL ORGAN, JAZZ PERC, GOSPEL, SOFT FLUTE, ROCK DRIVE. ANALOG: RAVE, PWM STR, SUB BASS, BRASS. LOFI: PULSE LD, WAVE BASS, WAVE LEAD. NOISE: METAL.
3. **Format.** 32000 Hz, mono, signed 16 bits. One root note per sound: C2 for a bass, C3 for the others. Length: 1.0 s of held note for a bass, a lead, or a pluck; 2.0 s for an organ, a pad, or strings; then the release of the preset, cut at -60 dB, with a 10 ms fade. Peak at -1 dB. No loop in this order: a held key stops at the end of the sample.
4. **Budget.** The PCM total is 2.5 MB or less. The bank has 64 sample slots; this order uses 24.
5. **Names.** A sample name has 8 characters or less (limit of the bank). Use a short engine prefix: `PH`, `TR`, `WH`, `AN`, `LO`, `NO`.
6. **No binary in the source tree, except the project file.** The WAV files are build products in `build/`. `carts/tape/tape.bme` carries the bank, so it grows by about 2.5 MB.
7. **Deterministic build.** The engines use fixed point. The same commit gives the same WAV bytes on every PC. A file of hashes in the repository proves it.

## 5. Files and changes

1. `third_party/felucca-engines/`: `src/`, `tools/gen_engine_tables.py`, `tests/`, `LICENSE`, and a new `README.bm`. The `README.bm` gives the origin URL, the commit, the list of files, the licence, and this sentence: "bm compiles these files on the PC only, to render samples. The kernel does not link them."
2. Run the test of the copied code first: `sh third_party/felucca-engines/tests/run.sh`. It must print that the renders equal `tests/golden.txt`. Stop and report if it fails.
3. `tools/felrender/felrender.c` (new, original code; `tests/fe_host.c` shows the engine interface): a PC program. Arguments: engine, preset number, note, held time in ms, output WAV. It includes `fe_tables.h` made with `--fs 32000`, plays one note, renders until the output stays under -60 dB or 4 s pass, and writes a 16-bit mono WAV.
4. `carts/tape/sounds/felucca.json` (new): the list of the 24 sounds. Each entry: sample name, engine, preset name, root note, held time, kind (`bass`, `keys`, `pad`, `lead`, `bell`, `fx`).
5. `scripts/felpack.py` (new): reads the list, makes the tables, compiles `felrender` with the host compiler, renders each sound into `build/tape/felucca/`, trims the tail, sets the peak, and writes the bank input `build/tape/bank.json` for `scripts/bmaudio.py` (`"samples"` with `"wav"`, `"root"`, `"loop": "off"`). It prints the size of each sample and the total. It stops with an error when the total is over 2.5 MB.
6. `carts/tape/build/build.py`: call `felpack.py`, then give the bank to `scripts/mkbm.py` with `--audio`. Verify first that a bank that holds samples only is valid (`scripts/bmaudio.py`, `au_parse` in `src/audio/player.c`). If it is not, add one minimal sound and report it.
7. `carts/tape/src/25_sounds.lua`: add the engine `PCM`. Base tone: `{ wave = "sample", sample = <name> }`. Macros: START (`begin`, 0 to 0.9), CUTOFF, RESO, DRIVE. Add the 24 sounds to the sound table with their kind. The keys `[` and `]` reach them after the 20 sounds of phase 2.
8. `carts/tape/src/30_scenes_synth.lua`: the `PCM` scene. Draw the sample as a ridge from the peak data of `audio_samples()` (the DRUM PAD scene of phase 2 reads the same data for the kit). Encoder 1 moves a start gate. The sun shows the cutoff as in the other engines.
9. `Makefile`: the `tape` rules depend on `scripts/felpack.py`, `tools/felrender/felrender.c`, `carts/tape/sounds/felucca.json`, and the files of `third_party/felucca-engines/`.
10. `carts/tape/bm.info`: add the credit line for Felucca-Engines (MIT, Hügelton Instruments) in the form that `docs/PROJECT-LAYOUT.md` gives for third-party parts. `README.md`: add Felucca-Engines (MIT, PC tool only) to the line of third-party components.
11. Documents: `docs/design/bm-tape.md` section 8 (the `PCM` engine and the 24 sounds), `docs/progress/audio.md` (the `bm Tape` bullet), `docs/PROJECT-LAYOUT.md` (the `tape` row: the bank and its source). Copy the built `.bme` to `carts/tape/tape.bme`.

## 6. Tests (`make test-tape` runs them)

1. `tests/tape/felucca_test.py` (new):
   - The 24 WAV files exist, are 32000 Hz mono 16-bit, are not silent, and have a peak between -1.5 dB and -0.9 dB.
   - The SHA-256 of each WAV equals the value in `tests/tape/felucca_golden.txt`.
   - The PCM total is 2.5 MB or less.
   - The bank of `build/carts/tape.bm` lists the 24 names (read it with `scripts/bmaudio.py`).
2. `tests/tape/render_test.py`: the `PCM` scene gives a picture that is not blank, each encoder moves the scene, and `audio_samples()` in the cartridge lists 24 samples.
3. `tests/tape/sound_test.py`: each of the 24 sounds, played live in bmhost with `--wav`, is not silent and does not clip.
4. `make test-audio`, `make test-bm`, `make test-bml`, and `make test-riff` pass.
5. `python scripts/docs_audit.py score` reports no new broken link and no orphan. `python scripts/consistency.py check` passes (the cartridge gets an AUDIO section).

## 7. Not in this order

- A kernel port of an engine (PHASE or the 6-operator FM). Measure the cost on the Pi Zero W first.
- Loop points for held notes, and more than one root note per sound.
- The CC0 piano, flute, and sax samples.
- A change of the bank format or of the synth registers.

## 8. Choices for the user

1. The size: 2.5 MB of samples in `carts/tape/tape.bme` in the repository (proposal), or a lower rate (22050 Hz gives about 1.7 MB), or fewer sounds.
2. The 24 presets of section 4: accept them, or name the ones to change.

## 9. Recommended agent

A Sonnet writer with shell access (the `claude` agent type, model Sonnet), as for phases 1 and 2. The orchestrator reruns `make test-tape` and listens to the WAV of three sounds before the commit.
