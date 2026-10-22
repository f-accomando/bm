# Triangle throughput on the Pi Zero W

Goal: raise the number of triangles on screen at 60 fps on the Pi Zero W (V3D 250/300 MHz, ARM1176 1 GHz, RGB565, 640x360). This file is an investigation. It changes no code. It ranks the options and gives a plan of PRs.

Sources: [v3d](../progress/v3d.md), [v3d-notes](../progress/v3d-notes.md), [BENCH3D](../BENCH3D.md), [PERFORMANCE](../PERFORMANCE.md), [DRIVERS](../DRIVERS.md), `src/gpu/gpu3d.c`, `src/gpu/v3d.c`, `src/bm/r3d.c`. Old branches: `claude/v3d-compact-tex-attrs` (b8f01430, 1ced7d02), `v3d-async-proposal` (2c383089, 69ef8453, 23b9c7bf), `claude/bench-vs-etc1` (72710c09), `claude/perf-3d-budget-table` (e0afc683), `claude/v3d-closeout` (4123405b, f15ff570), `claude/stages3d-budget` (PR 95: frame budget and quality governor).

Rules for this file: *(M)* = measured on the Pi. *(E)* = estimate from a formula written next to it. `not verified` = nobody checked.

## 1. What the data says

### 1.1 Data used

Pi Zero W, kernel `dev`, bm3d 7.3, ARM 1000 MHz, V3D 300 MHz measured, SDRAM 400 MHz. Reports (branch `reports`): `reports/unknown/20261021-150934_bench3d_pi-zero-w_dev.txt` and the baseline of 19 Oct (`d8b1290b`, `docs/bench/20261019_bench3d_pi-zero-w_7.3.txt`). Default driver: GPU+VS+FS2, **queue off**. Columns: `ms` = middle frame at the last step under 40 ms. `gpu` = `gpu_ms`. `A` = `ms - gpu`. ns per triangle = value / `tris` of that step.

| test | tris | ms | gpu | A = ms-gpu | A ns/tri | GPU ns/tri | ARM instr/tri (no wait) | d-miss/tri | tris at 60 fps |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| spheres (96 faces) | 62976 | 13.0 | 7.8 | 5.2 | 83 | 124 | 35 | 2.95 | 77780 |
| heroes_tex (1536 faces, 16 bones) | 101376 | 14.4 | 8.3 | 6.1 | 60 | 82 | 24 | 2.4 | 113641 |
| heroes (Gouraud) | 101376 | 16.3 | 10.3 | 6.0 | 59 | 102 | 22 | 2.4 | 103012 |
| heroes, A8 | 101376 | 14.9 | 8.8 | 6.1 | 60 | 87 | 24 | 2.4 | 110787 |
| match | 92864 | 15.7 | 10.1 | 5.6 | 60 | 109 | 22 | 2.6 | 102325 |
| match, A8 | 92864 | 14.0 | 9.4 | 4.6 | 50 | 101 | 22 | 2.6 | 111291 |
| mix | 70238 | 15.5 | 9.7 | 5.8 | 83 | 138 | 30 | 3.6 | 78486 |
| tiny (about 4 px) | 78848 | 14.1 | 11.8 | 2.3 | 29 | 150 | 9 | 0.8 | 94355 |
| big (10 080-tri models) | 94752 | 15.2 | 13.7 | 1.5 | 16 | 145 | 15 | 0.35 | 106581 |
| big, A8 | 114912 | 15.4 | 13.9 | 1.5 | 13 | 121 | 12 | 0.3 | 128282 |
| quad_flat (fill) | 312 | 15.9 | 13.7 | 2.2 | - | 1.5 ns/px | - | - | - |
| quad_tex (fill) | 234 | 15.1 | 12.0 | 3.1 | - | 1.9-2.0 ns/px | - | - | - |

The "tris at 60 fps" column counts triangles sent, before the V3D culls back faces. The bench does not separate the two (`drawn` is the count after the ARM clip, not after the hardware cull; `not verified`).

### 1.2 Findings

