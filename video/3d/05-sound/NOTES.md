# Video 5 (bm Sound): corrections for docs/video/05-sound/shot-list.md and script.md

- Row 101: a new bank's song 0 already has slot 1 = pattern 00. `Enter` there opens the pattern page (and the following keys edit pattern 00). Drop `Enter`; build from `i`.
- Row 115: the "Save an editable copy" question is a kernel modal that reads the keyboard (Enter held), not the typed queue: input line `FRAME keys 0x28` then `keys none`. `type \n` hangs bmhost (wav grows without end). Saved file: /carts/TROPIC.BME (a complete cartridge, project flag on).
- `--wav` is STEREO 48 kHz (pan audible), not mono as script.md limit 7 and input.md say.
- Rows 85-98: `=` on a track header is sound +1, `-` is -1; track n starts on sound n-1 (6 `=` reach KICK..PAD, 5 `-` give APPLE on track 6). Closed.
- Row 2: the bar says "Tropic Trail assets has no sounds yet: make them, then Save puts them in it". Closed.
- Octave: starts at 4 as assumed; effects use `,` once (HIT) so the pattern page starts at OCT 3, then `,` gives 2. Closed.
- `+`/`_` steps are the code's, as the shot list says. All values of the recipe tables reproduced exactly.
- Volume (user rule: peak <= -6 dBFS, RMS < -18 dBFS, music 3-9 dB under the effects). The recipe values were far too loud (peak -0.4 dBFS, music RMS -7 dB). FINAL values, applied in record.py (verify.py exit 0: peak -6.4 dBFS, music RMS -26.9, effects -19.4..-27.8, music 5.3 dB under): every new sound starts at VOLUME 200 and `_` is -16, so only values 200-16n exist (8 mod 16). Raw VOLUME / `_` presses:
  APPLE 56 (x9), CRATE 72 (x8), JUMP 24 (x11), STEP 120 (x5), SPIN 104 (x6), HIT 104 (x6); KICK 8 (x12), SHAKER 8 (x12), BASS 8 (x12), MARIMBA 8 (x12), PAD 8 (x12). Everything else of the recipes unchanged.
  The VOLUME row goes right after WAVE in each block (the goto path is computed). The music is quiet because the 16-step grid floor is 8: the honest alternative, if it sounds too thin on the console, is to turn the master down instead, or to use fewer music tracks; the numbers pass with all six.
  In shot-list rows 24, 46, 56, 68, 78-82 and APPLE/JUMP/HIT (rows 10, 36, 68) replace the VOLUME text and presses with the values above (captions: VOLUME 22% for APPLE ...). The piano previews while placing notes are louder than the effect itself (CRATE peak -5.9 dBFS at 0:55 with VOLUME 88), which is why CRATE is 72.
- Environment: `make` rule for build/sound.bm (and the other editors) passes bm.info as --lua (`$<`), so bmhost fails with "main.lua:1: unexpected symbol near '#'". record.py builds out/sound.bm itself.
- Script.write_input writes CRLF on Windows (a CR is typed as Enter): record.py writes its own LF file. Same risk for the other episodes if run from Windows Python.
