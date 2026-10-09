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
                     │  (48 kHz 16-bit Stereo PCM)  │
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
* PCM samples are formatted into standard IEC958 / S/PDIF subframes with parity bits and preamble flags.
* BCM2835 DMA feeds circular audio buffers directly into the HDMI audio FIFO at 48,000 Hz stereo.

### 2. PowKiddy RGB30 Audio (`src/rgb30/rk_audio.c`)
* Configures the Rockchip RK3566 I2S1 controller in TDM master mode (12.288 MHz MCLK from GPLL).
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