1. **Without the queue, the frame is the sum.** `ms = A + gpu` fits every row (mix 5.8 + 9.7 = 15.5). The ARM waits for the GPU job (27-33% of its instructions are waiting loops in `mix`, `heroes_tex`, `spheres`). The default games driver has `gpu3d_queue` off (`runtime.c`: the option is read as 0 unless `config.txt` sets 1 or 2).
2. **The two sides are of the same size.** ARM about 50-85 ns a triangle, GPU about 80-150 ns a triangle (6.7-12 million triangles a second). So the frame holds 70-110 k triangles with no game logic. This is far more than the 7 142 flat spheres of the first M33 table.
3. **A model fits the Pi.** With `F` = fixed cost of a frame (copy, flip, clear: 2.2-3.1 ms from `quad_*`, `tiny`, `big` where `A` is small), `a` = ARM ns a triangle, `g` = GPU ns a triangle, `T` = 16.67 ms:
   - queue off: `N = (T - F) / (a + g)`. Check on `mix`: `a = (5.8-2.5)/70238 = 47 ns`, `g = 138 ns`: `N = 14.2 ms / 185 ns = 77 k`. Measured: 78.5 k.
   - queue on: `N = min((T - F - L) / a, (T - F) / g)`, with `L` = the ARM time of the game logic that runs while the GPU draws.
4. **The synthetic bench is far from a game.** Overbit (played, 640x360, `r3d_impostor` 8): 8 588 triangles, 57.7 fps, GPU 7.8 ms a frame (v3d-notes). That is 908 ns of GPU per triangle against 80-150 ns in the bench. The game's GPU time is not setup bound. It is fill, per-draw, lit-textured shaders or 2D. The cause is `not verified`. A new bench scene (section 4) must show it before any work on setup cost.
5. **Game logic eats the ARM budget.** Test `queue` (4 M instructions of logic): 47 232 triangles at 11.0 ms against 14.5 ms without the queue (baseline of 19 Oct). Test `big_logic`: 54 k triangles, ARM-bound.
6. **Queue hides only part of the GPU time.** In test `queue` the GPU takes 6.6 ms and the queue hides 3.5 ms of it (53%). Why the rest stays is `not verified`. Possible causes: the job starts only at the end of the 3D calls; the ARM and the V3D share the SDRAM bus; the ARM waits in `flush3d` before the next draw.
7. **Cache misses do not limit the ARM.** `GPU+WC` (job memory uncached) cuts d-misses by 72% (228 848 to 64 352, `spheres`) and the load stays 199 against 200. So cutting misses gives nothing. Do not work on it.
8. **The second job block and triple buffering give nothing.** Queue 1 and queue 2 give the same load (651.4-652.4 and 651.6-652.7, v3d-notes). Decision of 12 Oct: no interrupts, no triple buffering.
9. **Textures are not a lever.** T16 and ETC1 equal the plain path within 1-6% (quad_tex 2.20 / 2.35 / 2.19 ns/px; heroes_tex 74.6 against 74.6). Small sheets sit in the TMU cache.
10. **The binner has room.** `pool_kb` is 74-398 KiB for 63-115 k triangles (about 3.5 B a triangle, *E*, if the column is the tile-list use). The pool is 4 MiB plus 2 MiB overflow. `ovf_kb` is 0 in every row. The control list `JOB_BCL` is 256 KiB. An indexed group is one command, so the list grows with groups and draws, not with triangles.
11. **A draw call costs 3.7-12.6 us.** `draws`: 1 023 cubes at 12.9 ms, ARM busy 3.3 M instructions, so 3.2 k instructions and 12.6 us a call. `clip`: 3 680 pieces at 13.6 ms, so 3.7 us a piece (the ARM culls 77% of 147 200 triangles with a bounding sphere).

### 1.3 Bytes per vertex (code)

The vertex shader reads one record per corner. Words of 4 bytes.

| shader | words/corner | bytes/triangle (3 corners) | with `gpu3d_attr` |
|---|---:|---:|---:|
| `GV_BAKED` | 9 | 108 | 5 words, 60 B (-44%) |
| `GV_LIT` | 14 | 168 | 10 words, 120 B (-29%) |
| `GV_TEX_RGB` | 8 | 96 | 6 words, 72 B (-25%) |
| `GV_LIT_TEX` | 12 | 144 | 12 (no change) |
| `GV_LIT_TEX2` | 11 | 132 | 11 (no change) |

