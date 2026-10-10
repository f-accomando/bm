# Audio Engine & Hardware Output Subsystem

This document details the audio architecture, the Audio 2 hi-fi synthesis engine, sound bank formats, live-coding pattern processing, AI musical generation, and hardware audio drivers in **bm**.

---

## 1. Architectural Evolution: Audio 1 to Audio 2

* **Audio 1 (M10 Baseline)**: A classic 8-bit retro synthesizer featuring 8 voices, naive waveform generation, linear envelope slopes, and raw digital clipping.
* **Audio 2 (M46 Modernization)**: A complete overhaul ([`src/audio/synth.c`](../../src/audio/synth.c)) introducing anti-aliased oscillators, zero-delay feedback resonant filters, physical string modeling, stereo spatialization, studio effects, and live-coding pattern integration while retaining exact cycle-accurate 8-bit backward compatibility via `sound=8bit` or `retro(true)`.

---

## 2. Audio 2 Hi-Fi Synthesis Engine (`src/audio/synth.c`)

The synthesis engine renders 8 independent polyphonic voices mixed into a high-fidelity 48 kHz stereo stream:

### Anti-Aliased Oscillators
Naive digital oscillators produce harsh high-frequency foldback aliasing. Audio 2 implements band-limiting algorithms directly in C:
* **PolyBLEP (Polynomial Band-Limited Step)**: Smooths discontinuities in square, pulse, and sawtooth waveforms.
  * Square wave aliasing at 1760 Hz reduced from **−22 dB to −60 dB**.
  * Sawtooth wave aliasing at 3520 Hz reduced from **−16 dB to −57 dB**.
* **PolyBLAMP (Polynomial Band-Limited Ramp)**: Band-limits triangular and discontinuous derivative waveforms.

### Advanced Synthesis Algorithms
1. **2-Operator FM Synthesis**: Modulator and carrier operators with variable frequency multipliers and feedback loops for bells, electric pianos, and metallic percussion.
2. **Karplus-Strong Plucked String Modeling**: Digital waveguide synthesis utilizing delay lines and Thiran allpass dispersion filters for expressive acoustic guitars, harps, and bass strings.
3. **Supersaw Generation**: Multi-oscillator detuned saw cluster for rich trance leads and brass pads.
4. **Drawbar Organ Model**: Additive harmonic overtone generator simulating classic tonewheel organs.

### Topology-Preserving Transform (TPT) Resonant Filter
Implements zero-delay feedback (ZDF) state-variable filtering:
* Modes: Low-pass (LPF), Band-pass (BPF), High-pass (HPF), and Notch filtering.
* Modulation sources: Dedicated exponential envelope generator, keyboard tracking (pitch scaling), and low-frequency oscillators (LFO).

### Spatial Effects & Master Dynamics
* **Per-Voice Stereo Panning**: Full stereo field placement (`pan = -1.0` to `+1.0`).
* **8-Line Feedback Delay Network (FDN) Reverb**: Simulates rich acoustic room reflections and decay characteristics.
* **Ping-Pong Echo**: Stereo cross-feedback delay synchronized to tempo beats.
* **Master Limiter / Compressor**: Transparent lookahead limiter preventing digital inter-sample clipping on dense polyphonic chords.
* **CPU Consumption**: Highly optimized integer/fixed-point code consumes only **4.2% of the Pi Zero W CPU** with all 8 voices active alongside full FDN reverberation.

---

## 3. Presets & Sound Bank Architecture

### Standard Preset Library (`src/audio/presets.c`)
bm bundles **42 production-ready instrument presets** categorized for immediate use in games:
* **Basses**: Synth Bass, Slap Bass, Acid 303, Sub Bass, Pluck Bass.
* **Leads**: Poly Lead, Chip Lead, Supersaw, Square Lead, Flute.
* **Pads & Keys**: Warm Pad, FM EPiano, Organ, Ambient Strings, Glass Bells.
* **Percussion**: Kick, Snare, Hi-Hat, Tom, Crash, Noise Zap.

