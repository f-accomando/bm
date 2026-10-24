# Graphics keys: what Settings shows and what is hidden (SYS-9 part 1, 23 Oct)

Split from [v3d](v3d.md).

Rule: the Settings *Screen* page shows only choices a player can judge by sight: *3D of the games* (`gpu3d`) and *3D on the ARM* (`r3d_fast`). It also shows *Menu size* and, on the Pi 1, the CRT, video and GPIO LCD rows. Every other graphics key is hidden: no row, and `config.txt` (or `:set` in the console) still sets it. Fixed values when a hidden key is unset: drawing direct (`draw`), anti-aliasing off (`gpu3d_aa`), textures nearest (`gpu3d_filter`), impostors off (`r3d_impostor`). The overlay key F11 and the `perf` key stay; the *Performance overlay* row stays on the RGB30 only. A default is on only where a Pi number below shows a gain. Change a default only with a number in this file. The user-facing table is in the 3D manual (`api-3d`, *The graphics keys of the console*).

| Key | Default | Shown in | Pi Zero W result (source) |
|---|---|---|---|
| `gpu3d` | GPU | Settings row | hero scene ARM 0.41 sets, GPU 0.56 (22 Oct, herotris table above); score GPU 953, ARM 166 (21 Oct) |
| `gpu3d_vs` | 2 | 3D Bench GPU+VS; hidden | GPU+VS 3.85 sets against GPU 0.56, x6.9 (22 Oct) |
| `gpu3d_queue` | 1 | 3D Bench GPU+VS+Q, +Q2; hidden | 3.85 to 6.74 sets, x1.75 (22 Oct); queue test +11.2% (19 Oct); queue 2 equal to 1 |
| `gpu3d_later` | off | 3D Bench Q2 columns; hidden | no gain: +0.9% triangles, same sets (22 Oct) |
| `gpu3d_aa` | off | 3D Bench GPU+AA; hidden | not measured |
| `gpu3d_filter` | off (nearest) | 3D Bench `bilinear`; hidden | full-screen pass 2.3 ns/px against 1.65 nearest (cost, look changes) |
| `gpu3d_fs2` | on | 3D Bench GPU+FS2; hidden | quad_tex 148 against 121 quads, +22% |
| `gpu3d_2d` | on | 3D Bench GPU+2D; hidden | gpu2d test 2013 sprites against GPU 1886, +6.7%; sprites test 3569+ against ARM 2574 (22 Oct, PERF-BUDGET); on since 24 Oct; `gpu3d_2d=0` turns it off |
| `gpu3d_wc` | off | 3D Bench GPU+WC; hidden | no gain: misses -72%, load 199 against 200 |
| `gpu3d_sort` | off | 3D Bench GPU+VS+S; hidden | not measured on the Pi (emulator: 99 960 against 416 546 shaded pixels) |
| `gpu3d_attr` | off | 3D Bench GPU+VS+A8; hidden | spheres +3.8%, herotris 0% (23 Oct) |
| `gpu3d_tex16`, `gpu3d_etc1` | off | 3D Bench T16, ETC1; hidden | within 1-6% (21 Oct) |
| `gpu3d_merge` | on | hidden | +24% for 100 small pieces, -26% for 10 big ones (23 Oct), threshold in `mg_flush` |
| `gpu3d_qpu` | off | 3D Bench row; hidden | QPU 47 us against ARM 158 us (particles), fade 9.3 against 20.4 ms |
| `r3d_impostor` | off | hidden | 8 px no effect; 16, 32 not measured |
| `r3d_fast` | off | Settings row (both kernels) | not measured |

- The 3D Bench profiles set the flags themselves and print them (options line): unchanged.
- Games choose AA, vertex shader, queue, later and impostors with `gpu3d(on, aa, vs, queue, impostor, later)`, lights with `light3d`, `lamp3d` and the `draw3d` flags, and cel and outline with `cel3d` and flags 256 and 512 (`docs/api/light3d.md`). No new API was needed.
- SYS-9b (not built): a menu of graphics choices in each game and an optional benchmark at the start of a game that picks the values. Do not build an auto tuner before SYS-9b is planned.