The ARM-transformed path writes `VSTRIDE(n) = 12 + 4n` bytes a vertex (24-44 B). Vertex reuse exists: indexed meshes, 16-entry FIFO model, `vcache_order` (`gpu3d_sort`): a grid goes from 2.96 to 0.69 shaded vertices a triangle (DRIVERS 5.6). Memory of a job: `JOB_SECTIONS` = TSDA 96 KiB + ALLOC 4 MiB + OVERFLOW 2 MiB + ZBUF 8 MiB + BCL 256 KiB + RCL 128 KiB + RECS 64 KiB + UNIF 256 KiB + CODE 40 KiB + VERTS 4 MiB + probe buffers, rounded to 1 MiB sections (about 19 MiB). The second block `JOB2_BLOCK` is about 11 MiB. A third block would cost about 11 MiB more.

The compact attributes of textured faces (b8f01430) gave `vs_lit_tex8` and half-float uv. Commit 1ced7d02 removed the half uv, because the lit textures are sampled nearest and the pixels changed. What is left: `vs_tex_rgb8` only (8 to 6 words). The branch is not in `bm-core`. The lit textured shaders, which the heroes use, have no compact form.

## 2. Candidates, ranked

Gain = more triangles at 60 fps in the tests the option helps. Ranges are *E* unless marked *(M)*.

### Opt 1: queue on by default (async submit)

- Mechanism: the end of the frame's 3D starts on the GPU. The next `_update` runs while it draws. Exists as `gpu3d_queue=1` (bm3d 4.1). The old proposal `v3d-async-proposal` is this feature (and it is already built). Triple buffering and `v3d_async.c`: skip (finding 8).
- Helps: tests with GPU time and ARM time of the same size: `mix`, `match`, `heroes`, `spheres`. Little where one side is 4 times the other (`tiny`, `big`, `quad_*`: `A` is 1.5-3 ms, hidden at most).
- Gain: +11% *(M)* in test `queue` (logic scene). Formula for a scene with no logic: `N_on / N_off = (a + g) / max(a, g)` at `F` small. `mix`: (47 + 138) / 138 = 1.34 (+34%). `heroes_tex`: a = (6.1 - 2.5) ms / 101 376 = 36 ns, g = 82 ns: +44%. `spheres`: a = 43 ns, g = 124 ns: +35%. With the logic `L` of a game (7-9 ms of ARM for 4 M instructions, *E*), `N` is capped by the ARM: `(T - F - L) / a`. Range: **+10% to +45%**. Finding 6 says only half of the GPU time was hidden in test `queue`: use the low end until a scene with logic is measured.
- Cost: 0 bytes of RAM, 0 lines of code to turn on; about 10 lines to make the Settings default. Risk: late frames; some carts draw after `flush3d`; `rt.no_early`. Overbit with `gpu3d_queue` 0 and 1 is not measured.
- QEMU test: `make test-queue2d` (same pictures with and without the queue). Extra: add a `queue` row in `tests/qemu_test.py` if none exists (`not verified`).
- Measure on the Pi: new scene (section 4) with `gpu3d_queue` 0 and 1 and 2: triangles at 60 fps, `ms`, `gpu_ms`, `wait_instr`, `stat(18)`.

### Opt 2: compact attributes on, then for lit textured faces

- Mechanism: `gpu3d_attr=1` packs colour and light bytes: 28% fewer attribute words. The GPU reads and the ARM writes fewer words. Stage 2b: lit textured faces (`GV_LIT_TEX`, the hero shader) also: uv as 16-bit fixed (the sheets are at most 512 and sampled nearest) and normals or light as bytes.
- Helps: GPU-bound mesh tests (`big`, `match`, `heroes`); ARM side a little (fewer uncached writes).
- Gain *(M)*, A8 against GPU+VS+FS2 at 60 fps: spheres +3.5%, heroes +7%, match +15% (n60 35.3 against 30.8 in the first run, 34.0 against 32.1 in the last), big +23% (9.2 to 11.3 models; the triangle counts differ). Textured heroes: 0 today. Stage 2b *(E)*: `GV_LIT_TEX` 12 to 10 words (-17%). Taking the heroes' ratio (-28% words gave -14% GPU job time): -17% words gives -8% GPU time, so **+3% to +6%** in `heroes_tex`, `mix`.
- Cost: 0 RAM (less). Stage 2a: about 5 lines (default on if the probe passes). Stage 2b: about 100-150 lines (shader in `tools/qpuasm.py`, `put_mesh_corner`, emulator unpack), `JOB_CODE` 40 to 48 KiB.
- Risk: pixel mismatch (the reason 1ced7d02 dropped half-uv). Medium. The probe `probe_attr` keeps the path only if pixels match.
- QEMU: `make test-gpu3d` (same pixels, fewer words). Pi: bench GPU+VS+A8 against GPU+VS+FS2 in `heroes_tex`, `mix`, the new scene; Overbit session with `attr=1`. The decision is the user's (v3d.md).

