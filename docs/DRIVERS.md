# bm's 3D drivers: the versions

Italian version: [DRIVERS-IT.md](DRIVERS-IT.md).

bm's 3D goes through two drivers: **r3d** (`src/bm/r3d.c`: scene, transforms, light,
the ARM rasterizer) and **gpu3d** (`src/gpu/gpu3d.c` with the shaders from
`tools/qpuasm.py`: the V3D backend). They have a single version, **bm3d X.Y**: X is the
development block (a milestone), Y the step within the block. The version is in
`src/gpu/version3d.h` (`BM3D_VERSION`, the `bm3d_versions()` table).

Where it shows: the GPU status line (test `g`, log), *Settings > System > 3D
driver*, the log when a game starts ("the 3D is drawn by the GPU as bm3d 3.0"), the
stress test, the 3D Bench, the Overbit benchmark and the fourth value of `gpu3d()`.

## The versions

- **0.1** (until September 2026, M7–M32): ARM only, the first r3d rasterizer (the
  September stress test: 31 spheres at 60 fps).
- **0.2** (2026-10-01, M33): ARM only, fixed-point edges, specialized texture
  loops, z cleared with DMA (69 spheres at 60 fps on the Pi).
- **1.0** (2026-10-01, M33): the GPU draws the triangles (NV shaders), the ARM sets them up and
  lights them; z preserved from one job to the next.
- **2.0** (2026-10-01, M34): clean pages without load, T-format textures, 4× MSAA, fewer
  ARM instructions per triangle (182 spheres at 60 fps on the Pi).
- **2.1** (2026-10-03, M34): Overbit on the GPU: screen-door faces, precomputed RGB light and
  fog on the vertices, shadows, 3D effects.
- **3.0** (2026-10-03, M36): the GPU's vertex shader sets up the scenery (unlit models
  or with the light on the vertices); the GPU clips on the near plane and on the guard band.
- **3.1** (2026-10-03, M36): the vertex shader also for the sunlit models, with
  bones (the heroes).