### Sound Bank Format (Version 2)
Cartridges store sounds in a structured binary chunk (AUDIO section 6, signature `BMAU`):
* **48-byte Instrument Chunks**: Store oscillator mode, FM parameters, filter cutoff/resonance, envelope times (attack, decay, sustain, release), LFO speed, and effect send levels (reverb, echo).
* **Track Patterns**: Step sequencer patterns specifying note pitch, duration, volume, and instrument IDs.

---

## 4. Hardware Output Drivers

Audio output is abstracted through [`src/audio/audio_out.h`](../../src/audio/audio_out.h):

```
                     ┌──────────────────────────────┐
                     │   8-Voice Synth & Effects    │
                     │ (48 kHz stereo, float mix →  │
                     │  16/24/32-bit, TPDF dither)  │
                     └──────────────┬───────────────┘
                                    │
            ┌───────────────────────┴───────────────────────┐
            ▼                                               ▼
┌──────────────────────────────┐                ┌──────────────────────────────┐
│     Raspberry Pi (HDMI)      │                │    PowKiddy RGB30 (I2S)      │
│  src/audio/hdmi_audio.c      │                │    src/rgb30/rk_audio.c      │
│  - IEC958 subframe packing   │                │  - RK3566 I2S1 TDM interface │
│  - BCM2835 DMA channel FIFO  │                │  - RK817 Audio Codec / Amp   │
│  - HDMI video-audio sync     │                │  - Headphone jack / Speakers │
└──────────────────────────────┘                └──────────────────────────────┘
```

### 1. Raspberry Pi HDMI Audio (`src/audio/hdmi_audio.c`, `src/audio/iec958.c`)
* The BCM2835 transmits digital audio packets interleaved within the HDMI video signal.
* PCM samples are formatted into standard IEC958 / S/PDIF subframes with parity bits and preamble flags: 24-bit words (`iec958_encode32`), so `sound_depth=24` reaches the TV whole (§8).
* BCM2835 DMA feeds circular audio buffers directly into the HDMI audio FIFO at 48,000 Hz stereo.

### 2. PowKiddy RGB30 Audio (`src/rgb30/rk_audio.c`)
* Configures the Rockchip RK3566 I2S1 controller in TDM master mode (12.288 MHz MCLK from GPLL), 16-, 24- or 32-bit samples in 32-bit slots (§8).
* Communicates directly with the onboard Rockchip RK817 power-management and audio codec chip.
* Handles dynamic output routing between internal stereo speakers and the 3.5mm headphone jack.
* Hardware volume keys (+ and −) trigger onscreen feedback overlays (`notice_flash`) and persist settings to `bm/config.txt`.

---

## 5. Live-Coding & AI Music Generation

### `riff` Pattern Language ([`docs/RIFF.md`](../RIFF.md))
An expressive musical sequencing engine embedded in the kernel:
* Inspired by Strudel and TidalCycles, written in Lua for `.bm` cartridges.
* Miniature string notation for rhythmic division (`"bd [sd cp] bd bd"`), note sequences, and euclidean polyrhythms.
* Queues events into the hardware audio ring buffer with sub-millisecond precision (within 1.3 ms).
* Interactive controls: in bm Code, `Ctrl+Enter` plays a pattern and visually highlights active notes in real-time.

### On-Console AI Music Generation (`src/ai/music.c`)
Integrated with the console's creative suite:
* **87 Procedural Accompaniment Recipes**: Automatically constructs basslines, chord progressions, arpeggios, and drum grooves matching user-specified styles (Rock, Synthwave, Lo-Fi, Gothic, Chiptune).
* **Trained INT8 Melody Neural Network**: A lightweight neural network (116 inputs → 64 hidden → 24 outputs) trained via [`scripts/trainmusic.py`](../../scripts/trainmusic.py) that evaluates existing chord structures to generate contextually fitting lead melodies.