### Opt 3: merge draws of one model, and static map chunks

- Mechanism: one `draw3d` per model or per map chunk, not per piece. One CL group and one shader record per material. Done at load time, by `bm-forge`, or in `r3d` for static pieces with the same texture.
- Helps: ARM-bound scenes with many calls (hero + map: tens to hundreds of calls).
- Gain: saved ms = `calls_removed x 3.7-12.6 us`. 100 map pieces merged to 10: 0.3-1.1 ms of 16.7 = **+2% to +7%** triangles when ARM bound (*E*). More when the logic fills the ARM.
- Cost: no RAM; a merged mesh keeps a bounding sphere, so culling is coarser (see Opt 4). 50-200 lines in the pipeline tools. Risk low.
- QEMU: host test with `draws` scene: same pixels, fewer calls. Pi: test `draws` (1 413 cubes at 60 fps) and the new scene with 1, 10, 100 map pieces.

### Opt 4: culling and LOD on the ARM (cheap early outs)

- Mechanism: bounding sphere per piece against the 4 screen edges and the near plane (exists, M33); distance LOD per face (`R3D_LOD_SHOWS`, 4 levels, exists); sector or portal culling for a map; far heroes drawn with fewer triangles. A culled piece costs about 2.5 us (`clip` row: `A` = 9.1 ms for 3 680 pieces); a drawn triangle costs `a + g` = 140-230 ns.
- Helps: any scene where triangles are sent and not seen. The gain depends on the content, not on the driver.
- Gain: `N_visible_budget` stays the same. The map the player can use grows by `1 / visible_fraction`. A map with 30% visible after culling holds 3.3 times the triangles in the world. For the triangles on screen, the gain is 0%. State this clearly: Opt 4 raises the world, not the screen count.
- Cost: 0 RAM, 50-300 lines in `r3d.c` and tools. Risk low. QEMU: `make test-impostor`, `clip` scene counts. Pi: `clip` and the new scene with `fraction_visible` 100%, 50%, 25%.

### Opt 5: resolution scaling (R41) and fill options

- Mechanism: render at 480x270 or 320x180 and let the scaler enlarge. Pixel count -44% or -75%. Dither off, early-z front to back (`gpu3d_sort`, exists) cut fill.
- Helps: fill-bound scenes: `quad_*`, 1080p, and the played Overbit (908 ns a triangle suggests fill or shaders, finding 4). Does not help `tiny` or `big`.
- Gain: `new_gpu = g_setup + g_fill x px_ratio`. If fill is 50% of the GPU time: 480x270 gives GPU -22%, so **0% to +40%** by scene (*E*). Quality cost: the user's "RGB565 stays" and 640x360 target are untouched by default; this is an option.
- Cost: 0 RAM (less). About 50 lines in `runtime.c`. Risk low; visible quality. Pi: new scene at 640x360, 480x270, 320x180: `gpu_ms` split into fill and setup.

### Opt 6: impostors for far heroes (R42)

- Mechanism: a far hero is one or two quads (`r3d_impostor=N`, `imp_threshold`). Threshold 8 had no effect at 640x360 (57.7 fps, 8 588 triangles). Values 16 and 32 are not measured.
- Helps: scenes with many heroes at a distance. Not the hero close to the camera, not the map.
- Gain: per far hero 1 536 triangles to 2. For `k` far heroes of `n` total: triangles saved = `k x 1 534`. In a crowd with 10 heroes, 6 far: -9 200 triangles (*E*). It raises the count of heroes, not the triangles on screen.
- Cost: texture RAM for the photo (small). Code exists. Risk: popping. Pi: `res:640/gpu:arm+vs/q:4/ring:1/far:60` with 0 / 8 / 16 / 32.

### Opt 7: split the frame into two jobs so bin and render overlap