- **3.2** (2026-10-03, M36): the vertex shader also for shadows and the foreground.
- **3.3** (2026-10-03, M36): the sunlit textured models (Meshy's heroes):
  the light of each vertex (Gouraud, grey) on textures too, on the ARM and on the GPU; the
  vertex shader `vs_lit_tex` sets them up with the bones.
- **3.4** (2026-10-03, M36): skins on the vertex shader: the faces straddling two bones (the
  Meshy models are a single surface) in groups per pair of bones, each vertex set up
  by the matrix of its bone as r3d does (`vs_lit_tex2`, `cs_colour2`, the shadows
  `vs_shadow2`/`cs_shadow2`).
- **4.0** (2026-10-03, M35): the queued frame: at the end of the frame the GPU job
  starts and is not waited for (binning increments a semaphore that rendering waits on,
  like Linux's vc4 driver: the two threads start together); meanwhile the `_update`
  of the next frame runs, and the GPU is waited for only before touching the page or the memory
  the job reads. A boot probe (`probe_queue`) turns it off if the V3D does not finish
  the job as it should. Option `gpu3d_queue`.
- **4.1** (2026-10-03, M35): a single GPU job per frame even with the HUD and the
  first-person arms. The 2D drawn after the 3D while the GPU draws (the HUD) no longer
  waits for it: the job starts and the 2D is recorded (`d2` in `runtime.c`: rectangles,
  text, sprites, map, prompt...), then goes onto the page in the same order when the GPU has
  finished (before more 3D, reading the page, showing it). The `zclear()` between the
  world and the arms (`R3D_FRONT`) stays in the same job: a quad over the whole
  page with `fs_zclear` writes the farthest depth and gives each pixel back its
  colour (the *colour load* signal reads the tile buffer, as Mesa does for blending);
  the boot probe `probe_zclear` turns it off if the V3D does not do it. The 2D that `_update`
  draws while the GPU works goes onto the next frame's page, as without the queue; an
  `_update` that draws 3D or reads the page goes back to running after the frame (a line
  in the log). All with `gpu3d_queue=1`.
- **4.2** (2026-10-04, M38): screens up to 1920×1080 (`screen()`, the resolution changed
  by the cartridge between one frame and the next): tile state, render list and
  depth between jobs sized for 1080p even with MSAA; the vertex guard band
  in 12.4 (±2048 pixels) narrows on wide screens (120 pixels at 1920, previously
  a fixed 1000), the triangles that cross it are clipped by the ARM. A `cls()` with the GPU is no longer
  drawn by the ARM (4 MB at 1080p): the GPU job clears its tiles to that colour;
  the ARM fills the page only if 2D or a read arrives before the 3D, or the page must be
  shown without 3D. It holds for all GPU modes.
- **4.3** (2026-10-05, M36): the vertex shader that drew nothing on the Pi (boot
  probe `0000 0000 0000 0000`). In the GL record the size of the attributes and their
  position in the VPM are in **bytes**, as Mesa writes them ("byte offsets for the start of
  the vertex attributes 0-7, and the total size", `vc4_context.h`); 3.0–4.2 wrote them in
  32-bit words, and the emulator read them that way too. Now the emulator reads them in bytes and
  checks, like Broadcom's simulator, that the shader reads every word loaded; the
  boot probe tries bytes and then words (a job that does not finish rules out only that
  path) and says which one drew. What the optional probes see ends up in the
  report of the `g` test and of the 3D Bench (`gpu3d_probe_log()`).
- **4.4** (2026-10-05, M35): the `zclear()` in the job, which on the Pi kept the colour but
  lost the 3D after it (`07e0 07e0`). The V3D's *early z* keeps its own idea
  of the depth, written only by primitives with *early z updates*: the
  `fs_zclear` quad brings back the far depth from the shader, early z does not know it and discards what
  comes after. After a `zclear()` in the job the primitives go without early z (the z
  test stays, in the tile buffer, in order); the emulator now does early z this way and repeats
  the Pi's pixels exactly. No early z with MSAA either (Mesa, HW-2905: after a load
  early z tracking can keep the values of the tile from before). The quad writes a
  depth just below 1 (0xFFFFF0). The queue also without vertex shader: GPU+Q profile
  of the 3D Bench, Overbit's GPU+Q renderer.
- **4.5** (2026-10-05, M35): the job memory (lists, records, uniforms, vertices) uncached:
  the ARM writes it once and does not read it back, and with the *write-allocate* cache every
  line written was first read from memory and pushed out the lines of Lua and r3d.
  Without the cache the writes leave merged by the write buffer. The block is allocated in whole
  1 MiB sections that the MMU remaps (`mmu_set_cached`, `v3d_uncached`). Option
  `gpu3d_wc=1` (*Settings > Screen and sound > 3D job memory*), off until the Pi
  shows it pays off: the 3D Bench compares it with the GPU+WC profile.
- **4.6** (2026-10-05, M35): up to 8 textures in a job (previously 2: with the third the job
  was closed, and the 3D Bench's `texswap` test was slower on the GPU than on the ARM);
  the texture copies are replaced starting from the least used, never one the open job
  reads if there is another.
- **4.7** (2026-10-05, M34): faces with texture *and* screen-door on the GPU (`fs_tex_lit_screen`,
  `fs_tex_rgb_screen`: the texel on the pixels with even x + y, where it is not transparent), also
  in the vertex shader meshes: it was the last case that passed the frame to the ARM
  (`r3d_t.arm_hook`, the backend now declares `tex_screen`). 3D Bench test `quad_texscreen`,
  scene `tex screen` of `make test-gpu3d`.
- **4.8** (2026-10-05, M37): the **2D over the 3D in the GPU job** (`gpu3d_2d=1`,
  *Settings > Screen and sound > 2D over the 3D*): rectangles, `rect`, `pset`, horizontal
  and vertical lines, sprites (`spr`, `sspr`, also flipped and enlarged by an integer
  factor), the cells of `map()` and the text of `print` (glyphs from a font texture,
  `fs_text`) become quads in the same job, without z test or write: the 3D
  after them covers them where it is closer than the earlier 3D, as on the ARM. Pixel for pixel like
  gfx16 (the texel at the centre of the pixel, the RGB565 colour that comes back unchanged from the tile buffer;
  `make test-gpu3d` scene `2D on GPU`, `make test-queue2d` with the option: the same
  frames). What the GPU does not do identically (circles, oblique lines, triangles,
  `prompt`, non-integer zoom) closes the job and the ARM draws it as before. A HUD between the
  3D and the 3D no longer closes the job: the 3D Bench's GPU+2D profile (`split`, `match`,
  `gpu2d`), step 16 of the `g` test (the same HUD from the ARM and from the GPU, the pixels equal).
  Then **filtered textures** (bilinear, `gpu3d_filter=1`, *3D textures*): the
  look changes compared with the ARM (and on the edges of a sheet's regions, the colour of the neighbouring
  region); the 2D stays at the nearest texel. 3D Bench test `bilinear`.
- **4.9** (2026-10-05, M37): the **ARM**'s 3D with a single object→camera matrix and the
  light in model space (sun, high, V and H rotated once per model without
  bones, the normals not rotated): `r3d_fast=1`, *3D on the ARM: Fast*; also on the RGB30.
  The pixels are no longer bit-identical (in `bench3d` only the scenes with the
  map change); `count_insns`: −2.2% spheres, −3.6% Gouraud, −2.6% room in the ARM's per-triangle
  work. Off, the path is the one from before (same checksums).
- **5.0** (2026-10-05, M39): **larger, indexed meshes**. A model, a `mesh()` and
  r3d reach 65535 vertices and 65535 faces (previously 4096 and 16384: a 10 000-triangle
  model had to be split; `bm.h`, `mkbm`/`bmmesh.py`, bm Studio, bm Mesh). On the GPU the
  groups of a mesh keep vertices that are equal word for word only once and
  draw them with 16-bit indices (`INDEXED_PRIMITIVE_LIST`): the V3D sets up once a
  vertex that lies on several faces (smooth shapes, maps with per-vertex light). A
  boot probe (`probe_index`, a quad of two triangles with two shared vertices)
  turns it off if it does not match (`indexed no` in the status line). 3D Bench tests `big` and `big_logic`:
  models of 10 080 triangles on a map of 14 112.
- **5.1** (2026-10-05, M39): **two jobs in flight** (`gpu3d_queue=2`, *3D frame queue: On, 2
  jobs*): a job's memory in two blocks, the next job is filled in the other
  while the GPU draws the previous one (it is waited for only once, when it starts). The
  depth between jobs, the shaders and the probe stay in the first block. The ARM waits for the
  GPU before touching a page only if it is the one of the job in flight (`gpu3d_sync_page`).
  The emulator now runs a started job when it is waited for, like the V3D: memory
  reused under a job or a page touched before waiting for it would give a wrong
  image. 3D Bench profile GPU+VS+Q2 (`queue`, `split`).
- **5.2** (2026-10-05, M39): **16-bit textures**: a fully opaque sheet goes to the GPU as
  RGB565 in T-format (`gpu3d_tex16=1`, *3D textures: ..., 16-bit*): half the memory and half the
  TMU reads, the same colours. The layout (4 KiB tiles of 64×32 texels) is learnt by
  the boot probe as for 32-bit textures: a 128×64 texture where word *i*
  is *i* and comes back on the page as itself (or with red and blue swapped). Sheets with
  transparent pixels stay at 32 bits. 3D Bench profile GPU+T16.
- **5.3** (2026-10-05, M39): **two-thread pixel shaders** (`gpu3d_fs2=1`, *3D pixel
  shaders: Two threads*): faces with opaque textures (`fs_tex_lit`, `fs_tex_rgb`) have a
  `_t` version that requests the texel, hands the QPU to the other thread (`lthrsw`) and reads it on
  return: while the TMU reads, the QPU shades the other thread's pixels instead of waiting.
  `qpuasm.py` checks Mesa's rules (`vc4_qpu_validate`: no switch with the
  scoreboard held, accumulators lost after the switch, texels read after a switch). The
  boot probe (`probe_fs2`) draws the same quads with both shaders and keeps them
  only if the pixels are identical (`two-thread shaders yes` in the status line); a job
  that does not finish turns them off and the rest goes on. 3D Bench profile GPU+FS2
  (`spheres_tex`, `heroes_tex`, `quad_tex`, `match`).
- **5.4** (2026-10-05, M39): **draw order** (`gpu3d_sort=1`, *3D draw order: Nearest
  first*): the vertex shader mesh draws that test and write z are set
  aside (each with all its state) and go into the job from nearest to farthest
  (the depth of the bone's origin) at the first draw of another kind (r3d
  triangles, shadows, 2D, `zclear()`) or at the end of the job: the V3D's early z discards the
  hidden pixels instead of shading them. Same image (except where two faces have the
  same depth; the groups of a mesh stay in their order). In the emulator eight
  boxes from far to near: 99 960 pixels shaded instead of 416 546, 0 pixels different.
  3D Bench profile GPU+VS+S (`spheres`, `heroes`, `match`, `big`, `big_logic`).
- **5.5** (2026-10-05, M39): **what is visible, for actors too**: `visible3d(x, y, z, r)`
  says whether a sphere can be on screen (r3d's test on models, `r3d_visible`) and,
  with the map's precomputed visibility given by `pvs3d{...}` (cells on the ground, the
  pieces seen from each one, the pieces' boxes), whether the camera's cell sees a piece it
  stands on. Overbit gives it at map start and does not draw the heroes behind walls
  (previously only those outside the view); shadows stay (at sunset they come out from the
  walls). It also holds on the ARM and on the RGB30. Tests in `make test-gameapi`.
- **5.6** (2026-10-05, M39): with the same option (*3D draw order: Nearest first*) the triangles of
  each group of an indexed mesh go in the order that best reuses the V3D's cache of already
  shaded vertices (Tom Forsyth's linear-speed optimization for a cache of 32), and the
  vertices are renumbered in order of first use (the VCD reads them in sequence). In the emulator, which
  now has a model of the cache (16, FIFO): a grid of 1800 triangles in scattered order goes
  from 2.96 to 0.69 shaded vertices per triangle, same pixels. Changing the option redoes the copies
  of the meshes.
- **6.0** (2026-10-05, M41): **the RGB30's Mali-G52 GPU**, first step (`src/rgb30/mali.c`,
  *Dev > GPU test*): vdd_gpu, clocks and the PD_GPU power domain, identity, reset,
  powering up the cores, MMU (address space 0, Mali LPAE tables as panfrost on the RK3568) and job
  manager jobs (WRITE_VALUE and a chain of two). It does not draw yet: the RGB30's 3D stays on the ARM
  and the 3D Bench says so in the machine line. Tests on the PC with a simulated GPU
  (`make TARGET=rgb30 test-mali`).
- **6.1** (2026-10-05, M41): **the Mali writes pixels**: a fragment job on slot 0 without
  draws (no tiler, no shaders) clears the 16×16 tiles and writes them through the
  render target: Bifrost's (v7) framebuffer descriptor as it is in Mesa (parameters,
  sample positions, an R8G8B8A8 render target written linear with the channels swizzled for
  the screen's XRGB8888, the "clean" pixels written). First a 64×64 surface checked
  pixel by pixel, then a green square at the top right of the screen of the *GPU test* page,
  redrawn last over the text: if it shows, the GPU has drawn.
- **6.2** (2026-10-05, M39): on the Pi the 3D Bench stopped in the `match` test with GPU+FS2 (the
  GPU did not finish the job): the shaders that do not switch threads were declared "multi-threaded"
  in the record (bm3d 2.0–5.3, harmless as long as nobody switched threads), while a multi-threaded
  shader must do LTHRSW once before finishing (Mesa, `vc4_program.c`). Now bit 0 of the
  record ("single-threaded") is set for all except `fs_tex_lit_t` and `fs_tex_rgb_t`, as in
  Mesa; the emulator rejects a record that declares multi-threaded a shader without switches, and the boot
  probe of the two-thread shaders also puts single-thread shaders in the same job. On the
  Pi GPU+FS2 had given +22% in `quad_tex` (148 quads against 121).
- **6.3** (2026-10-05, M39): the Pi's reports showed that 6.2 costs: the fill tests
  without textures lost 8–14% (`quad_smooth` 198 → 171, `quad_screen`
  184 → 160), and Overbit at 1920×1080 ran slower than with v0.2.3 (3D 22.0 → 29.9 ms; with
  MSAA 22.9 → 42.4). Two things: with **GPU+FS2** the other 3D shaders now have two threads too
  (`fs_colour_t`, `fs_colour_screen_t`, `fs_tex_lit_alpha_t`, `fs_tex_lit_screen_t`,
  `fs_tex_rgb_alpha_t`, `fs_tex_rgb_screen_t`: the colour in the register file, then `lthrsw` before
  the scoreboard), checked by `qpuasm.py`, which runs them pixel by pixel alongside the
  originals (same colour and depth writes, whatever the other thread leaves) and
  by the boot probe, which puts them all in one job; and with **MSAA** early z stays in
  the jobs that clear the page (HW-2905 concerns only a job that loads it: 6.2
  always removed it, v0.2.3 never). The rest is as in 6.2: without FS2 the shaders stay
  single-threaded.
- **6.4** (2026-10-06, M39): the Pi measured GPU+FS2 in every test. With textures the two
  threads always win (`quad_tex` 111 → 148, `quad_alpha` 105 → 136, `quad_texscreen` 102
  → 122; Overbit at 1920×1080 from 62.6 to 56.0 ms); with colour they lose 4–5% (there is no texel to
  wait for). So the textured shaders have **two threads by default** (`gpu3d_fs2=0` puts them
  back to one, *Settings > Screen and sound > 3D pixel shaders*; the 3D Bench's GPU row is
  2.1 single-threaded) and `fs_colour_t`/`fs_colour_screen_t` are removed. And `fs_colour`,
  `fs_colour_screen` wait for the scoreboard last, just before writing, like Mesa
  (previously they took it at the fifth step and held it while computing the colour: overlapping
  pixels queued up, and in some 3D Bench runs the colour quads ran from
  2 to 4 times slower for a whole test).
- **6.5** (2026-10-06, M36): the first report with the score (`v0.2.3-50`) says that with the default
  driver the limit is the ARM (in the heroes the GPU works 1.2 ms out of 16.5; in the `mix` scene 1–2 ms
  out of 13.5), so the lever is the **vertex shader**, which has never drawn on the Pi. The boot
  probe now, after the two earlier modes (VPM in bytes and in words), also tries **the Mesa way**: the
  clipper on in every draw and its scale written (Mesa never turns it off; we only did for the
  meshes inside the guard band). If it draws this way, GL draws stay this way (`gl_clip_all`, "vertex
  shader yes (clipper always on)" in the status). In any case it writes in the probe log the V3D's error
  registers (`ERRSTAT`, `DBGE`, `FDBGO`, `FDBGB`, `FDBGR`, `FDBGS`, `v3d_errors`) before
  the probe and after the first draw of each mode that leaves nothing: the errors of the VPM and
  of the front end say where it stops. And r3d counts shadows among the triangles (previously not: the triangles
  at 60 fps of the `mix` scene of `-50`, 6053, did not include shadows). The 3D Bench writes at each
  step the measured clock of core and V3D: 6.4 did not remove the slow quads (`quad_flat` 91 in
  `-50`, 175 in `-46`).
- **6.6** (2026-10-06, M36): on the Pi the vertex shader probe draws only with the clipper on
  (without it, `ERRSTAT` says *VPM write range*: the front end then wants a shorter vertex from a
  coordinate shader), so GL draws always keep it on, like Mesa. Step 14 of the
  `g` test gives 0.0% different pixels; in the 3D Bench GPU+VS does spheres 4×, heroes 11×, the
  `mix` scene 9× (79 071 triangles at 60 fps). The vertex shader is **on by default** (`gpu3d_vs`
  is 2 if missing; `gpu3d_vs=0` turns it off); the 3D Bench has the **GPU+VS+FS2** profile (the games'
  driver of 6.6: vertex shader and textured shaders on two threads) in every GPU test, and
  the score uses that.
- **6.7** (2026-10-06, M36): the first 3D Bench with the vertex shader by default (`v0.2.3-52`, score
  2601, 78 412 triangles at 60 fps in `mix`) showed two things that cost. `texswap` (three
  textures in turn) from 1436 to 6.6 quads: the GPU's mesh cache had one slot per mesh, and the
  same mesh with another texture redid its copy of the vertices, drawing the open job
  first: one job per quad. Now one slot per mesh **and** sheet (`mesh_is`): one copy per texture,
  one job per frame (`vshader sheets` test of `make test-gpu3d`). And the textured quads at
  half speed (`quad_tex` 148 → 76): `vs_lit_tex` and `vs_lit_tex2` computed the grey light like the ARM but
  sent it as three equal colours and three fog zeros, the eight varyings of `fs_tex_rgb`; now they
  send s, t, k to `fs_tex_lit` (`SH_TEX`, `SH_TEX_ALPHA`, `SH_TEX_SCREEN`), like the
  NV path. Meshes with light on the vertices and fog (the maps) stay on `fs_tex_rgb`. On the Pi
  (`v0.2.3-79`): score **3506**, `texswap` 1302, `quad_tex` 132.
- **6.8** (2026-10-06, M36): with 6.7 (`v0.2.3-79`, the first `bm3d-driver` kernel also with
  Audio 2, R12-R14 and bm Write) Overbit stopped with a *data abort* inside `free()` (at
  frame 245), and sometimes a game did not start or everything slowed down: the heap was corrupted (in the
  block size read by `free()` there was a float, 0.16). Looking for the cause: the indexed
  copy of a mesh's vertices
  starts each group on 16 bytes, but the space was that of the vertices alone: a mesh of flat
  faces (no vertex equal to another) over several groups (bones, levels of detail) wrote up
  to 12 bytes per group past the end, over the header of the next block or over the indices (broken
  GPU jobs, which end in a timeout). On the Pi it could happen only since the vertex
  shader draws (6.5) and is on (6.6). Now the space of each group is rounded as when it
  is written. The emulator puts 16 guard bytes after each block (`test_arena_overruns`) and the
  `vshader groups` scene (four flat triangles, one per bone) tests it: before, it wrote past a
  672-byte block and the GPU read index 34953. Overbit in the emulator (`bmhost-gpu`, which now
  checks the guards at the end) does not overrun, not even before the fix: whether it was the cause on the
  Pi remains to be seen. To find out, from this kernel a crash becomes a report (`crash`, with the
  last lines printed and the return addresses on the stack) and so does a hang of the Pi (`freeze`).

## The modes: old versions on today's code

The settings reproduce the earlier versions, so they can be compared on the same Pi:

- 3D on the ARM (`gpu3d=0`): **0.2**;
- GPU without vertex shader (`gpu3d_vs=0`): **2.1** (with `gpu3d_aa=1` also MSAA);
- GPU with the vertex shader for the scenery (`gpu3d_vs=1`): **3.0**;
- GPU with the vertex shader for everything (`gpu3d_vs=2`): **3.4** (the default from 6.6, with the
  textured shaders on two threads: GPU+VS+FS2 in the 3D Bench);
- with the queued frame (`gpu3d_queue=1`): **4.1** (with or without vertex shader);
- with uncached job memory (`gpu3d_wc=1`): **4.5**;
- with the 2D over the 3D in the job (`gpu3d_2d=1`): **4.8**;
- with two jobs in flight (`gpu3d_queue=2`): **5.1**;
- with 16-bit textures (`gpu3d_tex16=1`): **5.2**;
- with two-thread pixel shaders (`gpu3d_fs2=1`): **5.3**;
- with meshes from the nearest and triangles in cache order (`gpu3d_sort=1`, with the
  vertex shader): **5.4** and **5.6**.

What 4.2, 4.3, 4.4 and 4.6 added (screens up to 1080p, the GPU's `cls()`, the
GL record in bytes, no early z after a `zclear()` in the job, 8 textures in a job)
holds in all GPU modes: it cannot be turned off.

0.1 and 1.0 no longer run: their numbers are the ones measured on the Pi at the time
(`docs/M33-PRIMA-DOPO.md`).

## The rule

Each new block (a 3D milestone) raises X, each step that changes what the
driver can do raises Y: change `BM3D_VERSION`, add a line to the table in
`version3d.h` and to this page, and if the step can be turned off, a mode.
