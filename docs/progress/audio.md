# Audio

Synthesizer, sound bank and player, ready-made instruments, outputs, `riff` and the music
assistant. API: [`docs/API.md`](../API.md) (*Sound*, *Riff*); pattern language:
[`docs/RIFF.md`](../RIFF.md).

## How it is today

### Synthesizer (`src/audio/synth.c`, `synth.h`)
- 8 voices, 48 kHz stereo, mixed in float, then a compressor (`COMP_T`, once per 64-sample
  block) and the soft limiter `synth_limit` to 16 bits.
- Each voice is 32 bytes of registers (layout in `synth.h`; a register at 0 is the plain
  sound), so C, Lua (`apu()`, `tone()`) and the player drive the same sound.
- Waves (`SYNTH_SQUARE` … `SYNTH_ORGAN`, 10): square with duty, triangle, saw, noise, sine,
  metal noise, two-operator FM with feedback, plucked string (Karplus-Strong with a Thiran
  allpass), supersaw (three saws), organ (four drawbars). PolyBLEP/BLAMP band limiting.
- Per voice: white noise mix, drive, TPT state-variable filter (low/band/high pass, notch,
  key tracking) with its own envelope, LFO (cutoff and PWM), exponential ADSR with minimum
  attack/release (`MIN_ATTACK_S`, `MIN_RELEASE_S`: no clicks), pan, sends to the room and
  the echo.
- Room: 8-line FDN reverb run at half the rate (`SYNTH_ROOM_LINES`, `SYNTH_ROOM_LEN`). Echo:
  ping-pong delay up to `SYNTH_ECHO_LEN` samples.
- Retro mode: `SYNTH_RAW` per voice, or every voice with `synth_t.retro` (naive waves, held
  noise, straight envelopes, no room or echo). Turned on by `retro(true)` in a game, by
  `sound=8bit` in `bm/config.txt` or by *Settings > Sound style* (`audio_retro()` in
  `src/audio/audio.c`, two owners: game and user).

### Mixer and player (`src/audio/audio.c`, `player.c`, `player.h`)
- `audio.c` owns the synth, the player, nano8's channels (`n8snd.c`) and the volume; the
  output's interrupt calls `audio_render()`.
- The player runs inside the audio interrupt every 64 samples (a 1.3 ms grid): music and
  effects keep time whatever the frame rate. Notes queued with `play_at()` start there too.
- Sound bank = AUDIO section 6 with signature `BMAU`, version 2 (version 1 still read).
  Format in the comment at the top of `player.h`: 48-byte sounds (up to 32), sound effects
  (64), patterns (64, one track per voice), songs (8). A section 6 without `BMAU` is an old
  MESH (`src/bm/bm.h`).
- A sound effect started without a voice takes a free one, preferring voices the song leaves
  silent, and mutes that music track while it plays.

### Instruments (`src/audio/presets.c`)
42 presets (`P(...)` lines; groups drum, bass, keys, pad, pluck, lead, fx, plus the `chip*`
8-bit ones). `instruments()` lists them; riff and the bank refer to them by name.

### Outputs (`src/audio/audio_out.h`)
- Pi: HDMI audio (`hdmi_audio.c`, `iec958.c`): IEC 958 subframes into the MAI FIFO, a DMA
  channel paced by the HDMI DREQ plays two buffers in a ring.
- RGB30: I2S1 to the RK817 codec (`src/rgb30/rk_audio.c`), 48 kHz 16-bit, no DMA (FIFO
  threshold interrupt); jack/speaker switched by the codec, headphones mono. Volume keys:
  `src/rgb30/volume.c` (bar via `notice.c`, `volume=` saved after 2 s).
- QEMU virt (RGB30 tests): `src/rgb30/virt_audio.c`. `bmhost --wav`: 48 kHz mono.

### riff (`src/script/riff.lua`, `require "riff"`)
TidalCycles/Strudel-style patterns in Lua: mini-notation, chained functions, cycles of 2 s by
default. `R.update()` looks a little ahead and queues notes with `play_at()`. `R.code(text)`
is live code (bm Code: Ctrl+Enter plays and lights the sounding words, Ctrl+. stops);
`R.bake()` turns a pattern into a song for the bank (bm Sound).

