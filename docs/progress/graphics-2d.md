# Graphics: 2D on the Pi Zero W

2D capabilities and 2D open work of the Pi Zero W: GPU light, packed tile pool for `map()`, 2D call costs, sprite stacking, 2D benchmarks and the Pi speed checks. Part of [graphics](graphics.md).

## How it is today

### 2D capabilities on the Pi Zero W (28 Oct 2026)
Yharnam on the Pi went from 34.9 to 57.6 fps with the four items below. Each item says what it does and how a game uses it.

- **GPU light (`gpu3d_light`).** The V3D lights the page that `dark_begin` .. `dark_end` makes. The ARM then only records the lamps.
  - Use: write the normal game code (`fades()`, `dark_begin(ambient)`, `glow()`, `dark_end()`; [light3d.md](../api/light3d.md)). Draw the scene after `dark_begin`.
  - Switch: the hidden key `gpu3d_light` in `bm/config.txt` (default 0), or a cartridge: `gpu3d_opt("light", true)` (Yharnam does it in `_init`; `available` is false on the RGB30). Set it from the PC: `python3 tools/pi.py --dev pizero config gpu3d_light=1`. The kernel reads it at the next start.
  - Cost on Yharnam: light 12.9 ms on the ARM, 1.7 ms with the key on; GPU 1.2 ms a frame.
  - Look: the light is continuous and the lamps add up. The ARM light has levels, the strongest lamp wins, and it has a dither. The two pictures differ on purpose.
  - Limits: 12 lamps for a tile of 64 x 64 pixels (the strongest ones); `dither` and `step` of the call do not change the GPU light. The RGB30 and a failed GPU job use the ARM light.
- **Packed tile pool for `map()`.** No switch and no code in the game. `map()` copies each opaque cell once to a pool of 128 bytes a cell. Use a sheet of any width; keep the cells opaque (no colour key) to use the pool. Yharnam: `map` 2.9 -> 2.0 ms.
- **Cost of each 2D call.** The session report lists "the 2D calls of the draw" (`map`, `spr`, `sspr`, `print`, shapes: count and ms). Use it to find the call that costs most before you change a game.
- **Function profile.** The cart key `yharnam_profile=1` is the model: a cart reads a key with `cart_config` and turns the profiler on for the session report.

What the GPU does not take (measured on the Pi): the copy to the framebuffer (DMA 3.1 ms against 1.8 ms on the ARM), the 2D draws as quads (`gpu2d_only`, slower than the ARM on a big map), and a light job that overlaps ARM work (slower: GPU and ARM share the SDRAM).

## Rules (do not break)
- The light of `dark_end` can be made by the GPU (`gpu3d_light=1`, `gpu3d_light2d` in `src/gpu/gpu3d.c`, `fs_light` in `tools/qpuasm.py`): `glow()` only records the lamp (`rt.lamp`), and at `dark_end` the RAM page is lit in tiles of 64 x 64 pixels, one quad each, whose shader (`fs_light0` .. `fs_light12`: 0, 1, 2, 4, 6, 8 or 12 lamps unrolled) holds the lamps that reach the tile and multiplies each pixel by A + S * W (S = sum of level * (1 - d2 / r2)^2; A the ambient row of `fades()`, W the top row minus A). A tile takes the 12 strongest lamps that reach it; a job too full to hold every tile ends the frame half lit (never seen: 36 tiles are 36 records of 4096). Reason: the ARM's fade and `glow` were 12.9 ms of 22 on Yharnam (Pi). Keep: the shader reads the pixel with the colour load signal (`loadc`), so the job loads the page; the uniforms are in `G.unif_next`, never in the vertex area (the vertex count comes from its size); the shader is single-threaded (`fs_flags`); `v3d_emu.c` knows it by its code (`SH_LIGHT`): change the shader and the emulator together. The ARM path stays the fallback (no GPU, a failed job, the Mali).
- `map()` reads the opaque cells of the sheet from a packed copy (`tile_packed` in `src/bm/gfx16.c`: 128 bytes a cell, one after the other, made when a cell is first drawn). Reason: in a wide sheet the 8 rows of a cell are 8 KB apart and cost 8 cache misses on the Pi; packed, they cost 4 lines in a row (Yharnam on the Pi, 27 Oct: `map` 2.9 -> 2.0 ms, 34.9 -> 36.2 fps). The copy is dropped when `s->version` changes. Write the sheet only through `g16_sheet_set` and `g16_sheet_update_cell` (they change the version), or the copy shows old pixels. A sheet that changes more than 16 times turns the copy off. Reading alpha four pixels at a time in `blit` gave nothing on the Pi (`sspr` 1.7 -> 1.8 ms): the cost of `spr` and `sspr` is the cache misses, not the branches.

## Open work
- Sprite stacking: `stack()`, stack zones (`stack name x y w h n` in the zones file, `zone()` 7th value) and `zstack()` are done (`g16_stack` in `gfx16.c`, CPU only, no memory per stack; Pi speed not measured). `stackv` (the same call with the slices one under the other; zone line `stackv name x y w h n`, bit 0 of byte 27 of the SPRITES zone) is done. The mesh conversion is a host tool: `tools/stack2mesh.py` (see [tools](tools.md)). The bm Pixel Stack page is done (see [tools](tools.md)). Not done: a button for the conversion in bm Pixel and bm Studio, the Pi speed.
- 2D benchmarks: `b3d tests=map8,sprites,stack,fade,light` ([BENCH3D](../BENCH3D.md), *The 2D scenes*). Compare with them before and after `map()` (PR 105), the `-O3` knob (`PI_O3_FILES`, when it lands) and the fade. Not run on the Pi yet.
- 2D API gaps against LÖVE (API-7 to API-11, `docs/ROADMAP.md`): sprite rotation and scale with a transform stack, off-screen render targets, `palt` with alpha and blend modes, 2D physics, noise. Nothing is built. Write a design note for each item before its work order.
- Pi speed, two checks for the user (`g16_map_mask` tile path and the per-file -O3 knob). Both are built and tested on the host and in QEMU; neither is measured on the Pi. Steps:
  1. Baseline: build the kernel from `bm-core` before this PR. Run `b3d tests=mix,heroes_tex,tiny,gpu2d,queue,big_logic,quad_tex` twice, back to back. Baseline numbers: `git show d8b1290b:docs/bench/20261019_bench3d_pi-zero-w_7.3.txt`. Write down `ms` of `gpu2d` and `mix`, and the size of `kernel.img`.
  2. map() change: build this branch with `make`. Run the same `b3d` line twice. The 3D Bench has no test made for the 2D tile layer (`map()` of `gfx16.c`). `gpu2d` and `mix` include layers, but how much `map()` they draw was not checked. The gain of the map() change is NOT measured. To measure it, run a cartridge with a large `map()` layer before and after, and compare the frame time.
  3. -O3 knob: build with `make clean; make PI_O3_FILES="src/bm/gfx16.c src/bm/r3d.c"`. Run the same `b3d` line twice.
  4. Accept the -O3 knob only if `ms` of `gpu2d` and of `mix` drop by at least 3 % and `kernel.img` grows by at most 5 %. Then make the list the default in the `Makefile`. If not, leave `PI_O3_FILES` empty.
  5. The same variable exists in `rgb30.mk`; a dry run shows `-O3` on the listed files, nothing else was tried there.