---

## 6. Audio Engineering Principles & Hardware Constraints

Future synthesis improvements and optimizations follow a strict hierarchy of impact-to-cost, calibrated specifically for the ARM1176 architecture:

### Priority Interventions (Ordered by Yield / Impact)
1. **Waveform Anti-Aliasing (PolyBLEP / Band-Limited Wavetables)**:
   * Essential to avoid foldback distortion above Nyquist. PolyBLEP remains the optimal choice for ARM1176 due to low polynomial arithmetic overhead, with band-limited wavetables as a scalable alternative for complex waveforms.
2. **High-Precision Mixing & Master-Stage Limiting**:
   * Voices mixed internally in 32-bit floating-point with ample dynamic headroom. Master stage employs soft-knee saturation, DC blocking, and lookahead compression before final quantization.
3. **Per-Voice Resonant Low-Pass Filtering (TPT/ZDF)**:
   * State-variable zero-delay feedback filter providing smooth resonance, filter sweeps, and warmth without digital explosion or instability near Nyquist.
4. **Wavetable & Glide Pitch Interpolation**:
   * Continuous linear or Hermite interpolation on frequency increments across buffer boundaries to eliminate audible pitch stair-stepping during glides and vibrato.
5. **Fluid Envelopes (ADSR) & LFO Modulation**:
   * Sample-accurate or linearly ramped envelope stages and modulation curves across audio blocks (64 samples), preventing zipper noise during rapid filter sweeps and amplitude changes.
6. **Quantization Dithering** (done, §8):
   * Triangular Probability Density Function (TPDF) dithering applied during the final float to 16- or 24-bit PCM conversion to preserve subtle reverb tails and avoid harmonic truncation noise at low amplitudes.
7. **Richer Synthesis Models**:
   * 2-Operator FM synthesis with feedback modulation.
   * Multi-oscillator detuning (supersaw cluster).
   * LFO-modulated Pulse Width Modulation (PWM).
   * Short PCM/WAV sample playback (drum hits, acoustic samples, vocal clips).

### Real Hardware Budget & Constraints (BCM2835 / ARM1176JZF-S)
* **Cycle Budget**: On a single 1.0 GHz core at 48 kHz stereo:
  $$\frac{1\,000\,000\,000\text{ cycles/s}}{48\,000\text{ samples/s}} \approx 20\,833\text{ cycles per sample}$$
  Across 8 polyphonic voices, the theoretical budget is approximately **~2,600 cycles per voice per sample**.
* **No Hardware Integer Division**:
  * The ARMv6 architecture (ARM1176) lacks hardware integer division (`sdiv`/`udiv` instructions were introduced in ARMv7-R/M and ARMv7-A extensions).
  * Every integer `/` or `%` triggers a software subroutine call (`__aeabi_idiv`), consuming 20–80 cycles per invocation.
  * *Design Rule*: Per-sample integer divisions are strictly prohibited in the audio loop. Use precomputed reciprocal multiplication, bit shifts, or fractional phase accumulators.
* **Floating-Point & Fixed-Point Strategy**:
  * VFPv2 hardware floating-point executes `fadds`, `fsubs`, `fmuls`, and `fmacs` efficiently (1–2 cycles), but `fdivs` takes 15–20 cycles.
  * Precalculate all reciprocal values (e.g., `1.0f / rate`, `1.0f / n`, `idt = 1.0f / dt`) outside the per-sample rendering loops.

---

## 7. Retrospective: What Worked, What Failed, Discarded Paths & Next Steps

This section details the evolutionary history of bm's audio architecture, documenting trials, failure post-mortems, and discarded ideas:

