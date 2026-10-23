# Audio: samples, effects, WAV

Samples in the bank, Strudel effects, presets, deep WAV files, the Lua API. Overview and rules: [audio.md](audio.md).

## Samples and Strudel's effects

What superdough (Strudel's sound engine) has and the console lacked, where it makes sense on an ARM1176: recorded samples first, then its effects on a voice's path. Everything new is off when its register is 0: the old sounds render **bit for bit** as before (checked: every preset at three notes, clean and chip, the demo bank's songs and 22,000 blocks of random registers of the old layout, through `synth_mix` and the 24-bit rounding, against the synthesizer of `a4d79c1`: 100 MB identical).

### Samples (R9, R25)
* **The wave**: `SYNTH_SAMPLE` (10). `MOD1` says which sample (0..127 the bank's, `SYNTH_KIT` 128.. the console's kit), `MOD2` where it starts (x/256 of its length), `SYNTH_FLAG_REVERSE` (`FLAGS` bit 6) plays it backwards from its end. The note against the sample's root note sets its speed (frequency 0: its own speed), so the player's pitch envelope, vibrato, glides and riff's notes work as on any wave. The sample and its start are taken when the note starts; every new note starts it again. A one-shot that ends ends the note: the voice is free at that block's end.
* **The reading**: 4-point, 3rd-order Hermite between 16-bit frames, the position in 32.32 fixed point (no division, no float position), the step ramping across the block like every pitch. Loops: forward or ping-pong over `[loop_start, loop_end)`; the frames after a loop's end are dropped when the bank is read (never heard). Guard frames around every sample (one before, three after: the loop's start after a forward loop's end, the mirror after a ping-pong's) let the interpolation read without bounds checks; one compare a sample keeps the position in range (`ip - lo >= hi - lo`), the rare way out (the end, a loop's turn) is a function call.
* **Stereo samples** stay stereo: a second buffer goes through the same drive, filter (its own state, the same coefficients) and envelope; the place becomes a balance, the sends hear both channels.
* **The chip** (`sound=8bit`, `retro(true)`, `raw`): the nearest frame, 8 bits, mono. A drum kit still sounds like drums in the 8-bit mode, as on a tracker.
* **Measured** (`make test-audio`): a 1 kHz sine recorded at 22.05 kHz, played at 48 kHz: **70.7 dB** SNR (the chip's nearest-frame 8-bit: 21.6 dB); a 48-frame loop of one cycle repeated: 90.3 dB (the 16-bit source's own); a ping-pong over half a cycle: 998 Hz, never stuck; an octave up, twice as fast and over in half the time. ARM1176 (`arm-none-eabi-gcc -O2`, kernel flags): the mono loop is **41 instructions a sample** (4 loads, 5 conversions, 13 float operations, the 64-bit step), the stereo one 70; the state in registers (the slow path's pointers on copies).

### Samples in the bank (version 3)
* `src/audio/player.h` has the layout: the header's byte 9 counts the samples (0..`AU_SAMPLES` 64); after the songs, each sample is 32 bytes (name, frames, rate 1000..192000, format, channels, root, fine tune, loop mode, loop start and end) and its frames. Formats: `AU_PCM_U8` 0, `S16` 1, `S24` 2, `S32` 3 (signed integers, rounded to 16 bits and clamped), `F32` 4 (IEEE floats, ±1.0 full scale; NaN 0). Mono or stereo, interleaved.
* A bank **without samples is still written as version 2** (bmaudio.py), byte for byte as before: bm Sound (which reads only 1 and 2) and every console since read it. The C parser reads 1, 2 and 3; in versions 1 and 2 byte 9 is not read.
* `au_parse()` (unchanged for its callers: the checks of `cart_save`, the Lib viewer) describes the samples and leaves them silent; `au_parse_pcm(data, len, bank, pcm, cap, ...)` converts them into the caller's buffer of `bank->pcm_need` values (16-bit). `audio_bank()` keeps two buffers (`malloc`, one per bank of the double buffer, grown when a bank needs more) and parses twice when it must grow; `player_set_bank()` gives the bank's samples to the synthesizer (`synth_samples()`), `audio_reset()` gives them back after its `synth_init()`.
* **The memory**: at most `AU_PCM_MAX` = 8 Mi values (8,388,608) a bank (16 MiB: **174 s of mono at 48 kHz, 380 s at 22 kHz**), the guard frames counted; a longer bank is refused when read (and by bmaudio.py when packed). With the double buffer, 32 MiB at most. The cartridge itself is on the SD card; its bank (and the samples in it) is in memory too while the game runs, in the format it was stored in.

### The console's drum kit
* Eight drums made at the first `synth_init()` from oscillators, noise and filters (the analog drum machines' recipes: a sine falling in pitch, the six squares of the 808's cymbals, bursts of noise in a band...): nothing recorded, nothing to license. `bd` kick, `sd` snare, `hh` closed and `oh` open hi-hat, `cp` clap, `rim`, `tom`, `cb` cowbell; 48 kHz mono, root C4, under −1 dB (a soft clip, the drum machines' own saturation), the last 10 ms faded: 2.52 s, **242 KB** of memory. Made in 2.6 ms on the PC (the Pi's time is to be read: an estimate is 10–20 ms, once at boot).
* `synth_kit(k)`, `synth_kit_name(k)`, `synth_kit_find("sd")` (128 + k); `tone{sample = "sd"}`, `"kit:3"`.

### The tools
* `scripts/bmaudio.py`: `"samples"` in the JSON, each a `"wav"` (8, 16, 24, 32-bit integers, 32 and 64-bit floats, mono or stereo, `WAVE_FORMAT_EXTENSIBLE` too; its `smpl` chunk gives the root note and the first loop) or `"pcm"` (base64 with `format`, `channels`, `rate`); `"root"` (a note name or number), `"fine"`, `"loop"` (`off`, `fwd`, `pingpong`) with `loop_start`/`loop_end`, `"store"` (another format to keep it in, e.g. `u8` to halve it), `"mono"` (a stereo file mixed). A sound's `"sample"` is a name or number of the bank's, a kit name or `"kit:N"`; `"begin"`, `"reverse"`; `"raw"` (the chip flag, lost by the old unpack) and any other bit of `FLAGS` (`"flags"`) now survive unpack → pack. `unpack -o bank.json` writes the samples as WAV files beside it (in their own format: packed again, the same bytes); without `-o`, base64. `mkbm.py --audio bank.json` finds the WAV files beside the JSON. `show` lists the samples.
* `make wav BANK=...` (`tests/audio/render.c`) reads banks of any size and their samples.
* Lua: the global `SAMPLE` (10); `tone()`/`play()` keys `sample` (a number, a kit name, `"kit:N"`, or a name of the cartridge's bank), `begin` (0..1), `reverse`. The Settings' sound test plays four drums of the kit as the SAMPLE wave.

### The effects (superdough's, on a voice's path)
The registers 29..31 were reserved; `FLAGS` used one bit of eight, `FILTER` three. That was room enough for everything below: **no second table of registers**, the 32 bytes and the 48-byte sound unchanged (the sound's tone already held registers 11..31: a version 2 bank and bm Sound keep the new values as they are; bm Sound knows the four new waves and version 3 banks since R30, below).

| register | bits | what | 0 |
|---|---|---|---|
| `FILTER` (13) | 3-5 | the formants of a vowel after the filter: 1 a, 2 e, 3 i, 4 o, 5 u (`SYNTH_VOWEL_*`) | none |
| `FLAGS` (28) | 1-2 | the noise mix's colour: 1 pink, 2 brown, 3 crackle (`SYNTH_COLOR_*`) | white |
| `FLAGS` (28) | 3-5 | the drive's curve: 1 hard, 2 fold, 3 sine, 4 asym, 5 cubic (`SYNTH_CURVE_*`) | soft (as before) |
| `FLAGS` (28) | 6 | `SYNTH_FLAG_REVERSE`: a sample backwards | forwards |
| `SYNTH_CRUSH` (29) | 0-3 | bits kept, 1..15 (`round(x 2^(b-1)) / 2^(b-1)`, superdough's) | all |
| `SYNTH_CRUSH` (29) | 4-7 | coarse: every value held n + 1 samples (the rate divided) | none |
| `SYNTH_TREMOLO` (30) | 0-3 | tremolo: the LFO (its rate, 24) on the volume, n/15 deep | none |
| `SYNTH_TREMOLO` (30) | 4-7 | ducking: this voice pushes the others down n/15 times its envelope | none |
| `SYNTH_CHORUS` (31) | 0-7 | the send to the chorus | none |

And three waves: `SYNTH_PINK` (11, −3 dB an octave, Kellet's filters), `SYNTH_BROWN` (12, −6 dB, white integrated with a little leak), `SYNTH_CRACKLE` (13, random clicks: `MOD1` × 8 a second, 0: 100). The note does not change them (the filter with `keytrack` can). `SYNTH_WAVES` is 14.

* **Where**: wave → noise mix (its colour) → drive (its curve) → filter → vowel → envelope and volume (× tremolo × ducking, on the volume's own ramp: no zipper, no extra work a sample) → coarse → crush → the place and the sends (room, echo, chorus). Crush after the envelope, as in superdough: a fading note breaks into fewer and fewer steps.
* **The chorus**: one delay line of 2048 samples fed by the sends, read twice (left, right) around 14 ms, each tap swinging ±2.5 ms with an LFO (0.8 Hz) a quarter turn from the other's, a straight line between samples; as loud as 0.7 of the send. It sleeps when quiet, as the room and the echo (and the mix is exact zeros again). `synth_chorus(s, rate_hz, depth_ms, wet)` sets it (Lua: `chorus()`, below). Chosen over a phaser: one line for every voice, two taps a sample, and the pads it is for want width more than a sweep.
* **The ducking**: once a block the loudest source (its envelope × its amount) pushes every other clean voice down; it lets go in about 120 ms. One block late (1.3 ms): nobody hears it. Neither the chip voices nor the room's tail are ducked. A sound effect with `duck` lowers the music under it, a kick with `duck` pumps a pad.
* **The vowels**: three band passes side by side (state variable filters, 0 dB at their peak), at the first three formants of a bass voice (the classic measured table: frequencies, bandwidths ×1.5, levels), summed with their levels; as loud as the sound was (±6 dB).
* **The curves**: hard (flat at ±1), fold (reflected at ±1, again and again), sine (`sin(πx/2)`: soft, then folding), asym (the soft curve with a bias: even harmonics, a tube or a diode), cubic (`1.5x − 0.5x³`). Each scaled so 1 stays 1 (the asymmetric one by its own value at the drive).
* **Nothing new for the chip**: in `retro` mode, and on raw voices, all of it is ignored (the noises are its noise, the sample its own); checked sample for sample with every new register set, every wave.
* **The keys**: `tone()`, `play()`, `play_at()` (`src/audio/presets.c`): `crush` 1..15 bits (0 and 16+: none), `coarse` 1..16, `trem`, `duck`, `chorus` 0..1, `density` 0..1, `vowel` (`"a"`..`"u"`, `""`), `curve` (`"soft"` `"hard"` `"fold"` `"sine"` `"asym"` `"cubic"`), `color` (`"white"` `"pink"` `"brown"` `"crackle"`); `filter` and `keytrack` now keep the vowel's bits. bmaudio.py: the same names in register units (`crush` 0..15, `coarse` 1..16, `trem` and `duck` 0..15, `chorus` and `density` 0..255; any other bit kept as `flags`/`filter_bits`). Lua globals `PINK`, `BROWN`, `CRACKLE`; the lists of wave names are sized by their contents and checked against `SYNTH_WAVES` when compiled (a missing name was a NULL global).
* **Pitch envelope, vibrato**: already there (the sound's `pitch`/`ptime`, `vib`/`vibhz`, the player's), sample-exact enough (the player writes the frequency every 64 samples and the synthesizer ramps between): nothing added.

### The presets (50: 42 + 8)
The player and the 48-byte sound needed nothing new: the sound's tone is registers 11..31, so a bank's sound (or a preset, or a Lua table) carries the sample (`MOD1`), its start (`MOD2`) and every effect. `au_voice_default` and the 42 presets are as they were. The new ones (`src/audio/presets.c`, at the end of the list):
* `kit` (drum): the kit as samples; riff's `kit:N` picks the drum (below).
* `pump` (drum): the kit's kick, ducking the rest by 12/15: a sidechain pump.
* `lush` (pad): the supersaw through the chorus (220), a slow filter, a big room.
* `choir` (pad): a saw through the formants of "a", the chorus, a light vibrato: voices singing aah.
* `solo` (lead): a saw warmed by the asymmetric drive, vibrato (25 cents, 5.5 Hz) and an echo.
* `rhodes` (keys): the FM electric piano with a 4.5 Hz tremolo and a little chorus.
* `bitbass` (bass): a resonant saw bass crushed to 4 bits at a quarter of the rate.
* `vinyl` (fx): crackle (80 clicks a second), a little pink hiss, the highs rounded.

### riff (`src/script/riff.lua`)
* New controls, as superdough's names: `:crush(bits)`, `:coarse(n)`, `:vowel("a")`, `:chorus(x)`, `:trem(x)`, `:duck(x)`, `:begin(x)` (tone()'s units: `crush` 1..15, `coarse` 1..16, the others 0..1). The rest goes through `:tone{curve = "fold", color = "pink", density = .2, reverse = true}`.
* **The kit**: `s "kit:2"` (or `s "kit" :n "<0 2>"`) picks the kit's drum, at its own speed (the note C4 when none is given); `pump` too. The drums by their names, as in Strudel: `s "bd sd hh oh cp cb"` (with `:2` two semitones up, as for every drum); `rim` and `tom` stay the synthesizer's presets (the kit's are `kit:5`, `kit:6`). `R.bake` keeps `kit:0`, `kit:1`... as instruments of their own.
* In C (`src/audio/lua_tone.c`): `play()`, `play_at()`, `tone{s = ...}` and `instrument()` know a sample's sound as `"NAME:N"`, the same on its N-th sample after (wrapping; the kit's eight, or the bank's samples for a sound of the bank), and the kit's drums by name (after the bank's sounds and the presets).
* `make test-riff`: the kit's notes, `kit:2`, the new controls into a voice's registers (read back with `apu()`), `instrument("kit:3")`, bake, and a live-code example with all of them. bmhost did not compile with GCC 15 (`strnlen` is POSIX, not C11): two uses in `src/bm/runtime.c` now go through a local `name_len()`.

### Tests

`make test-audio` (formats, loops, stereo, kit, effects, bank to synthesizer) and `make test-music` (presets by name in the assistant's pieces).

## Deep WAV files, bm Sound's samples, the Lua API

* **R28, WAV files at 24 and 32 bits and in floats**: `bmrender --bits 16|24|32|f32` (`make
  wav BITS=24`), 24 and 32 from `synth_render32`, `f32` the mix itself; `bmhost --wav-bits
  16|24|32|f32`. 16 stays the default, the same files as before. `tests/audio/wavout.h`
  writes the headers for both; `test_bmaudio.py` checks each depth.
* **R30, samples and bit depth in bm Sound**: it reads version 3 banks and writes their
  samples back unchanged (version 2 without samples); knows SAMPLE, PINK, BROWN, CRACKLE; a
  SAMPLE sound shows its sample's meter, start, length and peak in place of the wave (its
  settings SAMPLE and START); the menu's *Samples...* lists the bank's and the kit's and puts
  one into the sound; the menu's footer shows `output N-bit`. `tests/sound/sim.lua`: a
  version 3 bank byte for byte, *Samples...*, undo. Its HELP is 20 lines (it fits the 360-px
  screen again: it was 25 and overflowed).
* **Lua**: `chorus([rate_hz], [depth_ms], [wet])` (`audio_chorus`/`audio_chorus_get`;
  `test-riff` checks it), `audio_depth()` → `bits, out`, `audio_samples(["kit"])` → name,
  frames, rate, channels, root, fine, loop, loop_start, loop_end, peak, meter[32].
* **Docs and kb (R29)**: the four API docs, `docs/RIFF*.md`, kb entries `chorus`,
  `audio_depth`, `audio_samples`, `sample_kit`, `tone_fx`, `riff_kit` (and `tone`, `play`,
  `instruments`, `note` updated), `make ai-model`.
* **Import with its sample (R31)**: bm Sound's *Import from...* copies the sample of a SAMPLE
  sound (`MOD1` below 128) with the sound, to the end of the bank's samples, and renumbers
  `MOD1`. The same bytes already in the bank are reused (no second copy); a song, pattern or
  sound effect brings the samples of all its sounds. The kit's drums (128 and up) need no
  copy. Refused when the bank would pass 64 samples or `AU_PCM_MAX`. The parser keeps each
  sample's offset and size in `sraw` (`o`, `n`). `tests/sound/sim.lua` 9d.
