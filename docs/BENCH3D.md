# 3D Bench



bm's single benchmark of its 3D capabilities: *Dev > 3D Bench*, or the `j` key of the
monitor. Code: `src/bm/b3d.c` (portable), `src/kernel/b3dpi.c` (the Pi),
`tests/bm/b3d_host.c` (the PC, `make test-b3d`).

## What it does

Each test is a scene whose load `n` grows (spheres, heroes, pixel quads, calls…).
Each test runs with every **profile**, i.e. with every driver that today's code can
reproduce (`docs/DRIVERS.md`):

- **ARM**: bm3d 0.2, the ARM rasterizer, as before the GPU;
- **GPU**: bm3d 2.1, the GPU draws the triangles that the ARM prepares;
- **GPU+AA**: the same with 4× MSAA;
- **GPU+VS1**: bm3d 3.0, the vertex shader for the scenery;
- **GPU+VS**: bm3d 3.4, the vertex shader for everything;
- **GPU+VS+Q**: bm3d 4.1, the same with the queued frame (bm3D 4.1; only in the `queue` test);
- **GPU+FS2**: the textured pixel shaders with two threads (bm3d 5.3, all the 3D ones from
  6.3), in the tests with textures; from 6.4 they are the default, and the GPU row reproduces them with one thread.
  The colour ones stay single-threaded (with two threads the Pi measured them 4–5% slower).
- **GPU+VS+FS2**: the driver as games have it from 6.6 (vertex shader for all models and
  textured shaders with two threads); it runs in every GPU test (not in `bilinear`, which tests
  an option) and the score uses it.
- **GPU+VS+A8**: GPU+VS+FS2 with compact attributes (bm3d 7.1, `gpu3d_attr`: colour and light
  bytes packed in the corners); in `spheres`, `heroes`, `match`, `big`, `big_logic`.
- **GPU+T16**: the GPU with 16-bit textures (`gpu3d_tex16`): opaque sheets as RGB565 (bm3d
  5.2) and, from 7.2, cut-out sheets as RGBA5551; in the textured tests, `quad_alpha` and
  `quad_texscreen` (the cut-out sheet), `match`, `mix`.
- **GPU+ETC1**: GPU+T16 with the opaque sheets as ETC1 (bm3d 7.3, `gpu3d_etc1`: 4 bits a
  texel, encoded when the texture is made; the cut-outs stay RGBA5551), so against GPU+T16 it
  measures ETC1 alone; in `spheres_tex`, `heroes_tex`, `quad_tex`, `match`, `mix` (the bench's
  sheets are 128×128, opaque). Summary column `ETC1`.

A profile the GPU cannot do (the boot probes turned it off) has no row.
At each step `n` grows by a third until a frame exceeds **40 ms**; the loads at
**60 fps** (16.7 ms) and at **30 fps** (33.3 ms) are interpolated between two steps. Each step
measures 6 frames after a warm-up one (textures, vertex copies, caches) and counts
the **middle** frame (the median; the counters are the average): a frame slowed down
by something else, like the network printing during a monitor line, does not move the step. The load
starts from the **last** step under the limit: a slow step before a heavier one that fits
had been slowed down by something else (on the Pi, on 2026-10-05, `quad_flat` with the GPU: 25 quads yes,
33 no, 44 yes, and the report said 28.7). The time of a frame includes the copy to the
screen, as in the stress test.

The spheres and the quads are the scenes of the stress test, at the same resolution (640×360): this way
the Pi's numbers with the earlier drivers are the historical bars.

## The tests

- `spheres`, `spheres_smooth`, `spheres_tex`: spheres of 96 faces, flat, Gouraud, with
  texture (those of the stress test);
- `spheres_unlit`, `spheres_baked`: unlit, with the light on the vertices (the scenery: also
  GPU+VS1);
- `spheres_shine`: sky and ground, rim, highlights, 4 lamps, fog;
- `heroes`: heroes of 16 bones and 1536 faces, Gouraud, moving bones; `heroes_tex`: the
  same with the texture; `heroes_skin`: with the texture and the joints on two bones (like the
  Meshy models of Overbit); `heroes_shadow`:
  with shadows on a floor;
