# smw: native port of Super Mario World for the RGB30

Personal, educational port. The user owns the cartridge. The ROM, the extracted data, and
the finished cartridge stay on the user's machine and SD card.

## State

- `scripts/smw_extract.py`: reads the Europe Rev 1 ROM (SHA-1 checked). Decompresses the 50
  graphics files (LZ2, table at `$00B9A6`) and writes PNG sheets (2bpp and 3bpp).
- `scripts/smw_capture.py`: runs the ROM in snes9x (`stable-retro`, venv) and dumps VRAM,
  CGRAM, OAM, and WRAM. WRAM `$7B`, `$7D`, `$94`, and `$96` give the physics reference.

## Rules (do not break)

- Keep the ROM, `*.sfc`, `*.smc`, and `carts/smw/{rom,extracted}/` out of git.
- Do not upload the cartridge to the Market. Do not put Nintendo assets in the repository,
  in a PDF, or in a manual.
- Start each change from the `.bme`. Build the `.b16` from it (RGB30 edition, 360x360).
- Measure the fidelity of the physics against the emulator trace, frame by frame.

## Open work

- Run a playable level in the emulator. Record a physics trace (walk, run, jump).
- Extract the level map (Map16 and geometry) from WRAM and ROM.
- Build the sprite atlas of Mario with the right palette.
- Write the movement engine and the collisions in a `.bme` (360x360). Compare with the trace.
- Measure the frame time on the RGB30.
- Write the PDF guide (EN and IT, with `bm-manuals`): how the `.bme` is built, which APIs it
  uses, the game structure, and the asset pipeline. Use original diagrams and code excerpts
  only. Do not include extracted images.
- Add the closure steps: `python scripts/docs_audit.py score`, `python scripts/consistency.py check`.
