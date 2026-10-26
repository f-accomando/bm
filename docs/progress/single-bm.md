# One cartridge type: `.bm`

State of 26 Oct 2026. The `.bm` runs on the Pi Zero W and on the RGB30. The header says the kernels (`targets` in `bm.info`, bytes 120..127: [`../BM-FORMAT.md`](../BM-FORMAT.md)). Design: [single-bm-and-adaptive-screen](../design/single-bm-and-adaptive-screen.md). Policy: `docs/spec/compat-policy.toml`.

## Open work

1. Yharnam, Overbit, Titan Clash and Titan EX on the Pi and on the RGB30, same behaviour: not verified on hardware. The user runs them and sends a report. Titan EX stays out of the Market.
2. Rest of the compat check: it reads the header profile only, not the 3D or sound facts of a cart. The catalog line carries only `targets`. The load-time message is text on the serial console only. The menu reads the first 512 bytes on the Pi, so a table of more than 24 sections is cut.
3. Screen: bars for existing games, `lib.layout`. Overbit reads the screen modes of the console (`Quality.modes`); its HUD already sits in the 16:9 page.
4. Input: per-system input chips, the pad cursor for carts that declare `mouse_optional`.
5. A long sound (above the PCM limit) needs a stream from the SD card.
6. The new SDK `.bm` for both kernels (the user makes it). Until then the RGB30 SDK is built into its kernel and the Market has only the Pi SDK.
7. Old files on an SD card stay readable: the kernels read a file of the older edition as made for the Pi and the RGB30. Review that loader at v1.0.0.

## Rules (do not break)

- Every cartridge writer keeps header bytes 120..127 and flag bit 4 (the compatibility block). `project.c` clears only bytes 104..119 (`bm_set_built_from`).
- An old file (bit 4 clear) reads as Pi only.
- A cartridge made for the other device keeps its drawing: the cover changes colour and gets a vignette, with no pill and no row. Play asks the reason as the question, with OK and Cancel.
- The Market has one entry per game id, a `.bm`.
- A game never creates a cartridge file (`webpolicy.c`).