### Music assistant (`src/ai/music.c`, `music_net.c`, `lua_music.c`)
87 recipes (`RECIPES`: `beat.*`, `base.*`, `bass.*`, `arp.*`, `melody.*`, `sfx.*`); the words
of a request choose key, mode, tempo, length and instrument, the seed the variant. Melodies
come from a small INT8 network (`MUS_NFEAT` 116 inputs, `MUS_NOUT` 24 outputs, `BMNN` blob in
`music_net.c`) trained by `scripts/trainmusic.py` on `src/ai/melodies.txt`. Lua: `ai.music`
(`lua_music.c`), used by bm Sound's assistant panel (F6).

## Output depth, Dither & In-Block Ramps (2026-10-10)

The mix was already 32-bit float; until now it left the synthesizer as `int16_t` and the Pi's IEC958 packer got `int16 × 256` (24-bit words, 16 bits of information). Now the float goes all the way to the output and is rounded once, at the very end, to the depth the output carries.

### The path
* `synth_mix()` (`src/audio/synth.c`): n frames as floats, 1.0 = full scale, after the room, the echo, the DC blocker, the compressor, the limiter and the master volume. It says what the block is: `SYNTH_MIX_SILENT` (all zeros: voices idle, room and echo quiet, DC blocker settled), `SYNTH_MIX_SOUND`, `SYNTH_MIX_RETRO`.
* `n8snd_mix_float()` (`src/audio/n8snd.c`): nano8 adds its channels into the same floats (returns 1 when it added a sound). `n8snd_mix()` (int16) is unchanged, bit for bit (checked against the old file on a pseudo-random RAM).
* `synth_quantize()` / `synth_quantize16()`: the rounding. `audio_render32(out, n, bits)` (`src/audio/audio.c`, `audio_out.h`) gives words aligned to the left (bit 31 the sign, the bits under the depth 0); `audio_render(int16)` stays as the 16-bit wrapper (QEMU's sink). `synth_render()` (int16) and the new `synth_render32()` are the two halves together. `audio_idle()` without an output only mixes (no rounding).
* The depth: `sound_depth=16|24|32` in `bm/config.txt`, default **24**; Settings > Screen and sound > **Bit depth** on both consoles (`src/kernel/settings.c`, read in `src/kernel/config.c`). `audio_set_depth()`, `audio_depth()`, `audio_depth_out()` (`audio.h`). The monitor's / Settings' sound test prints the depth and the dither.

### The outputs (what each really carries)
* **Pi, HDMI** (`hdmi_audio.c`): IEC958 subframes hold 24 bits. 16 → the low byte 0; 24 → whole; 32 → rounded to 24 *with* the dither rather than cut. `iec958_encode32()` takes the top 24 bits of the words.
* **RGB30, I2S1 → RK817** (`src/rgb30/rk_audio.c`): the slots were already 32 bit clocks wide (BCLK = 64 fs). 16: VDW 16, one FIFO word a frame (left low, right high: as before); 24: VDW 24, a word a sample in its low 24 bits; 32: VDW 32, a whole word. These are Linux's settings for S16_LE / S24_LE / S32_LE (`rockchip_i2s_tdm.c`: `I2S_TXCR_VDW(16/24/32)`, SJM left right-justified), and the codec's `DI2S_RXCR2/TXCR2` 0x0f for 16 bits, **0x17 for both 24 and 32** (`rk817_codec.c`, `rk808.h`): the RK817's DAC converts at most 24 bits. So on neither console does anything convert more than 24 bits: `32` is the float mix on the wire (RGB30) or rounded to 24 (HDMI). **Not tried on the console yet** (no hardware here): only the simulator (`tests/rgb30/audio_sim_test.c`) checks the words, the codec's registers and the timing. If 24 sounds wrong on the RGB30, `sound_depth=16` is the old path exactly.
* At 24 and 32 bits a frame takes two FIFO words: the FIFO (32 words) holds 16 frames instead of 32 and the I2S interrupt comes 6000 times a second instead of 3000. The driver now keeps two chunks: the next one is rendered right after the FIFO was topped up (32 words of margin, 333 µs at 24 bits) instead of when it runs dry (as little as 8 frames, 167 µs). The simulator renders a chunk in 250 µs at 24 and 32 bits with no underrun.
* A new `sound_depth` on the RGB30 changes the I2S and the codec from `audio_out_idle()` (the main loop, once a frame), the DAC muted for those few ms.

### The dither
* TPDF, ±1 LSB of the depth (two uniform numbers in [0, 1) LSB subtracted): the low and the high half of one xorshift32 step a sample (three shift-xors; no division, no multiply in the integer part); the sample is scaled to 1/2^15 (1/2^7) of an LSB in an int32, the dither and the half added, an arithmetic shift is the floor. The clamp is one `SSAT` with the shift folded in on the ARM1176: **24 instructions a sample** for the 16-bit loop (≈0.3% of the Pi Zero's CPU for 48 kHz stereo).
* Silence is exact zeros at every depth: a `SYNTH_MIX_SILENT` block gets no dither. `synth_init()` now starts with the room and the echo already quiet (before, every `audio_reset()` was followed by 100 ms of a "not quiet" room: with the dither, 100 ms of noise at −93 dBFS).
* The chip (`sound=8bit`, `retro(true)`) is untouched: its 16 bits truncated as before, no dither, the same words at every depth.

### In-block ramps (zipper)
* The filter's g (= tan(π fc / fs)) moves from the last block's value to this one's by a constant ratio a sample (the cutoff glides in octaves, as the envelope and the LFO move it), k (resonance) in a straight line. a1 = 1/(1 + g(g + k)) follows with one Newton step a sample from the last one: it approaches from below, so the filter is at most a little more damped than asked and never unstable while 1 + g(g + k) less than doubles in a sample (always true in a whole block: g ∈ [0.0006, 6.4], ratio ≤ 1.16 a sample); a short block that jumps further divides. A change under 1e-5 (a 50th of a cent: the end of a filter envelope's decay) counts as still and uses the old loop.
* The square's width (the LFO's PWM, a new duty) ramps across the block; the LFO is evaluated at the block's end so both ends of every ramp are known.
* ARM1176 (kernel flags, instruction count): the SVF loop is 14 instructions a sample still, 24 while sweeping, plus one `powf` a sweeping voice a block. Host (x86, WSL, noisy): the whole synth +5–10% with 8 voices sweeping. **The Pi's number is to be read on the console**: Settings > Screen and sound > Test the sound prints "synth N us per 256 samples".

