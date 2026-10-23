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
- the 2D scenes `map8`, `sprites`, `stack`, `fade`, `light` (see *The 2D scenes*), `herotris`, `cel` (see *herotris*), `stress` (see *stress*) and `ht_p100`, `ht_p100m`, `ht_p10`, `ht_p10m`, `ht_p1` (group `htmap`, see *herotris with the map in pieces*): they run only when `tests=` names them.

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

Each run saves `bm/bench/3d0001.txt`, `3d0002.txt`… on the SD: a header (kernel,
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
| `fade` | lamps, from 1 | `g16_fade_glow` lamps and `g16_fade_apply` over the whole screen (the ARM path of `dark_end`). The `QPU` profile does the same fade through the QPU path (see *The fade on the QPU*). | ARM, QPU |
| `light` | lamps, from 1 | `g16_light_add` lamps and `g16_light_apply` over the whole screen. | ARM |

Every frame starts with `cls` of the 640x360 page. `fade` and `light` also draw 12 blocks of
colour first, so the pass over the whole screen has a picture to work on. The cost of the `cls` is
in every row, so compare rows of the same test only.

## The fade on the QPU

The profile `QPU` is a column of the `fade` test only. It draws the same frame as `ARM`, but the full-screen fade goes through the hook of `dark_end()` (`qpu_fade_hook`, `src/gpu/qpu.h`). The hook runs the fade on the QPUs in blocks of 32 pixels and falls back to the ARM version when a QPU run fails. The profile is **opt-in**: it runs only with `tests=fade`, and it is not in the score.

The QPU path is on only with the config key `gpu3d_qpu=1` (default off). Without the key, the `QPU` profile runs the ARM fade and says so. The report has one extra line after the `qpu row` line:

```
qpufade key=1 path=qpu frames_qpu=<n> frames_arm=<n> n60_qpu=<n> ms_qpu=<ms> n60_arm=<n> ms_arm=<ms> (n60: lamps at 60 fps; ms: 1 lamp)
```

| Field | Meaning |
|---|---|
| `key` | 1 if `gpu3d_qpu=1` was set when the bench started, else 0. |
| `path` | `qpu` if every frame of the `QPU` profile ran on the QPUs, `arm` if none did, `mixed` if both happened (a 3D job was running, or a run failed and the ARM took over). |
| `frames_qpu`, `frames_arm` | the frames of the `QPU` profile on each path. |
| `n60_*`, `ms_*` | the load at 60 fps (lamps) and the median frame time at 1 lamp, for `QPU` and for `ARM`. `ms` at 1 lamp is the figure to compare: it is the full-screen fade plus the fixed part of the frame (`cls` and the 12 blocks). The gain of the QPU is `ms_arm - ms_qpu`. |

Measured on the Pi Zero W (22 Oct, one run, report `20261022-184933_bench3d_pi-zero-w_dev.txt` on the `reports` branch): `qpufade key=1 path=qpu frames_qpu=98 frames_arm=0 n60_qpu=14.3 ms_qpu=9.3 n60_arm=-1 ms_arm=20.4`. The full-screen fade with 1 lamp takes 20.4 ms on ARM (misses 60 fps) and 9.3 ms on the QPU (2.2 times faster). The QPU path reaches 14.3 lamps at 60 fps. Every frame ran on the QPU. Not verified on hardware beyond this one run.

The names are the usual ones: `profiles=ARM,QPU` (letters and digits, comma only). The `R,fade,QPU` line is the same format as every `R` line.

Run it on the Pi (monitor line; `set` changes the key until a restart, so put it first: the GPU test then runs its QPU step too):

```
./easy_install.sh line "set gpu3d_qpu=1; gpu; b3d tests=fade profiles=ARM,QPU; send"
```

The PC test (`make test-b3d`) runs the profile with a stand-in hook and checks the line (`path=qpu`); the QEMU test `test_monitor_line` runs it without the key (`path=arm`). Neither measures a QPU time.

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

`cel` is an opt-in group of five tests, the same scene: `n` heroes (the textured hero, 1536 triangles, 16 bones, lit by the sun) in
rows of 8. Profiles: `ARM` and `GPU+VS` (and `GPU+VS+FS2`, the driver as games get it).

| Test | Light | Dither of the ARM raster |
|---|---|---|
| `cel_lit` | the sun, no cel | on |
| `cel` | cel, 4 bands (`r3d_cel`, floor 0.35), flag `R3D_CEL` | on |
| `cel_lit_nd` | the sun, no cel | off (`r3d_dither(0)`) |
| `cel_nd` | cel, 4 bands | off |
| `cel_outline` | the sun, no cel, outline: flag `R3D_OUTLINE`, colour 0x2030A0, width 0.03 (the inverted hull) | on |

```
./easy_install.sh line "gpu; b3d tests=cel; send"
```

`tests=cel` runs the five. Since bm3d 7.5 the vertex programs of `GPU+VS` cut the light, so the cel hero keeps the mesh path there.
`cel_outline` draws the mesh again as a hull: its ms against `cel_lit` is the cost of the outline. The `ARM` rows are the ARM raster.
After the `H` lines the report has the `C` lines, one for each profile and n from 1 to 4 (the same steps in the five tests):

```
columns C,profile,n,tris,lit,cel,lit_nd,cel_nd,outline,cel_over_lit,cel_nd_over_lit_nd,outline_over_lit
```

`lit`, `cel`, `lit_nd`, `cel_nd`, `outline` are the ms a frame of the five tests. `-1.00` is a step that did not run. The ratios are
`cel / lit`, `cel_nd / lit_nd` and `outline / lit`. The `R` lines of the tests have the usual fields (`n60` is the heroes at 60 fps).

### Both together

```
./easy_install.sh line "gpu; b3d tests=herotris,cel; send"
```

Not developed here: `stackv` and the 3D particles. Their scenes wait for their code in `bm-core`.
The hook is the comment at the top of the block "the 2D scenes" in `src/bm/b3d.c`.
The test of the triangle count is a different work (`docs/design/triangle-throughput.md`).

On the PC, `make test-b3d` runs them (`herotris`, `cel` and `stress`) for a few frames and checks that the fields, the `H` lines, the `C` lines and the `T` lines exist.

## herotris with the map in pieces

Stage 3 of `docs/design/triangle-throughput.md` (draw merging, bm3d 7.6). It is **opt-in**, group `htmap`. It answers: what does a draw call cost
the ARM, and how much of it does `gpu3d_merge` take back?

```
./easy_install.sh line "gpu; b3d tests=htmap; send"
```

It is the `herotris` set (a hero of 1536 triangles with its shadow, the 2D, 4M instructions of logic), but the 5000 triangles of the map are in 100 pieces
of 50 (`ht_p100`), 10 pieces of 500 (`ht_p10`) or 1 piece of 5000 (`ht_p1`). Same area, same triangles, same baked light and sheets; only the number of
`draw3d` calls of the map changes: 100, 10 or 1 a set. The pieces are drawn in two runs by sheet (all of the first sheet, then all of the second), as a
game that sorts its static map by texture. (`herotris` itself alternates the two sheets in a chequer and has no run to merge.) `ht_p100m` and `ht_p10m` are
`ht_p100` and `ht_p10` with `r3d_t.merge` on, the same as a game with `gpu3d_merge` on (the bench profiles leave `gpu3d_merge` off, so every other test
keeps its numbers).

Profiles: `ARM`, `GPU`, `GPU+VS`, `GPU+VS+Q` (the path of the games). Read the `R` lines of `GPU+VS+Q` (the line format is the same as every test):

| Question | Where |
|---|---|
| Cost of a call on the ARM | `ms - gpu_ms` of `ht_p100` less that of `ht_p1`, divided by 99 and by the sets at 60 fps. With the queue the report has `ms` only; use `GPU+VS` (no queue) for this. |
| What merging takes back | `ht_p100m` against `ht_p100`, and `ht_p10m` against `ht_p10`: `tris60`/`drawn60` at 60 fps (more is better), `ms` at the same step. The best case is `ht_p1`. |
| Calls a frame | The progress log line of each step ends `N meshes` (meshes placed by the vertex shader a frame): about 90 for `ht_p100` at one set, 3 for `ht_p100m`, 11 for `ht_p10`, 2 for `ht_p1` (the PC counted those). |

Draw merging holds the draws, so at the first frame of a run (and after any change of the run) the frame is as without merging; from the second frame
on the run is one draw. A step of the bench has several frames, so the median of a step is the merged one. Run each profile three times.

On the PC `make test-b3d` runs the five (`check_b3d.py`: the same triangles given to r3d in all five, every profile of the list has a row) and `make test-gpu3d`
checks the pixels of merged against drawn one by one (`draw merging` line).

## stress

`stress` answers: does the frame stay stable at twice the load that holds 60 fps? It is **opt-in** like `herotris`: the default run, the score and the old numbers do not change.

```
./easy_install.sh line "gpu; b3d tests=stress; send"
```

The test runs on one profile only: `GPU+VS+Q` (the GPU, the vertex shader and the queue: the default path of the games on the Pi). It has two stages.

1. The ramp. It is the ramp of `herotris` in finer steps: the load `n` is a number of quarter sets. A quarter set is 25 map pieces of 50 triangles. A hero with its shadow (3072 triangles) is added for every 4 quarters, or part of them. So 4 quarters are one `herotris` set (8036 triangles). The 2D (parallax, HUD, text) and the 4M instructions of logic are the ones of `herotris`. The ramp gives `n60`, the load at 60 fps.
2. The two loads. The frame runs for 3 s at 1x (`n60` rounded down to whole quarters, at least 1) and then for 3 s at 2x (twice that `n`). If the driver gives up at 1x (`v3d_failed=1`), the 2x load does not run (`T,GPU+VS+Q,2x,not_run`).

The report has the usual `R` row of the ramp and, after the `C` lines, these lines:

```
stress path=GPU+VS+Q n60=<quarter sets> (quarter sets: 4 are one herotris set of 8036 triangles) secs=3 each
columns T,profile,load,n,tris_in,frames,secs,ms,worst,drops,arm_ms,gpu_ms,jobs,pool_kb,ovf_kb,ovf_jobs,v3d_failed
T,GPU+VS+Q,1x,...
T,GPU+VS+Q,2x,...
```

| Column | Meaning |
|---|---|
| `load` | `1x` or `2x` |
| `n`, `tris_in` | quarter sets, and the triangles given to r3d in a frame |
| `frames`, `secs` | frames run and seconds of the load |
| `ms`, `worst` | mean ms of a frame (with the copy to the screen) and the slowest frame |
| `drops` | frames over 16.7 ms (they missed 60 fps) |
| `arm_ms` | ms less the ARM's wait for the GPU in `gpu3d_sync` |
| `gpu_ms`, `jobs` | the V3D's busy time and the jobs, a frame |
| `pool_kb`, `ovf_kb`, `ovf_jobs` | the binner's memory: the most a job used, the overflow memory, the jobs in the overflow |
| `v3d_failed` | 1 if the driver gave up (V3D hang or timeout) during the load |

How to read it. The 2x load is stable if `v3d_failed` is 0, the bench finished (no freeze), and `pool_kb` and `ovf_kb` stay inside the pool the driver has. At 2x the frame is expected to miss 60 fps: `ms` is then about the double of 1x and `drops` is near `frames`. What is looked for is a clean slow-down (`worst` near `ms`) and no `v3d_failed`. The test has no pass or fail mark of its own: the SPV reads the columns. Without the V3D (QEMU) the profile does not run and the report has no `stress` line.

On the PC, `make test-b3d` runs the test for 6 frames a load and checks the `T` lines (the PC's ms do not count).

Measured on the Pi Zero W, 22 Oct 2026, one run (report `20261022-191158_bench3d_pi-zero-w_dev.txt`, id `b3d-191158`): `n60` = 24.8 quarter sets.

| Load | `n` | `tris_in` | `ms` | `worst` | `drops` | `arm_ms` | `gpu_ms` | `pool_kb` | `ovf_kb` | `v3d_failed` |
|---|---|---|---|---|---|---|---|---|---|---|
| 1x | 24 | 48 432 | 13.3 | 13.5 | 0 | 13.1 | 6.8 | 122 | 0 | 0 |
| 2x | 48 | 96 864 | 25.2 | 25.3 | 116 of 116 | 25.2 | 16.1 | 142 | 0 | 0 |

At 2x the frame runs at about 40 fps, with 2 jobs a frame. The slow-down is clean (`worst` is near `ms`). The ARM sets the pace (`arm_ms` equals `ms`), not the GPU. The Pi did not hang. Only one run exists.

## On the PC

`make test-b3d` does two short runs on the V3D emulator (few steps, one frame
each: the PC's ms do not count) and checks report, profiles, score, comparison and pages
(`build/b3d/page-NN.ppm`). `build/host/b3d_host DIR --full -v` does the full run;
`--tests=mix --profiles=GPU,ARM` a part, `--frames` saves every frame
(`DIR/frame-NNNN.ppm`) to look at the scenes.
