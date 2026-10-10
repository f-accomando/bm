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

## Open work

- **M46 (Audio 2)**: done on the PC; to verify on the Pi and on the RGB30 (Dev > *Audio
  test*, bm Sound HIFI song, F6 "rock beat", *Sound style* 8-bit and back, riff in bm Code,
  bm Sound F7). Checklist in `docs/ROADMAP.md`.
- **M44 step 4**: block audio effects (reverb, echo, filters) on the QPUs, also for nano8.
- Ideas not decided (spunti in `docs/ROADMAP.md`): short PCM samples, TPDF dither before 16
  bits, modulation interpolated inside a 64-sample block.

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