### Measured (`make test-audio`, tests/audio/test_audio.c)
| | before | after |
|---|---|---|
| SNR, sine at −1 dBFS, 16 bits (theory with TPDF 92.3) | 16-bit truncation | 92.3 dB |
| SNR, 24 bits (theory 140.5) | 16 bits at most | 140.2 dB |
| SNR, 32 bits (the float as it is) | — | 216 dB |
| a 0.6 LSB sine: the note / its 3rd harmonic | plain rounding 0.71 / 0.42 LSB | 0.60 / 0.005 LSB |
| dither noise, mean | — | 0.5 LSB rms, mean < 0.01 LSB |
| DC after the blocker, a 25% square | — | 1.1e-6 of full scale |
| room tail at 24 bits vs the float | — | ≤ 0.60 LSB rms down to −126 dBFS; 0 after 7.6 s |
| block-rate sidebands, 3 kHz sine, resonant LPF, LFO 8 Hz 4 oct | −36 dBc | −77 dBc |
| the same, LFO 14 Hz, res 200 / res 0 (bench) | −23 / −29 dBc | −53 / −67 dBc |
| filter envelope sweeping over the carrier (FDECAY 40 / acid 15) | −33 / −25 dBc | −75 / −62 dBc |
| duty 25% → 75%: high samples in the next block | 48 of 64 (a step) | 33 of 64 (a ramp) |

