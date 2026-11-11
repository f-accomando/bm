# Audio

Synthesizer, sound bank and player, ready-made instruments, outputs, `riff` and the music
assistant. API: [`docs/api/sound.md`](../api/sound.md), [`docs/api/riff.md`](../api/riff.md); pattern language:
[`docs/RIFF.md`](../RIFF.md).

Detail: [audio-output.md](audio-output.md) (depth, dither, ramps), [audio-samples.md](audio-samples.md)
(samples, effects, presets, WAV).

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
50 presets (`P(...)` lines; groups drum, bass, keys, pad, pluck, lead, fx, plus the `chip*`
8-bit ones; the last 8 from Pass 2 below). `instruments()` lists them; riff and the bank refer to them by name.

### Outputs (`src/audio/audio_out.h`)
- Pi: HDMI audio (`hdmi_audio.c`, `iec958.c`): IEC 958 subframes into the MAI FIFO, a DMA
  channel paced by the HDMI DREQ plays two buffers in a ring.
- RGB30: I2S1 to the RK817 codec (`src/rgb30/rk_audio.c`), 48 kHz 16-bit, no DMA (FIFO
  threshold interrupt); jack/speaker switched by the codec, headphones mono. Volume keys:
  `src/rgb30/volume.c` (bar via `notice.c`, `volume=` saved after 2 s).
- QEMU virt (RGB30 tests): `src/rgb30/virt_audio.c`. `bmhost --wav`: 48 kHz mono
  (`--wav-bits 16|24|32|f32`, below).

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

## Open work

- **M46 (Audio 2)**: done on the PC; to verify on the Pi and on the RGB30 (Dev > *Audio
  test*, bm Sound HIFI song, F6 "rock beat", *Sound style* 8-bit and back, riff in bm Code,
  bm Sound F7, *Samples...*, *Bit depth* 16/24/32). Checklist in `docs/ROADMAP.md`.
- **KO-II (`carts/ko2`)**: a groovebox played with the USB keyboard (12 pads, groups A-D, step
  recorder, 12 effects, LCD, fader, volume knob; F1 shows the keys). It runs at 1280x720 (`screen()`; the GPU scales it) with the real panel's proportions and its own stroke font in the sprite sheet. Esc opens the exit page. Ctrl+Esc freezes it (`_exit()` saves and returns `true`). Done and tested on the PC
  with bmhost. To verify on the Pi and on the RGB30 with a USB keyboard (keys, latency, voices).
- **bm Tape (`carts/tape`)**: one Pi app for synth, sampler, sequencer, 4-track tape, and DJ set. The design and the decisions are in [`docs/design/bm-tape.md`](../design/bm-tape.md). The development state is in [`docs/design/bm-tape-dev.md`](../design/bm-tape-dev.md). Phase 3 is done on the PC (`make test-tape`). Phase 2 gave seven synth engines and 20 sounds, the engine `PCM` with 24 samples rendered on the PC from Felucca-Engines (MIT, `scripts/felpack.py`, 2.35 MB in the bank), a drum kit of 12 pads, four tracks with a 16 to 64 step sequencer, live play on voices 6 and 7, record with quantize, step edit, and the TEMPO page. Phase 3 adds: 8 patterns (A to H) for each track; a song of 64 slots for each track (the TAPE page: clips, loop in and out, `T` writes, Backspace clears); PATTERN and SONG mode (`P`); one `.b` function for both modes (`song_window` in `engine.b`, notes queued with `play_at` from `audio_time()`); MIX LEVEL and MIX PAN (level and pan are the volume and the `pan` of each note; the meters follow the queued notes through `meter_fall`); FX ECHO, ROOM and CHORUS (`echo()`, `reverb()`, `chorus()`, and one send of each effect for each track); punch-in effects on `Z` stutter, `X` half speed, `C` filter sweep, `V` crush (queued notes plus `apu()` on the sounding voices); save and load of the whole song in save slot 1 (`save()` / `saved()`, format `BMTAPE` version 1; on `_exit()`, on Ctrl+S and when the menu leaves; Ctrl+N asks twice for a new song). DJ is still a preview scene. Screen (11 Nov 2026): 1280x720 with a top bar of module tabs and transport, the scene in a 960x360 window, an inspector with the four encoder cells, and a drawn keyboard map that labels each key for the open page (`38_frame.lua`, `39_keys.lua`); the keys follow an Apple Magic Keyboard: modules on Q to I, encoders on 9 0 - = and O P [ ], a piano on Z to / To verify on the Pi: the same keys on a USB keyboard (`;` `'` `P` `Z` `X` `C` `V`, Ctrl+S, Ctrl+N); how the punch-in effects feel (the start of stutter and half speed is 150 ms to 185 ms behind the key, because of the look-ahead); the size and the write time of a save on the SD card; the frame time of the TAPE, MIX and FX scenes at scale 2; the load and the memory of the song state. Verify also, from phase 2: how the 20 sounds and the 24 PCM sounds sound; the load time and the memory of the 2.35 MB bank; the timing by ear at the real frame rate (the PC test checks the queued times at 60, 20 and 7.5 frames a second, not the audio onsets); the frame time of the new scenes at scale 2; the keys and the Esc exit page; the cost of `native_compile` for the larger `engine.b`; whether six sequencer voices are enough for the drums with a bass and a pad. Next: phase 4 (LIB, bank import and export, riff lane, live code).
- **QPU programs, step 4**: block audio effects (reverb, echo, filters) on the QPUs, also for nano8.
- Ideas not decided (spunti in `docs/ROADMAP.md`, R31 on). R28, R29, R30, R31, short PCM samples,
  TPDF dither, in-block ramps and the WAV and MIDI import (bm Sound, Ctrl+W) are done (above and
  in [audio-samples](audio-samples.md)). Left: GPIO audio, MIDI export, PC audio, sample editor,
  sample packs.

## Rules (do not break)

- **bm Tape voices.** The sequencer queues notes with `play_at` and keeps `play_voices(0, 1, 2, 3, 4, 5)`. Live play uses `play(6 or 7, ...)`. Keep this split: a live key must not take a voice of the sequencer.
- **Felucca-Engines in bm Tape.** Compile `third_party/felucca-engines/` (MIT) on the PC only, through `scripts/felpack.py`. The kernel does not link it. Change no file of that folder. Do not copy code of the GPL-3.0 firmware `hugelton/Felucca`. Keep the PCM total of the tape bank at 2.5 MB or less. After a new commit of the engines, make the golden file `tests/tape/felucca_golden.txt` again and review the change.
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
