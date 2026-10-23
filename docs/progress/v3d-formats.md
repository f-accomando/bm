# V3D: compact attributes and texture formats

Split from [v3d](v3d.md); open work and rules stay there.

- `gpu3d_attr=1` (bm3d 7.1; off by default; hidden key, 3D Bench GPU+VS+A8): compact
  attributes for `GV_BAKED` (5 words a corner, was 9: `vs_baked8`) and `GV_LIT` (10, was 14:
  `vs_lit8`). ARM side `put_mesh_corner` / `pack_abc`; shader side the regfile A unpack
  (`UNPACK_A` in `tools/qpuasm.py`, emulated in `tests/gpu/v3d_emu.c`); `probe_attr` keeps it
  only if the pixels match the floats (a step of RGB565 allowed). `make test-gpu3d`: same
  pixels, 28% fewer attribute words. Pi (probe `compact attributes yes`, 3D Bench GPU+VS+A8
  against GPU+VS+FS2 at 60 fps): spheres 837 vs 808 (+3.5%), heroes 72.3 vs 67.3 (+7%, GPU
  job 8.9 vs 10.3 ms), match 35.3 vs 30.8 (+15%, 9.3 vs 10.5 ms), big 11.4 vs 9.2 (triangles
  differ between the runs). On `herotris` (hero and map, with the queue) there is no gain: 54 056 plain,
  54 013 A8 (23 Oct), ARM 12.0 ms and GPU 7.0 ms in both. Off by default; a played Overbit with
  `gpu3d_attr=1` is not measured (the one played so far was `attr=0`, 1920x1080: 31.7 fps, GPU set the pace
  in 82% of the frames, GPU wait 10.4 ms).
- `gpu3d_tex16=1` (hidden key, 3D Bench GPU+T16): opaque sheets as
  RGB565 (type 4, bm3d 5.2); bm3d 7.2: sheets with transparent texels (cut-outs) as RGBA5551
  (type 3) in the same 64x32 T-tiles (`t16_word`), `cut16_of` / `tex_get`. Sheets have 1-bit
  alpha (`g16_sheet_t.alpha`), so nothing is lost but green's last bit (a step of RGB565 at
  most); the alpha shaders discard the same texels. `probe_cut16` draws a 64x32 texture
  with the alpha shader for each order of the fields (red/blue as RGB565 or swapped, alpha
  low or top bits) and keeps the first that gives every pixel within a step of the type's
  bits (`cut16_near`) and every hole as the cleared page (status `alpha
  textures RGBA5551 RGBA4444`, `gpu3d_tex16()` 2, options line `tex16=2`, `stats.cut16`).
  RGBA4444 (type 2) is probed the same way but unused: no sheet has alpha between 0 and 1.
  `make test-gpu3d`: same holes, red and blue the same, green a step at most
  (`EMU_T16_ALPHA=1` alpha on top: learned; `=2`: off; `EMU_T16_LOSSY=1` widens with half a
  step, as the Pi gives some pixels a step off: still learned).
  Pi (report 20261012-171826): type 3 works, order 0 (RGB565's, alpha low) is the right one
  (5551 1845/2048 exact, 4444 1561/2048, other orders under 45%); the exact check failed it.
  Next report: status `alpha textures RGBA5551 RGBA4444` expected.
- `gpu3d_etc1=1` (bm3d 7.3, off; hidden key; 3D Bench GPU+ETC1): opaque sheets with sides multiples of the ETC1 tile (64x128, or 128x64
  for 4x2 utiles) as ETC1 (type 8), encoded in `tex_get` (`etc1_sheet`, encoder
  `src/gpu/etc1.c`, unit test `make test-etc1`); else the 16-bit / 32-bit chain as before.
  Layout: `etc1_block` (tiles and utile slots from RGB565's `t16_inner`: `etc1_slots`),
  `etc1_put` (byte order). `probe_etc1` draws a 128x256 texture of varied blocks for 8
  candidates (utiles 2x4 then 4x2; bytes BE, words LE, LE, halves swapped) and keeps the first
  whose sampled texels come back (status `ETC1 textures yes (utiles 2x4, bytes BE)`,
  `gpu3d_etc1()`, options line `etc1=1`, `stats.etc1`). Each candidate is also checked
  against the 8 maps of a block's texels (`etc1_at`, `e1_map`: columns/rows reversed,
  transposed); `etc1_sheet` moves each block's texels by the map learned before encoding
  (status adds e.g. `, rows reversed`). Emulator: `EMU_ETC1=0` (2x4, BE), `=1` (4x2, LE: a
  later candidate), `=2` (off), `=3` (2x4, BE, rows reversed as the Pi). `make test-gpu3d`:
  same silhouette, 37 dB PSNR against RGB565, worst channel 9 (also with `=3`).
- Pi (v0.2.5-208): `alpha textures RGBA5551 RGBA4444` (the probe compares within a step of the
  type's bits; the exact compare failed) and `ETC1 textures yes` (type 8 = ETC1, 2x4 utiles,
  bytes BE, each block's rows reversed: `e1_map`). 3D Bench GPU / GPU+T16 / GPU+ETC1 are equal
  within 1-6% (quad_tex 2.20 / 2.35 / 2.19 ns/px, spheres_tex 157 / 158 / 159 fps, heroes_tex
  6.5 all): small sheets sit in the TMU cache, so fewer bytes buy nothing there.
- A failed `probe_cut16` / `probe_etc1` logs to the probes' line (report) `params0`, each
  candidate's right pixels, first 4 texels wanted/got and (5551/4444) an opaque-shader control;
  `etc1_diagnose` adds flat-block, block-map and one-block lines. Tests: `EMU_T16_ALPHA=2`,
  `EMU_ETC1=2`.
