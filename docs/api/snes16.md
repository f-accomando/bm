# API: SNES-16 native ARM engine

Part of the game API. Index and basics: [API.md](../API.md). The native engine (`src/bm/snes16.c`, bindings in `src/bm/api_snes16.c`) reuses the ARMv6 SIMD PPU (`src/bm/snes_ppu.c`, `src/bm/snes_ppu_arm.S`) and S-DSP (`src/bm/snes_dsp.c`) without 65C816 or SPC700 emulation. The ten hot per-frame functions (`s16_mode`, `s16_scroll`, `s16_oam`, `s16_oam_clear`, `s16_m7`, `s16_hdma_grad`, `s16_hdma_band`, `s16_hdma_m7`, `s16_render`, `s16_sfx`) are also native `BMN_HOST_S16_*` builtins in `bmlc.c` and execute directly in C from `.b` ARMv6 modules.

| Function | Description |
|---|---|
| `s16_init([lines])` | resets the native SNES-16 PPU and S-DSP (`lines`: 224 or 239, default 224), binds the fast C host dispatcher for `.b` modules, and connects `snes_audio_mix` |
| `s16_mode(bgmode, tm, [ts, bright])` | sets BG mode (`0..7`), main/sub layer masks (`1` BG1, `2` BG2, `4` BG3, `8` BG4, `16` OBJ), and brightness (`0..15`, or `-1` force blank) |
| `s16_bg(layer, map_addr, chr_addr, [size_flags])` | sets tilemap byte address, tile data byte address, and map size (`0` 32×32, `1` 64×32, `2` 32×64, `3` 64×64) for BG `0..3` |
| `s16_scroll(layer, hofs, vofs)` | sets horizontal and vertical scroll for BG `0..3` |
| `s16_obj_cfg(size_mode, [base0, gap])` | configures OAM sprite size table (`0..7`), primary tile base (`0x8000` default), and secondary table gap (`0x2000`) |
| `s16_math(cgwsel, cgadsub, [fix_rgb555])` | sets colour math control (`cgwsel`, `cgadsub`) and 15-bit fixed colour |
| `s16_m7(a, b, c, d, [cx, cy, hx, hy, m7sel])` | sets the 8.8 fixed-point Mode 7 affine matrix and origin/scroll registers |
| `s16_tile(byte_addr, bpp, pixels)` | encodes 64 chunky colour indices (string or table) into 2, 4, or 8 bpp SNES planar VRAM at `byte_addr` |
| `s16_map(layer, tx, ty, tile, [pal, prio, hflip, vflip])` | writes one tilemap entry on BG `0..3` |
| `s16_m7_tile(tile_idx, pixels)` | writes an 8×8 Mode 7 chunky tile (64 bytes/entries) into odd VRAM bytes |
| `s16_m7_map(mx, my, tile_idx)` | writes a Mode 7 128×128 tilemap entry |
| `s16_cgram(idx, rgb555_or_r5, [g5, b5])` | sets CGRAM palette entry `0..255` from 15-bit `rgb555` or `(r5, g5, b5)` channels |
| `s16_oam(id, x, y, tile, [pal, prio, hflip, vflip, big, tab])` | configures OAM sprite `0..127` |
| `s16_oam_clear([from_id])` | hides OAM sprites from `from_id` (default 0) to 127 |
| `s16_oam_rot(enable, [first_id])` | enables or disables OAM priority rotation starting at `first_id` |
| `s16_hdma_clear([mask])` | clears native scanline HDMA tables (`mask` bitmask: 1 fix, 2 scroll, 4 Mode 7, 8 mode) |
| `s16_hdma_grad(y0, y1, c0, c1)` | builds a smooth RGB555 fixed-colour gradient across scanlines `y0..y1` |
| `s16_hdma_band(layer, y0, y1, hofs, vofs)` | sets per-scanline scroll values across `y0..y1` for BG `layer` |
| `s16_hdma_m7(y0, y1, cx, cy, angle_q8, z_q8, horizon_y)` | fills perspective Mode 7 matrices across scanlines `y0..y1` |
| `s16_hdma_mode(y0, y1, bgmode, tm, [ts])` | switches BG mode and active layers across scanlines `y0..y1` |
| `s16_render()` | renders the current frame into the internal 256×lines RGB565 buffer |
| `s16_draw(target, [w, h])` | copies or nearest-scales the rendered frame onto a render target |
| `s16_to_sheet([x, y])` | copies the rendered frame into the sprite sheet at `(x, y)` for GPU `sspr` scaling |
| `s16_dsp_pcm(slot, brr_addr, pcm_table, [loop_sample])` | encodes a 16-bit PCM table to SNES BRR in DSP RAM at `brr_addr` and registers sample `slot` |
| `s16_dsp_play(voice, slot, [pitch, adsr1, adsr2, vol_l, vol_r])` | starts S-DSP voice `0..7` on sample `slot` |
| `s16_dsp_stop(voice)` | sends key-off to S-DSP voice `0..7` |
| `s16_dsp_step([samples32k])` | steps the S-DSP by `samples32k` 32 kHz samples (default 534 per 60 Hz frame) |
| `s16_stats([arm_us])` | returns `{ ppu_us, dsp_us, arm_us }` (optionally recording `arm_us`) |
