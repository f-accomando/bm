# 3D Bench

Italian version: [BENCH3D-IT.md](BENCH3D-IT.md).

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
- **GPU+VS+Q**: bm3d 4.1, the same with the queued frame (M35; only in the `queue` test);
- **GPU+FS2**: the textured pixel shaders with two threads (bm3d 5.3, all the 3D ones from
  6.3), in the tests with textures; from 6.4 they are the default, and the GPU row reproduces them with one thread.
  The colour ones stay single-threaded (with two threads the Pi measured them 4–5% slower).
- **GPU+VS+FS2**: the driver as games have it from 6.6 (vertex shader for all models and
  textured shaders with two threads); it runs in every GPU test (not in `bilinear`, which tests
  an option) and the score uses it.

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
- `mix`: everything together, like a game (M41, for the score): n slices of a road, each one a piece
  of map with baked light under a hero (in turn Gouraud with highlights, with the texture, with the
  skin on two bones) with its shadow, a fence of texels with holes, a screen-door glass and a
  crate; the ground under the camera across the near plane, sky, rim, 4 lamps and fog, a
  first-person model at the bottom right and a HUD of sprites and text over the 3D;
- `queue`: Gouraud spheres plus a fixed ARM workload after the 3D (4 million instructions,
  a game's logic), with GPU+VS and GPU+VS+Q: in the queue the ARM works while the GPU draws;
- `gpu2d`: sprites and text over the 3D (M37; with GPU+2D in the GPU job, otherwise the ARM);
  `bilinear`: quads with filtered textures (M37, an option off by default);
- `big`, `big_logic`: models of 10 080 triangles on a map of 14 112 (M39), the second with the
  logic of `queue`.

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
  earlier drivers (0.1, 0.2, 1.0: `docs/M33-PRIMA-DOPO.md`); in red the hardware
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

## On the PC

`make test-b3d` does two short runs on the V3D emulator (few steps, one frame
each: the PC's ms do not count) and checks report, profiles, score, comparison and pages
(`build/b3d/page-NN.ppm`). `build/host/b3d_host DIR --full -v` does the full run;
`--tests=mix --profiles=GPU,ARM` a part, `--frames` saves every frame
(`DIR/frame-NNNN.ppm`) to look at the scenes.
