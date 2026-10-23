# Audio: deep WAV files, bm Sound samples, import

Deep WAV output, bm Sound's samples, the Lua API, WAV and MIDI import (R28-R31, R9, R17). Overview: [audio-samples.md](audio-samples.md).


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
- **WAV and MIDI import from the SD card (R9, R17)**: bm Sound's *Import WAV or MIDI...*
  (menu, Ctrl+W) lists the `.wav` and `.mid` files of `/docs` (`doc_list`, `doc_read`, at most
  4 MiB; a tool needs no permission for the documents) and imports one. The code is pure Lua
  in `carts/sound/main.lua` (`B.wav_parse`, `B.wav_sample`, `B.add_sample`, `B.mid_parse`,
  `B.mid_plan`, `B.mid_import`); no new C API.
  * **WAV**: RIFF chunks in any order, `WAVE_FORMAT_EXTENSIBLE`, a data chunk longer than the
    file read up to the end. 8 and 16-bit PCM, 24 and 32-bit PCM, 32 and 64-bit float; mono or
    stereo; a rate of 1000 Hz or more (lower: refused). The `smpl` chunk gives the root note,
    the fine tune (the pitch fraction in cents) and the first loop (forward or ping-pong). A
    file of 8 or 16 bits that needs no change is copied as it is (8-bit stays 8-bit); the rest
    becomes 16-bit. A rate above 48000 Hz is brought to 48000 Hz; the menu also offers mono,
    mono at 22050 Hz and mono at 11025 Hz. A lower rate is the mean of the input frames that
    the output frame covers (a steady value stays steady; the rate never goes up). The loop
    points scale with the frames. Floats clip at full scale, NaN is silence. The sample goes to
    the end of the bank (the same bytes are reused); it is refused past 64 samples or
    `AU_PCM_MAX`, and the bank stays as it was. The current sound becomes SAMPLE with that
    sample (`MOD1`, start 0).
  * **MIDI**: standard files (format 0 or 1, ticks per quarter note; the time code division
    is refused). Running status, note-on with velocity 0, meta events and system exclusive
    are handled; a file cut short keeps its notes (the message says so). One step is a 16th
    note; a note goes to the nearest step. The first tempo (held between 40 and 255 BPM) and
    the first time signature (the pattern is whole bars, up to 64 steps) are used. A pattern
    has 8 tracks and one note a step on a track: a channel keeps its track when it is free,
    chords go to more tracks, a note over the 8 tracks is dropped (and counted). A note that
    ends before the next one on its track gets an OFF step. Channel 10 plays the kit
    (`B.GM_DRUM`: bd, sd, hh, oh, cp, rim, tom, cb) with one-shots on note 60; the other
    channels get a sound with a wave chosen from the General MIDI program family
    (`B.GM_WAVE`). The velocity becomes the step volume. The song takes the file's name; the
    patterns go to free slots that no song plays (the song is cut to the free slots, and the
    message says so); the sounds are reused when the same ones are in the bank; the current
    sound stands in when no sound slot is free.
  * **Tests**: `make test-sound` (`tests/sound/sim.lua`, 9e and 9f: formats, the sampler chunk,
    rates, mono, limits, the tracks, the tempo, bars, refusals, then the keys Ctrl+W);
    `tests/qemu_test.py -k sound_import` (an SD card with `/docs`, both imports, the bank saved
    as a pack and read with `scripts/bmaudio.py`).
  * **Not done**: MIDI export, tempo changes inside the song, control changes and pitch bend,
    a sample editor (trim, normalise, loop points). Not verified by ear, on the Pi or on the
    RGB30.