- Mechanism: the V3D bins job N+1 on CT0 while it renders job N on CT1. Today `v3d_start` rejects a second job and `job_wait` runs before it. Chaining needs two CLs in flight and the job memory of `JOB2`. A frame split in two (top and bottom half) would let bin(2) overlap render(1).
- Helps: only GPU-bound scenes where binning is a large part of `gpu_ms` (`tiny`, `big`).
- Gain: at most `min(bin, render of the other half)`. The split between bin and render is not reported (`bin_us`, `rnd_us` exist in `v3d_wait`, the bench does not print them). If bin is 30% of 11.8 ms: -1.8 ms at best, **0% to +15%** in `tiny`, `big` (*E*, no data). v3d.md already says: "only if a measurement shows the GPU idle between jobs". Queue 1 equals queue 2 (finding 8), so there is no sign of idle time.
- Cost: about 11 MiB RAM (job block), 150-300 lines in `v3d.c` and `gpu3d.c`. Risk high (hang, lost depth between halves). Pi: print `bin_us`, `rnd_us` and the idle time before any code.

### Opt 8: 16-bit positions (smaller vertex)

- Mechanism: positions as 16-bit fixed, scaled per mesh. The coordinate shader reads a separate x y z stream. Position is 3 of 5 words in `vs_baked8`.
- Gain: `vs_baked8` 5 to 4 words (-20%): about -10% GPU time on A8 scenes, so **+2% to +5%** (*E*, from the A8 ratio: -28% words gave -14% time). Textured lit: 12 to 10.5 words.
- Cost: 150-300 lines, precision loss at large meshes (a map with 16 bits per axis: 65 536 steps). Risk medium. After Opt 2b.

### Opt 9: index strips, vertex cache tuning

- Vertex reuse is done (0.69 shaded vertices a triangle). Strips save only the index bytes. **0% to +2%** (*E*, not verified). Skip.

### Opt 10: V3D interrupt instead of polling

- Mechanism: `INTENA` is never written. The ARM spins in `v3d_wait`. An IRQ lets it sleep or run work.
- Gain: **0%** in triangles. The ARM has no work to do in the wait (queue off) or does it already (queue on). Lower power only. v3d-notes decided: skip. Needs an `irq.h` line and a handler (about 80 lines). Last.

### Opt 11: texture size and ETC1 / T16

- Gain *(M)*: 0% (finding 9). Skip until a played Overbit at 1080p shows a gain.

### Opt 12: tile size and CL layout

- The V3D tile is 64x64 (32x32 with MSAA) and is fixed by the hardware. A frame of 640x360 uses 10x6 = 60 tiles. The last row is 40 lines of 64 (62% full). Height 320 would give 10x5 = 50 tiles (-17% tile overhead) but is not the target resolution. The pool and `JOB_BCL` do not limit (finding 10). **0%.** The only knob is the tile-list block size, which is not exposed (`not verified`).

### Opt 13: more vertex work on the QPUs

The vertex shader already runs on the QPUs (`gpu3d_vs=2`). Moving more ARM work (bone matrices, light) to the QPUs would cut the 24-35 ARM instructions a triangle. ARM time `a` is 47-83 ns. If half moves: `a` from 60 to 40 ns. With the queue on and ARM bound, this is **+5% to +15%** in `heroes_tex`, but the queue-off sum model gives only +4-8% (*E*: `(a+g)` from 142 to 122 ns). It needs a per-bone upload shader. Medium code (200+ lines). Put it after Opt 1-3 once the new scene shows who is bound.

## 3. Summary table

| Rank | Option | Helps | Gain at 60 fps | RAM | Lines | Risk |
|---:|---|---|---|---|---:|---|
| 1 | Queue on by default | GPU and ARM of equal size | +10% to +45% (*E*); +11% *(M)* with logic | 0 | ~10 | low |
| 2 | Compact attributes on (2a), lit textured (2b) | GPU-bound meshes | +3% to +23% *(M)* (2a); +3% to +6% (2b, *E*) | 0 | 5 / 100-150 | low / medium |
| 3 | Merge draws | ARM-bound, many calls | +2% to +7% (*E*) | 0 | 50-200 | low |
| 4 | Early-out culling, LOD | content | 0% on screen, x1/visible in the world | 0 | 50-300 | low |
| 5 | Resolution scaling R41 | fill-bound | 0% to +40% (*E*) | less | ~50 | low |
| 6 | Impostors R42 | crowds | k x 1 534 triangles saved (*E*) | small | exists | medium |
| 7 | Split frame, bin/render overlap | GPU-bound `tiny`, `big` | 0% to +15% (*E*, no data) | +11 MiB | 150-300 | high |
| 8 | 16-bit positions | GPU-bound meshes | +2% to +5% (*E*) | 0 | 150-300 | medium |
| 9 | Strips | - | 0% to +2% (*E*) | 0 | 80 | low |
| 10 | V3D IRQ | power | 0% | 0 | ~80 | medium |
| 11 | ETC1 / T16 | - | 0% *(M)* | 0 | exists | - |
| 12 | Tile / CL layout | - | 0% | 0 | - | - |
| 13 | More QPU vertex work | ARM-bound | +4% to +15% (*E*) | 0 | 200+ | medium |

