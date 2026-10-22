# Audio: output depth and dither

Output depth, dither and in-block ramps. Overview and rules: [audio.md](audio.md).

## Output depth, dither and in-block ramps

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

### Tests

`make test-audio` (`tests/audio/test_audio.c`: SNR per depth, dither noise, DC, room tail, block-rate sidebands) and `make TARGET=rgb30 test-audio` (I2S simulator at 16, 24 and 32 bits).

## Open work (Output Hardware)

- **Audio without HDMI (Analog / I2S on the Pi)**:
  - PWM output on GPIO pins with a passive RC filter (270 Ω + 33 nF) and a stereo jack, to allow listening on monitors without built-in speakers.
  - Alternative support for external I2S audio DACs (e.g. MAX98357A / PCM5102).

