# SIDBOX (`bm/sidbox`)

A SID-style synthesizer and step sequencer with 4 tracks. It is played with the USB keyboard. The code is one file, `bm/sidbox/main.lua`. The project (`.bme`) is `bm/sidbox/sidbox.bme`. Build it with `make build/bm/sidbox.bm build/bm/sidbox.bme`. Test it on the PC with `make test-sidbox`.

## How it is today

- **Tracks.** There are 4 tracks. Each track is a melodic voice or a drum kit (the TYPE row). Each track has 4 patterns (banks A to D) of up to 16 steps. The length of each track is its own, so the tracks can make polymeter. The pad row of each track loops.
- **Steps.** A step is one integer (0 = empty). A melodic step holds the note, a slide bit and an accent bit. A drum step holds a mask of the 8 slots of the kit, so one step can play kick and hat together.
- **Instruments.** 22 SID instruments (every parameter is open) and 39 ready-made sounds of the console (`MEL` in `main.lua`: bass, keys, pads, strings, brass, FX). A SID instrument has: pulse, saw, triangle and noise; the combined waves (P+T, P+S, S+T, P+S+T) with the optional 6581 bitwise mode; ring mod and hard sync with the OSC2 interval and the sweep (the `sid+` wave); ADSR in the 16 steps of the 6581 (`AT` and `DT`); PWM; a filter (LP, BP, HP, notch) with resonance, envelope, wah and key tracking; vibrato; tremolo; portamento; an arpeggiator (11 chords, 4 directions, 5 speeds); drive; bit crush; pan; and sends to reverb, echo and chorus. A ready-made sound takes the same rows as overrides: a row without a value shows `PRESET`.
- **Drums.** 4 kits of 8 slots: SAMPLE (the console's kit), SYNTH, CHIP and SID (noise and falling triangles).
- **Engine.** `play_at` queues the notes of the next 130 ms from `audio_time()`, so the timing does not depend on the frame rate. A track uses `play_at` tag 1 to 4. The live keyboard uses `play()`. The live arpeggiator uses tag 5. Swing delays the odd steps. The echo time follows the tempo.
- **Pages.** F5 SEQ (the pads, live keyboard and help). F6 SOUND (47 rows in 4 columns, an envelope graph and a filter curve). F7 VIS (copper bars, a scope and a spectrum for each track; they follow the notes, they do not read the audio output).
- **Save.** The song is saved in slot 1 four seconds after the last change and in `_exit()`.

## Open work

- Verify on a Pi and on the RGB30 with a USB keyboard: the keys, the latency, the voice count (8 voices: 4 tracks, the live keys and the tails), and the frame time of the VIS page.
- The VIS page draws a model of the sound, not the real output. Use a real level read-out when the runtime gives one.
- There is no song mode (a chain of banks). The bank is chosen by hand with Shift+F1 to F4.
- There is no gamepad input. The RGB30 needs a USB keyboard.

## Rules (do not break)

- Noise is clocked by the note. A noise sound with a high-pass filter is silent at low notes. Play a noise drum at a high note (120 to 127) and add `drive`. `tests/sidbox/check.py` fails if a drum slot is silent.
- A step is an integer. Do not store tables in a pattern. The save has a limit of 32 KB.
- Keep the keys F11 and F12 and the Esc key free. The system uses them.
- Use `play_at` for the sequencer and the arpeggiator. Do not time notes with the frame counter.