Opt 1 and 2 stack. If the gains multiply (*E*): `mix` 78 k, with queue +34% and A8 +5%, gives about 110 k. A real game will do less: the 908 ns a triangle of Overbit shows another limit (fill or shaders) that no triangle option touches.

## 4. Bench scene: hero + map at 60 fps

Name: `herotris` (test id), unit "triangles". Code in a later PR: `src/bm/b3d.c` (scene and `tests[]` row), `src/kernel/b3dpi.c` (profile list, report), `tests/bm/b3d_host.c` (host run).

Scene, every frame:

- **Hero**: 1 skinned model of about 1 500 triangles (1 536 as in `heroes`), 16 bones, animated, textured with one sheet of 64x32 texels (`GV_LIT_TEX`), lit by the sun, with a flat shadow on the floor. Centre of the screen, about 25% of the screen height.
- **Map**: about 5 000 triangles. 100 pieces of 50 triangles, baked light (`GV_BAKED`), 2 sheets of 64x32 texels. The camera sees about all of them (no culling gain). Total with the hero: 6 500 triangles at step 1.
- **Two parallax layers**: two full-width 2D textured layers, behind the 3D and in front, 640x360, one with holes (alpha). Drawn with the sprite path (`gpu2d` style).
- **HUD**: 20 sprites and 3 lines of text over the 3D.
- **Fixed logic**: 4 M ARM instructions a frame (the `work(1300)` of `queue`), after the 3D.
- Resolution 640x360, RGB565. Same clocks as the other tests.

Load `n` = the scale of the triangle count, not heroes. At each step the scene grows by a third like the other tests (start 1, `max` 64):
- `n` heroes in a ring, with `n x 1 536` triangles; AND the map grows with `n x 5 000 / 4` triangles (pieces added in rows) so the ratio hero : map stays 1.5 k : 5 k. Step 1 = 1 hero + 5 000 map. Alternative if the camera must stay fixed: add `n` copies of the pair at deeper depth (does not change the overdraw of the first).
- Report: the maximum triangles at 60 fps = interpolated `tris60` and `drawn60` (as every `R` line), plus the first step whose middle frame is over 16.7 ms.

Profiles (existing names): GPU+VS+FS2 (default), GPU+VS+FS2 with queue 1 (GPU+VS+Q), queue 2, GPU+VS+A8, and a new GPU+VS+A8+Q. Columns to print: `ms`, `gpu_ms`, `A = ms - gpu_ms`, `wait_instr`, `bin_us`, `rnd_us` (add), `fill_px`.

Pass line for the plan: every stage in section 5 must raise the report's maximum triangles at 60 fps in this scene. Check, in QEMU, only that the scene runs and the count is stable (QEMU gives no fps).

## 5. Staged plan (one PR each)

| Stage | PR content | Bench scene shows | Gate |
|---|---|---|---|
| 0 | Add the `herotris` scene, `bin_us`/`rnd_us` columns, GPU+VS+A8+Q profile. No driver change. | The baseline, and who is bound (ARM or GPU) at a hero + map load. | Runs in `make test-b3d`; Pi report. |
| 1 | `gpu3d_queue=1` by default in Settings (Opt 1). Keep `gpu3d_later` off. | `herotris`: GPU+VS+FS2 against GPU+VS+Q. | +10% at least, Overbit played with 0 and 1: no late frames. |
| 2 | `gpu3d_attr=1` by default where `probe_attr` passes (Opt 2a). | `herotris` A8 against plain; `heroes`, `match`. | +3% at least; user decides (v3d.md). |
| 3 | Draw merging for static map pieces (Opt 3). Count of calls in the scene: 1, 10, 100. | `herotris` with 100 / 10 / 1 map pieces. | Fewer calls, same pixels. |
| 4 | Compact attributes for lit textured faces (Opt 2b). | `herotris` and `heroes_tex`. | `make test-gpu3d` same pixels; +3% at least. |
| 5 | Resolution scaling R41 as an option and the impostor default R42 with the quality governor of PR 95. | `herotris` at 480x270; `herotris` with far heroes. | A visible-quality photo; user decides. |
| 6 | Only if stage 0 shows idle GPU or a big `bin_us`: frame split (Opt 7) or 16-bit positions (Opt 8). | `tiny`, `big`, `herotris`. | Measured idle time above 10% of the frame. |