`make TARGET=rgb30 test-audio` runs the I2S simulator three times (started at 16, 24 and 32 bits, each changing to the other two and back, a 250 µs render at 24 and 32; the last with an odd FIFO of 31 words, which the driver uses as 30 so frames stay whole). The QEMU tests touched (`tests/qemu_test.py` `test_home_ui`, the Pi's new Bit depth row; `tests/rgb30/qemu_test.py` `test_sound`, one row more to the last) were updated but not run: no working QEMU on the machine of this session.

---

## Pass 2: Samples and Strudel's Effects (2026-10-10)

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
* **The memory**: at most `AU_PCM_MAX` = 2^20 values a bank (2 MiB: **22 s of mono at 48 kHz, 47 s at 22 kHz**), the guard frames counted; a longer bank is refused when read (and by bmaudio.py when packed). With the double buffer, 4 MiB at most. The cartridge itself is on the SD card; its bank (and the samples in it) is in memory too while the game runs, in the format it was stored in.

### The console's drum kit
* Eight drums made at the first `synth_init()` from oscillators, noise and filters (the analog drum machines' recipes: a sine falling in pitch, the six squares of the 808's cymbals, bursts of noise in a band...): nothing recorded, nothing to license. `bd` kick, `sd` snare, `hh` closed and `oh` open hi-hat, `cp` clap, `rim`, `tom`, `cb` cowbell; 48 kHz mono, root C4, under −1 dB (a soft clip, the drum machines' own saturation), the last 10 ms faded: 2.52 s, **242 KB** of memory. Made in 2.6 ms on the PC (the Pi's time is to be read: an estimate is 10–20 ms, once at boot).
* `synth_kit(k)`, `synth_kit_name(k)`, `synth_kit_find("sd")` (128 + k); `tone{sample = "sd"}`, `"kit:3"`.

### The tools
* `scripts/bmaudio.py`: `"samples"` in the JSON, each a `"wav"` (8, 16, 24, 32-bit integers, 32 and 64-bit floats, mono or stereo, `WAVE_FORMAT_EXTENSIBLE` too; its `smpl` chunk gives the root note and the first loop) or `"pcm"` (base64 with `format`, `channels`, `rate`); `"root"` (a note name or number), `"fine"`, `"loop"` (`off`, `fwd`, `pingpong`) with `loop_start`/`loop_end`, `"store"` (another format to keep it in, e.g. `u8` to halve it), `"mono"` (a stereo file mixed). A sound's `"sample"` is a name or number of the bank's, a kit name or `"kit:N"`; `"begin"`, `"reverse"`; `"raw"` (the chip flag, lost by the old unpack) and any other bit of `FLAGS` (`"flags"`) now survive unpack → pack. `unpack -o bank.json` writes the samples as WAV files beside it (in their own format: packed again, the same bytes); without `-o`, base64. `mkbm.py --audio bank.json` finds the WAV files beside the JSON. `show` lists the samples.
* `make wav BANK=...` (`tests/audio/render.c`) reads banks of any size and their samples.
* Lua: the global `SAMPLE` (10); `tone()`/`play()` keys `sample` (a number, a kit name, `"kit:N"`, or a name of the cartridge's bank), `begin` (0..1), `reverse`. The Settings' sound test plays four drums of the kit as the SAMPLE wave.

### The effects (superdough's, on a voice's path)
The registers 29..31 were reserved; `FLAGS` used one bit of eight, `FILTER` three. That was room enough for everything below: **no second table of registers**, the 32 bytes and the 48-byte sound unchanged (the sound's tone already held registers 11..31: a version 2 bank and bm Sound keep the new values as they are; **bm Sound's own list of waves still ends at organ**: it opens a sound of the four new waves as a square, and reads no version 3 bank, until it learns them).

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
* **The chorus**: one delay line of 2048 samples fed by the sends, read twice (left, right) around 14 ms, each tap swinging ±2.5 ms with an LFO (0.8 Hz) a quarter turn from the other's, a straight line between samples; as loud as 0.7 of the send. It sleeps when quiet, as the room and the echo (and the mix is exact zeros again). `synth_chorus(s, rate_hz, depth_ms, wet)` sets it (C only for now). Chosen over a phaser: one line for every voice, two taps a sample, and the pads it is for want width more than a sweep.
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

### Cost
Host (x86, the same C), 8 voices, ns a block of 64 frames, against 8 saws through a resonant filter with a little room (the old sound, 4.2% of the Pi's CPU measured before): mono samples ×1.12, stereo samples ×1.69, pink noise ×1.11, a folding drive ×1.11, the vowel ×1.58, crush and coarse ×1.14, the chorus send ×1.01, **all of it at once on all 8 (and a ducking voice) ×2.14**. Scaled to the Pi: about +0.5 points for samples, +2.4 for vowels on every voice, **≈ +5 points (≈ 9% of the CPU) for everything at once**, under the budget of +8. ARM1176 code (kernel flags): Hermite mono 41 instructions a sample, stereo 70; the vowel's three filters 39 (36 of them float, three independent chains). The Pi's number is to be read on the console: Settings > Screen and sound > Test the sound prints "synth N us per 256 samples".

### Tests
* `make test-audio`: the C tests (every format converted, rounded and clamped; the guards; broken banks; version 2 with a byte 9; Hermite's SNR; the octave; where it starts and backwards; forward and ping-pong loops; a pitch far up a tiny loop; stereo in its channels, the balance, the filter on both; no sample: silent and free; the kit's peaks, spectral centroids and fades; the player's bank to the synthesizer and away; the effects: crush's noise against `2^(1−b)/√12` at 3 and 8 bits, coarse's held samples (3 of 4), each curve's harmonics (odd for the symmetric ones, a second at −28 dBc for the asymmetric, every curve a different sound), the noises' tilt from 250–500 Hz to 4–8 kHz (white +12.0 dB, pink −0.2, brown −11.2; the pink noise mix the same), their loudness, crackle's clicks a second, tremolo all the way down at 15 and nothing without the LFO, the chorus's two sides apart and its silence after, the ducking (−122 dB under a full source, −0.09 dB half a second after), the vowels' formants ("a" against "i": 32 dB apart between 500–700 and 200–300 Hz), the chip deaf to all of it; every preset without a NaN and under 1.0, the new ones heard and under the limiter's bend, the kit's voices free when their sample ends; the tone's keys) and `tests/audio/test_bmaudio.py` (WAV files of every format into a bank, unpack → pack byte for byte both ways, version 2 without samples, the limits, then `bmrender` reads the same bank with the console's parser and plays each sample: its pitch at the root, an octave up, both channels, the loops going on, a one-shot ending, the kit's kick backwards; the effects' keys into their registers and back).
* `make test-music` (the presets by name in the assistant's pieces): passes with the 50. It did not compile with GCC 15 (glibc's new `fdiv()` in `math.h` against `src/ai/music.c`'s own): renamed `floordiv`, nothing else.

## Open work

- **M46 (Audio 2)**: done on the PC; to verify on the Pi and on the RGB30 (Dev > *Audio
  test*, bm Sound HIFI song, F6 "rock beat", *Sound style* 8-bit and back, riff in bm Code,
  bm Sound F7). Checklist in `docs/ROADMAP.md`.
- **M44 step 4**: block audio effects (reverb, echo, filters) on the QPUs, also for nano8.
- Ideas not decided (spunti in `docs/ROADMAP.md`): R28 24-bit WAV, R29 audio entries in the kb
  (to do with the API docs), R30 bit depth and sample meter in bm Sound. Short PCM samples,
  TPDF dither and in-block ramps are done (above).

## Rules (do not break)

- **The bank format lives in three places**, changed together: C `au_parse`
  (`src/audio/player.c`), Lua (`carts/sound/main.lua`, test double `tests/sound/sim.lua`),
  Python `scripts/bmaudio.py` (also read by `scripts/bmres.py`, `scripts/bmmesh.py`). Always
  write version 2; keep reading version 1.
- A new synth register changes `synth.h`, the tone bytes of a sound (24..44 = registers
  11..31) and the three encoders above.
- `synth.c`, `player.c`, `music.c` are portable C (no hardware): the PC tests build them
  as they are. Keep them free of kernel includes.
- ARM1176 has no integer divide and VFP divides are slow: no per-sample `/` or `%` in the
  render loop; precompute reciprocals per block.
- Tests: `make test-audio`, `test-sound`, `test-riff`, `test-music`; RGB30
  `make TARGET=rgb30 test-audio`.