### 1. Naive Oscillators vs Band-Limited PolyBLEP
* **Considered**: Audio 1 (M10) generated pure digital steps (0 and 1 for square, linear slopes for saw).
* **What Failed**: Audible high-frequency aliasing foldback above 1.5 kHz (−22 dB on square waves, −16 dB on sawtooth), sounding harsh and metallic on modern speakers.
* **Positive Outcome (Audio 2 / M46)**: Implemented PolyBLEP (step correction) and PolyBLAMP (slope correction). Suppressed aliasing down to **−60 dB** for square and **−57 dB** for saw with negligible CPU cost on ARM1176.
* **Why Wavetables Were Deferred**: Large multi-octave band-limited wavetables consume precious L1 data cache lines (only 16 KiB total on ARM1176). PolyBLEP polynomial math executes purely in registers.

### 2. Linear ADSR vs Exponential RC Envelopes
* **Considered**: Linear delta additions per sample.
* **What Failed**: Produced abrupt step discontinuities and audible clicking/popping during fast attack transients and release cutoffs.
* **Positive Outcome**: Replaced with natural exponential curves modeled after analog RC circuits (`env_coefs`). Added minimum stage bounds (`MIN_ATTACK_S = 0.7 ms`, `MIN_RELEASE_S = 3.0 ms`) to guarantee click-free transitions.

### 3. Full-Rate vs Half-Rate FDN Reverb
* **Considered**: Running the 8-line Feedback Delay Network (FDN) reverberator at the full 48 kHz output rate.
* **What Failed**: Pushed total audio CPU utilization above 8%, leaving less headroom for the 60 fps game loop.
* **Positive Outcome**: Reverberation tails contain little spectral energy above 10 kHz. Running the FDN at half-rate (24 kHz) with linear interpolation between output pairs **halved reverb CPU load** while preserving spatial warmth. Total audio budget dropped to just **4.2% of CPU**.

### 4. Dynamic Range Management & Limiting
* **Considered**: Hard integer clipping at $\pm 32767$ or simple scalar volume division.
* **What Failed**: 8 polyphonic voices playing simultaneous chords caused harsh digital clipping distortion.
* **Positive Outcome**: 32-bit floating-point mixing headroom + master DC-blocking filter (~4 Hz) + lookahead compressor (`COMP_T = -4.4 dB`) + smooth knee limiter (`synth_limit`). Multiple simultaneous loud voices are attenuated transparently before the limiter engages.

### 5. Discarded Architectural Paths
* **Neural Speech / Voice Synthesis (AI.md)**: Discarded. The Pi Zero lacks analog microphone inputs, and real-time neural vocoding requires tens of GFLOPS. Formant synthesis using existing filter oscillators remains the viable path if speech is needed.
* **WAV-Only Soundbanks in Early Versions**: Discarded. Relying on recorded audio samples would bloat `.bm` cartridges and stress the ~100 MB/s SDRAM bus. Procedural synthesis models (FM, Karplus-Strong, Supersaw) provide rich sound with zero memory bandwidth overhead.

### 6. Pending & Alternatives to Explore
* ~~**Short PCM Sample Playback (R9/R25)**~~: done (2026-10-10), §9: the SAMPLE wave, samples in the bank (version 3), the console's drum kit.
* ~~**TPDF Dithering**~~: done (2026-10-10), §8: TPDF at 16 and 24 bits, silence stays exact zeros.
* ~~**In-Block Modulation Interpolation**~~: done (2026-10-10), §8: the filter's g and k and the square's width ramp sample by sample.
* Still open after §8: the release ends at `ENV_DONE` (−60 dB of the voice) with a step; the fast `sine()` (≈0.1% error) puts harmonics near −60 dB (the chip uses it too: a second, finer one would be needed); drive and noise-mix amounts step once a block when their registers change; the LFO itself is evaluated once a block (the ramps make its path piecewise in octaves); `QUIET` stops the room near −126 dBFS (12 dB over 24 bits' step).

---

## 8. Output Depth, Dither & In-Block Ramps (2026-10-10)

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

## 9. Pass 2: Samples and Strudel's Effects (2026-10-10)

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