Do stages 1 and 2 first. They cost almost no code and have measured gains. Stage 0 comes before both because it gives the number against which they are judged.

## 6. Numbers to collect on the Pi

1. Stage 0 baseline: `herotris` max triangles at 60 fps, `ms`, `gpu_ms`, `A`, for GPU+VS+FS2, +Q, +Q2, +A8, +A8+Q. Three runs each (noise of `n60` steps is 3-6% in some tests).
2. `bin_us` and `rnd_us` of a job in `tiny`, `big`, `herotris`. Share of binning in `gpu_ms` (Opt 7). Idle time of the GPU between jobs with queue 2.
3. Why the queue hides 3.5 of 6.6 ms in test `queue`: time of the ARM wait in `flush3d`, the time from the end of the 3D to `v3d_start`, `stat(16)`/`stat(18)` per frame.
4. Overbit played at 640x360: `gpu3d_queue` 0 / 1, `gpu3d_attr` 0 / 1; `GPU 3D` ms, triangles drawn, fps. Find why 908 ns a triangle: GPU `gpu_ms` split into fill (`fill_px`, 1.5-2.0 ns/px), setup and 2D.
5. Hardware-culled triangles: the share of back faces in `heroes` and `herotris` (counter `gltris` against `tris`), to know the real GPU load.
6. Cost of a draw call on the real map: calls a frame, `A` per call, in `herotris` with 1 / 10 / 100 map pieces.
7. `r3d_impostor` 16 and 32 with far heroes (v3d-notes list), and `herotris` at 480x270.
8. All with `throttled 0`, core 250 MHz, V3D measured clock (300 MHz in the last report), temperature below 50 C.

## 7. Measured on the Pi, 22 Oct (stage 0)

`herotris`, one profile for each run, 640x360, ARM 1000 MHz, V3D 250 MHz (measured 300). A set is a hero of 1536 triangles, its shadow and a map of 5000 triangles.

| Profile | Sets at 60 fps | Hero tris at 60 fps | Drawn at 60 fps | ms | gpu_ms |
|---|---:|---:|---:|---:|---:|
| ARM | 0.41 | 630 | 2393 | 40.7 | - |
| GPU | 0.56 | 859 | 3265 | 29.8 | 2.1 |
| GPU+VS | 3.85 | 5907 | 28961 | 14.9 | 3.9 |
| GPU+VS+Q | 6.74 | 10351 | 50746 | 13.0 | 7.1 |
| GPU+VS+A8 | 4.03 | 6182 | 30310 | 16.6 | 5.1 |
| GPU+VS+A8+Q | 6.75 | 10364 | 50809 | 13.2 | 7.1 |
| GPU+VS+Q2 | 6.74 | 10353 | 50755 | 13.3 | 7.1 |

- Opt 1 (queue): x1.75 (3.85 -> 6.74 sets). The range of section 2 was +10% to +45%. The stage 1 gate (+10% at least) passes on `herotris`. The second part of the gate (Overbit played with 0 and 1, no late frames) is not measured.
- Opt 2a (A8): +4.7% without the queue, 0% with it. It stays off.
- Queue 2 (frame later): no gain over queue 1.
- Without the queue the frame is ARM-bound (`gpu_ms` 3.9 of 14.9 ms). With the queue it is still ARM-bound (`arm_ms` 12.6, `gpu_ms` 7.1). Opt 7 and the job chain help a GPU-bound frame, so they do not apply at this load.
- Not collected yet: three runs for each profile (the table is one run each), the items 3 to 7 of section 6.
- Pi freeze: see `docs/NEXT.md`, Open issues.

Hardware behaviour in this file is `not verified` for every *(E)* value. Section 7 is a measurement; the rest of the file is an estimate.