- `clip`: map pieces around and below the camera, the closest ones across the near
  plane (the GPU clips them);
- `tiny`: faces of about 4 pixels (the cost of setting up a triangle);
- `draws`: one cube per call (the cost of a call);
- `quad_flat`, `quad_smooth`, `quad_tex`, `quad_alpha`, `quad_screen`: fill, quads
  of 320×180 (flat, Gouraud, texture, texels with holes, screen-door);
- `texswap`: three textures in turn (the GPU keeps two);
- `split`: 3D and 2D on top, several times in the same frame (GPU jobs split,
  z preserved);
- `match`: a synthetic match, map of 100 pieces, heroes with shadows, a
  first-person model and a HUD;
- `mix`: everything together, like a game (for the score): n slices of a road, each one a piece
  of map with baked light under a hero (in turn Gouraud with highlights, with the texture, with the
  skin on two bones) with its shadow, a fence of texels with holes, a screen-door glass and a
  crate; the ground under the camera across the near plane, sky, rim, 4 lamps and fog, a
  first-person model at the bottom right and a HUD of sprites and text over the 3D;
- `queue`: Gouraud spheres plus a fixed ARM workload after the 3D (4 million instructions,
  a game's logic), with GPU+VS and GPU+VS+Q: in the queue the ARM works while the GPU draws;
- `gpu2d`: sprites and text over the 3D (bm3D 4.8; with GPU+2D in the GPU job, otherwise the ARM);
  `bilinear`: quads with filtered textures (bm3D 4.9, an option off by default);
- `big`, `big_logic`: models of 10 080 triangles on a map of 14 112 (larger indexed meshes, bm3D 5.0), the second with the
  logic of `queue`;
- the 2D scenes `map8`, `sprites`, `stack`, `fade`, `light` (see *The 2D scenes*) `herotris` and `cel` (see *herotris*): they run only when `tests=` names them.

The maximum loads are far beyond what the drivers do today (up to 8000 spheres, 512
heroes, 40 000 calls): they leave headroom for the drivers to come.

## The statistics

For each test and profile, at the step that still fits in 60 fps: load, ms (average and worst),
fps, triangles drawn, vertices set up by the ARM, pixels (of the ARM), GPU ms
(binning + rendering), GPU jobs, **ARM instructions** per frame without those
spent waiting for the GPU (and those separately), instructions per triangle and per element,
data **cache misses**. The instructions and the misses come from the ARM1176 counters
(`src/kernel/pmu.c`), only on the real Pi (QEMU does not have them).

## The score

The first page at the end (user's request, 2026-10-06). Two numbers:

- **Score**: for each test that is a technology (24: flat, Gouraud, textured, unlit, baked-light
  spheres, lights; heroes, texture, skins, shadows; clip, small faces, calls; the fills; three
  textures; 3D and 2D; match; big meshes; 2D over the 3D) the load at 60 fps divided by the one
  that **bm3d 2.1** (the GPU profile) gave at its best on the Pi Zero W (reports of 5 and 6 October,
  `score_ref` in `b3d.c`); the score is the geometric mean of the ratios times 1000. So 1000 is
  bm3d 2.1 on the Pi, 2000 a driver twice as fast in every test (or 4 times in half and equal
  in the rest). A load below the first step counts as the part of that step that fits in 16.7 ms (one
  hero in 20 ms: 0.83). Outside the score: the tests with logic (`queue`, `big_logic`), the
  options off by default (`bilinear`) and `mix` (it has the other number). The big score is for the driver
  **as games have it** in this version: from 6.6 the GPU+VS+FS2 row (in bm3d 6.4 and 6.5
  it was the GPU with the textured shaders on two threads: the GPU+FS2 row where there is one, the GPU in the other
  tests); without that row (the vertex shader probe did not pass, and games do not have it)
  GPU+FS2, then the GPU, and the ARM if there is no GPU, like the RGB30. Next to it, the scores of bm3d 2.1 (GPU) and 0.2 (ARM) in the
  same tests and the one of the previous report (green if it did not drop more than 3%). A run with
  `tests=` or `profiles=` says "a part of the bench": it is not compared.
- **Triangles per frame at 60 fps** (640×360): those of the `mix` scene with the games'
  driver, interpolated like the load, and how many the GPU actually drew (the others are
  back faces or off screen); below, the test with the most triangles at 60 fps and how many per
  second.

Below, one bar per test: its ratio against bm3d 2.1 (the tick is 1×) and the profile that
is the driver there. The report has the lines `score ...`, `S,driver,version,score,tests,full` (the
next run reads `S,games`) and `triangles at 60 fps ...`; every `R` line has `tris60` and
`drawn60` at the end. Shadows count as triangles (a mesh drawn again, flattened: r3d counts them
from kernel `v0.2.3-51`; the first report with the score, `-50`, left them out).

## The clocks

The `machine` line gives the clocks of the ARM, of the core and of the V3D (the requested one, the maximum the
firmware allows and the measured one) and of the SDRAM; at each step the bench reads again the **measured**
clock of the core and of the V3D (`GET_CLOCK_RATE_MEASURED`, outside the step's time) and the `R`
lines have them at the end (`core_mhz`, `gpu_mhz`, of the step at 60 fps), as do the `b3d` lines of the log.
They help understand why the quads of the GPU profile sometimes run at half speed or less in the same
run (`quad_flat` 175 in one run, 91 or 70 in another, with the same drivers). On the Pi Zero W
`enable_uart=1` in `boot/config.txt` keeps the core at 250 MHz (the serial port), and the V3D has also been
seen at 250; `v3d_clock=max` in `bm/config.txt` asks for the firmware maximum at boot (a
trial, off by default).

## The pages at the end

- **Score** (above);
- **Summary**: for each test the load at 60 fps of each profile, the best, how many
  times the ARM, how many times the previous report, whether it fits in 60 fps; the (geometric) mean
  of each profile against the ARM; the tests not yet developed.
- **Drivers**: the bm3d versions, the profiles, the machine (clocks, temperature, throttling),
  the GPU status, the saved report.
- **One test per page**: the bars (60 fps bright, 30 fps dim) of each profile with a
  white tick at the value of the previous report; in grey the numbers measured on the Pi with the
  earlier drivers (0.1, 0.2, 1.0: `docs/GPU-BENCHMARK-REPORT.md`); in red the hardware
  limit where there is one (the V3D: 1 Gpixel/s and 1.5 Gtexel/s according to Raspberry Pi; 3.0
  million triangles per second measured by the `g` test); below, the table of
  statistics.

Left/right change page, B (or Esc) exits.

## The reports

Each run saves `bm/bench/3D0001.TXT`, `3D0002.TXT`… on the SD: a header (kernel,
driver, date if there is a network, machine, GPU status) and one CSV line per test and profile
(`R,test,profile,version,n60,n30,…`; the columns are in the `columns` line). The next
run reads the last report and compares against it (white ticks, "x last" column). To
keep their history in the repository, copy them to `docs/bench/`.

The columns `bin_us` and `rnd_us` (last two) are the binner's and the renderer's time of the GPU jobs, in
microseconds a frame (the jobs' `bin_us` and `render_us` of `gpu3d_stats_t`, the same counters as `gpu_ms`). They are 0 on the ARM
and small on the PC. With the queue, jobs that had ended before the ARM looked are not timed (see below): read both columns on the profiles without `Q`.

Five columns before them are measurements only (they change nothing): `clean_kcyc` and
`inval_kcyc` are the ARM cycles (thousands, a frame; 0 without the PMU) of the whole data
cache clean + invalidate before each job and after it; `pool_kb` and `ovf_kb` the most of
the binner's tile-list pool (4096 KiB) and of its overflow area (2048 KiB) one job used,
from `BPCA` after the job; `ovf_jobs` the jobs whose binner went into the overflow. The
GPU test (`g`) and the session report print the binner's figures too.

**The cost of a pixel.** The fill tests (`quad_*`, `bilinear`) and `gpu2d` count the pixels a
frame asks to fill (`fill_px`: 320x180 a quad; for `gpu2d` the area of its sprites and text) and
fit a line through the steps (`b3d_slope`): its slope is the cost of one more pixel in **ns a
pixel** (= ms a Mpixel), so the frame's fixed part (binning, clear, tile stores, the
spheres under the 2D) drops out. Three more `R` columns: `fill_px` (at the 60 fps step),
`ns_px_gpu` (from the GPU job time; -1 on the ARM and with the queue, whose job time is not
trustworthy), `ns_px_frame` (from the frame's ms: what the ARM pays for its 2D or fill), and
`gpu0_ms` (the GPU's line at 0 pixels). After the `R` lines, one `P` line per such row
(`columns P,...`) repeats the costs and gives the ms to fill 640x360, 1280x720 and 1920x1080 at
them (fill only: the bench draws at 640x360, so the larger screens' tile stores are not in it).
Compare `quad_flat` vs `quad_tex` vs `bilinear` (nearest vs filtered texels) and, in `gpu2d`,
`GPU+2D` (`ns_px_gpu`) vs `GPU` (the ARM's 2D: `ns_px_frame`). On the PC the job time is 0 and
the ms are noise: only the shape is checked.

A job's GPU time (`gpu_ms`, the session report's *ms a frame* and *longest job seen to its
end*) runs from its start to the end the ARM saw polling; a queued job that had ended before
the ARM looked is not timed and counts at the timed ones' mean (`gpu3d_busy_us`; none timed:
its start to the wait, an upper bound). The session report after a resume covers that run only.
A game reads the same time for its last frame with `stat(16)` (ms; 0 on the ARM); the Overbit
bench logs its mean per step as `gpu` next to `update` and `3D`: close to the frame's ms, the
GPU sets the pace; well under it, the ARM does.

So runs can be compared, every report says the 3D options in force on one `options` line
(`gpu3d_options_line`): `bm3d 7.3 gpu3d=on vs=2 aa=0 queue=2 wc=0 filter=0 tex16=0 fs2=1
sort=0 attr=0 etc1=0` (`gpu3d=off` without the V3D), plus `3d= later= 2d= impostor= fast=` in the session
report (second line, as the run ended) and on top of a game's `report()` (the Overbit
bench's `overbit-bench`). The 3D Bench takes it at its start (each profile then sets its
own), the GPU test (`g`) prints it at its end.

## A part, from the monitor

The monitor's command line (`:`) runs only some tests and profiles, without waiting
on the pages, and can queue other commands:

```
:gpu; b3d tests=match,quad_tex profiles=GPU,FS2; send
```

`tests=` takes the test ids (`spheres`, `match`, `quad_tex`…), `profiles=` the profile
names (`GPU+FS2`) or the short ones of the summary (`FS2`); without them, everything. The report states the part
(`only tests …, profiles …`). `set key=value` changes a key of `bm/config.txt` until
reboot (to try a game), `save` keeps it.

The line can also be typed in the menu: the `:` takes it to the monitor. From the PC: `./easy_install.sh line
"gpu; b3d; send"` (or `tools/bm_net.py IP --line "..."`) sends it and shows what the console
writes until it says `the line is done`; in the easy_install monitor (6) paste `:gpu;
b3d; send` and Enter. The console tells the PC where the keys go (the menu, which does not repeat them, or the
monitor).

## The 2D scenes

Five tests measure 2D work, so a 2D change can be compared on the Pi Zero W. They are **opt-in**:
the default run does not run them, and they are not in the score. Run them by name:

```
./easy_install.sh line "gpu; b3d tests=map8,sprites,stack,fade,light; send"
```

Each scene is fixed. The positions come from a hash of the item number and the frame number: no
time, no random seed. The report has the usual fields (`ms`, `worst`, `instr`, `gpu_ms`,
`clean_kcyc`, `inval_kcyc`) and a `P` line with `ns_px_frame`, the cost of one pixel of the
frame. Compare the same test before and after a change, on the same console, ARM at 1000 MHz.

| Test | Load `n` | Scene | Profiles |
|---|---|---|---|
| `map8` | layers, 1 to 8 | 8 layers of `map()` at 640x360, parallax scroll. Layers 0-2: every cell, opaque tiles. Layers 3-7: half the cells, tiles with holes (alpha). | ARM |
| `sprites` | sprites, from 16 | 16x16 `sspr` with flip, half opaque and half with alpha. | ARM, GPU+2D |
| `stack` | stacks, from 1 | `g16_stack`: stacks of 8 slices, three sizes, turning. A fixed pile. | ARM |
| `fade` | lamps, from 1 | `g16_fade_glow` lamps and `g16_fade_apply` over the whole screen (the ARM path of `dark_end`). | ARM |
| `light` | lamps, from 1 | `g16_light_add` lamps and `g16_light_apply` over the whole screen. | ARM |

Every frame starts with `cls` of the 640x360 page. `fade` and `light` also draw 12 blocks of
colour first, so the pass over the whole screen has a picture to work on. The cost of the `cls` is
in every row, so compare rows of the same test only.

## herotris

`herotris` is stage 0 of `docs/design/triangle-throughput.md`. It is **opt-in** like the 2D scenes: the default run, the score and
the old numbers do not change. It answers: how many triangles at 60 fps does a hero and a map hold, with a game's 2D and logic?

```
./easy_install.sh line "gpu; b3d tests=herotris; send"
```

Load `n` = sets (1 to 64). A set is a hero (1536 triangles, 16 bones, a 64x32 texture, lit, with its shadow) and a map of
100 pieces of 50 triangles (baked light, two 64x32 sheets), so a step adds 6500 triangles (8036 with the shadow). The sets share
the map's place and the camera sees them all. Each of the 16 parts of the hero is on one bone, so every face is on one bone: the
mesh path of the vertex shader takes the mesh (it refuses a face on two bones unless the face is textured and lit).

After the 3D job: 4M instructions of logic (`work`, as `queue`), then the 2D (two parallax bands, top opaque and bottom with holes,
20 sprites and 3 lines of text). The 2D runs after the job and not inside the scene, as the 2D records of a game play after the job. With
the queue profiles the ARM then does the logic while the GPU draws. The bands are drawn over the 3D, not behind it: a page cleared by
the GPU job would lose the ARM's layer. Resolution 640x360 (the report says it on the `size` line; the bench has no 720x405 and no
1080p mode). The bench steps the load up until the frame misses 60 fps.

Profiles: `ARM`, `GPU`, `GPU+VS` (no FS2), `GPU+VS+FS2` (the driver as games get it), `GPU+VS+Q`, `GPU+VS+Q2`, `GPU+VS+A8` and
`GPU+VS+A8+Q` (compact attributes with the queue; short name `VSAQ`). Run each three times. The ARM row can miss 60 fps at one set:
its numbers then scale the first step (`tris60` of the `R` line).

### The hero triangles at 60 fps (Q1)

After the `P` lines the report has the `H` lines, one for each profile of `herotris`:

```
columns H,test,profile,sets60,hero_tris60,hero_shadow_tris60,submitted60,drawn60,ms,gpu_ms,arm_ms,mesh,probe_gltris,probe_meshes,vs_probe,later_submitted60,later_drawn60
```

| Column | Meaning |
|---|---|
| `sets60` | Sets a frame of 60 fps holds. |
| `hero_tris60` | Hero triangles in them (1536 a set). `hero_shadow_tris60`: with the shadow, which is the hero drawn again. |
| `submitted60` | Triangles given to r3d (hero, shadow, map). It is `tris60` of the `R` line. |
| `drawn60` | Triangles r3d keeps after its clip and cull. The GPU throws back faces away later. |
| `ms`, `gpu_ms`, `arm_ms` | At the last step of 60 fps. `arm_ms` is `ms` less the time the ARM waits inside `gpu3d_sync`. |
| `mesh` | What the vertex shader did with one hero drawn alone before the ramp. `ACCEPTED`: the mesh path placed all 1536 triangles. `FALLBACK`: it placed 0, the triangles went one by one through the ARM. `PARTIAL`: some. `NA`: the profile has no vertex shader. |
| `probe_gltris`, `probe_meshes` | The counters behind `mesh` (`gltris` and `glmeshes` of `gpu3d_stats_t`). |
| `vs_probe` | `gpu3d_vshader()` of the start-up probe: 0 none, 1 scenery only, 2 heroes too. A profile that needs level 2 has no row if it is 1. The header line `vsprobe` shows it too. |
| `later_*` | `GPU+VS+Q2` only. See below. |

**Frame shown a frame later.** The bench has one page, so it cannot show a frame a frame later (`gpu3d_later`). The `later_` columns are a
model of it: the frame costs the larger of the ARM side (`ms` less the wait in `gpu3d_sync`) and the GPU time (`gpu_ms`). The ramp
is read again at 60 fps with these times. The model gives the best case. With the queue the job times are not exact (a job that ended before the ARM looked
counts at the mean). Check the model with a cart that sets `gpu3d_later=1`.

The four rows asked for: ARM, GPU, GPU+VS (level 2), and GPU+VS+Q2 (queue 2) with its `later_` columns.

A note on the old numbers: before this change the 2D ran inside the scene, so the 3D job was flushed before the 2D, and the queue profiles
had nothing to overlap. The `Q`, `Q2` and `VSAQ` rows of `herotris` are now different from the first runs. Do not compare them.

### The cel shader (Q2)

`cel` is an opt-in group of four tests, the same scene: `n` heroes (the textured hero, 1536 triangles, 16 bones, lit by the sun) in
rows of 8. Profiles: `ARM` and `GPU+VS`.

| Test | Light | Dither of the ARM raster |
|---|---|---|
| `cel_lit` | the sun, no cel | on |
| `cel` | cel, 4 bands (`r3d_cel`, floor 0.35, no outline) | on |
| `cel_lit_nd` | the sun, no cel | off (`r3d_dither(0)`) |
| `cel_nd` | cel, 4 bands | off |

```
./easy_install.sh line "gpu; b3d tests=cel; send"
```

`tests=cel` runs the four. A cel draw forces the ARM path (`r3d.c`: the QPU programs have no cel cut yet), so on `GPU+VS` the lit hero goes
through the mesh path and the cel hero does not: the `GPU+VS` rows show what the fall back costs. After the `H` lines the report has
the `C` lines, one for each profile and n from 1 to 4 (the same steps in the four tests):

```
columns C,profile,n,tris,lit,cel,lit_nd,cel_nd,cel_over_lit,cel_nd_over_lit_nd
```

`lit`, `cel`, `lit_nd`, `cel_nd` are the ms a frame of the four tests. `-1.00` is a step that did not run. The ratios are `cel / lit`
and `cel_nd / lit_nd`. The `R` lines of the four tests have the usual fields (`n60` is the heroes at 60 fps).

### Both together

```
./easy_install.sh line "gpu; b3d tests=herotris,cel; send"
```

Not developed here: `stackv` and the 3D particles. Their scenes wait for their code in `bm-core`.
The hook is the comment at the top of the block "the 2D scenes" in `src/bm/b3d.c`.
The test of the triangle count is a different work (`docs/design/triangle-throughput.md`).

On the PC, `make test-b3d` runs them (`herotris` and `cel`) for a few frames and checks that the fields, the `H` lines and the `C` lines exist.

## On the PC

`make test-b3d` does two short runs on the V3D emulator (few steps, one frame
each: the PC's ms do not count) and checks report, profiles, score, comparison and pages
(`build/b3d/page-NN.ppm`). `build/host/b3d_host DIR --full -v` does the full run;
`--tests=mix --profiles=GPU,ARM` a part, `--frames` saves every frame
(`DIR/frame-NNNN.ppm`) to look at the scenes.
