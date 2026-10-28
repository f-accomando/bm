# bm's 3D drivers: the versions



bm's 3D goes through two drivers: **r3d** (`src/bm/r3d.c`: scene, transforms, light,
the ARM rasterizer) and **gpu3d** (`src/gpu/gpu3d.c` with the shaders from
`tools/qpuasm.py`: the V3D backend). They have a single version, **bm3d X.Y**: X is the
development block (a milestone), Y the step within the block. The version is in
`src/gpu/version3d.h` (`BM3D_VERSION`, the `bm3d_versions()` table).

Where it shows: the GPU status line (log), *Settings > System > 3D
driver*, the log when a game starts ("the 3D is drawn by the GPU as bm3d 3.0"), the
3D Bench, the Overbit benchmark and the fourth value of `gpu3d()`.

## The versions

- **0.1** (until September 2026, M7–M32): ARM only, the first r3d rasterizer (the
  September stress test: 31 spheres at 60 fps).
- **0.2** (2026-10-01): ARM only, fixed-point edges, specialized texture
  loops, z cleared with DMA (69 spheres at 60 fps on the Pi).
- **1.0** (2026-10-01): the GPU draws the triangles (NV shaders), the ARM sets them up and
  lights them; z preserved from one job to the next.
- **2.0** (2026-10-01): clean pages without load, T-format textures, 4× MSAA, fewer
  ARM instructions per triangle (182 spheres at 60 fps on the Pi).
- **2.1** (2026-10-03): Overbit on the GPU: screen-door faces, precomputed RGB light and
  fog on the vertices, shadows, 3D effects.
- **3.0** (2026-10-03): the GPU's vertex shader sets up the scenery (unlit models
  or with the light on the vertices); the GPU clips on the near plane and on the guard band.
- **3.1** (2026-10-03): the vertex shader also for the sunlit models, with
  bones (the heroes).
- **3.2** (2026-10-03): the vertex shader also for shadows and the foreground.
- **3.3** (2026-10-03): the sunlit textured models (Meshy's heroes):
  the light of each vertex (Gouraud, grey) on textures too, on the ARM and on the GPU; the
  vertex shader `vs_lit_tex` sets them up with the bones.
- **3.4** (2026-10-03): skins on the vertex shader: the faces straddling two bones (the
  Meshy models are a single surface) in groups per pair of bones, each vertex set up
  by the matrix of its bone as r3d does (`vs_lit_tex2`, `cs_colour2`, the shadows
  `vs_shadow2`/`cs_shadow2`).
- **4.0** (2026-10-03): the queued frame: at the end of the frame the GPU job
  starts and is not waited for (binning increments a semaphore that rendering waits on,
  like Linux's vc4 driver: the two threads start together); meanwhile the `_update`
  of the next frame runs, and the GPU is waited for only before touching the page or the memory
  the job reads. A boot probe (`probe_queue`) turns it off if the V3D does not finish
  the job as it should. Option `gpu3d_queue`.
- **4.1** (2026-10-03): a single GPU job per frame even with the HUD and the
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
- **4.2** (2026-10-04): screens up to 1920×1080 (`screen()`, the resolution changed
  by the cartridge between one frame and the next): tile state, render list and
  depth between jobs sized for 1080p even with MSAA; the vertex guard band
  in 12.4 (±2048 pixels) narrows on wide screens (120 pixels at 1920, previously
  a fixed 1000), the triangles that cross it are clipped by the ARM. A `cls()` with the GPU is no longer
  drawn by the ARM (4 MB at 1080p): the GPU job clears its tiles to that colour;
  the ARM fills the page only if 2D or a read arrives before the 3D, or the page must be
  shown without 3D. It holds for all GPU modes.
- **4.3** (2026-10-05): the vertex shader that drew nothing on the Pi (boot
  probe `0000 0000 0000 0000`). In the GL record the size of the attributes and their
  position in the VPM are in **bytes**, as Mesa writes them ("byte offsets for the start of
  the vertex attributes 0-7, and the total size", `vc4_context.h`); 3.0–4.2 wrote them in
  32-bit words, and the emulator read them that way too. Now the emulator reads them in bytes and
  checks, like Broadcom's simulator, that the shader reads every word loaded; the
  boot probe tries bytes and then words (a job that does not finish rules out only that
  path) and says which one drew. What the optional probes see ends up in the
  report of the 3D Bench (`gpu3d_probe_log()`).
- **4.4** (2026-10-05): the `zclear()` in the job, which on the Pi kept the colour but
  lost the 3D after it (`07e0 07e0`). The V3D's *early z* keeps its own idea
  of the depth, written only by primitives with *early z updates*: the
  `fs_zclear` quad brings back the far depth from the shader, early z does not know it and discards what
  comes after. After a `zclear()` in the job the primitives go without early z (the z
  test stays, in the tile buffer, in order); the emulator now does early z this way and repeats
  the Pi's pixels exactly. No early z with MSAA either (Mesa, HW-2905: after a load
  early z tracking can keep the values of the tile from before). The quad writes a
  depth just below 1 (0xFFFFF0). The queue also without vertex shader: GPU+Q profile
  of the 3D Bench, Overbit's GPU+Q renderer.
- **4.5** (2026-10-05): the job memory (lists, records, uniforms, vertices) uncached:
  the ARM writes it once and does not read it back, and with the *write-allocate* cache every
  line written was first read from memory and pushed out the lines of Lua and r3d.
  Without the cache the writes leave merged by the write buffer. The block is allocated in whole
  1 MB sections that the MMU remaps (`mmu_set_cached`, `v3d_uncached`). Option
  `gpu3d_wc=1` (*Settings > Screen > 3D job memory*), off until the Pi
  shows it pays off: the 3D Bench compares it with the GPU+WC profile.
- **4.6** (2026-10-05): up to 8 textures in a job (previously 2: with the third the job
  was closed, and the 3D Bench's `texswap` test was slower on the GPU than on the ARM);
  the texture copies are replaced starting from the least used, never one the open job
  reads if there is another.
- **4.7** (2026-10-05): faces with texture *and* screen-door on the GPU (`fs_tex_lit_screen`,
  `fs_tex_rgb_screen`: the texel on the pixels with even x + y, where it is not transparent), also
  in the vertex shader meshes: it was the last case that passed the frame to the ARM
  (`r3d_t.arm_hook`, the backend now declares `tex_screen`). 3D Bench test `quad_texscreen`,
  scene `tex screen` of `make test-gpu3d`.
- **4.8** (2026-10-05): the **2D over the 3D in the GPU job** (`gpu3d_2d=1`,
  *Settings > Screen > 2D over the 3D*): rectangles, `rect`, `pset`, horizontal
  and vertical lines, sprites (`spr`, `sspr`, also flipped and enlarged by an integer
  factor), the cells of `map()` and the text of `print` (glyphs from a font texture,
  `fs_text`) become quads in the same job, without z test or write: the 3D
  after them covers them where it is closer than the earlier 3D, as on the ARM. Pixel for pixel like
  gfx16 (the texel at the centre of the pixel, the RGB565 colour that comes back unchanged from the tile buffer;
  `make test-gpu3d` scene `2D on GPU`, `make test-queue2d` with the option: the same
  frames). What the GPU does not do identically (circles, oblique lines, triangles,
  `prompt`, non-integer zoom) closes the job and the ARM draws it as before. A HUD between the
  3D and the 3D no longer closes the job: the 3D Bench's GPU+2D profile (`split`, `match`,
  `gpu2d`), step 16 of the GPU test, removed on 24 Oct 2026 (the same HUD from the ARM and from the GPU, the pixels equal).
  Then **filtered textures** (bilinear, `gpu3d_filter=1`, *3D textures*): the
  look changes compared with the ARM (and on the edges of a sheet's regions, the colour of the neighbouring
  region); the 2D stays at the nearest texel. 3D Bench test `bilinear`.
- **4.9** (2026-10-05): the **ARM**'s 3D with a single object→camera matrix and the
  light in model space (sun, high, V and H rotated once per model without
  bones, the normals not rotated): `r3d_fast=1`, *3D on the ARM: Fast*; also on the RGB30.
  The pixels are no longer bit-identical (in `bench3d` only the scenes with the
  map change); `count_insns`: −2.2% spheres, −3.6% Gouraud, −2.6% room in the ARM's per-triangle
  work. Off, the path is the one from before (same checksums).
- **5.0** (2026-10-05): **larger, indexed meshes**. A model, a `mesh()` and
  r3d reach 65535 vertices and 65535 faces (previously 4096 and 16384: a 10 000-triangle
  model had to be split; `bm.h`, `mkbm`/`bmmesh.py`, bm Studio, bm Mesh). On the GPU the
  groups of a mesh keep vertices that are equal word for word only once and
  draw them with 16-bit indices (`INDEXED_PRIMITIVE_LIST`): the V3D sets up once a
  vertex that lies on several faces (smooth shapes, maps with per-vertex light). A
  boot probe (`probe_index`, a quad of two triangles with two shared vertices)
  turns it off if it does not match (`indexed no` in the status line). 3D Bench tests `big` and `big_logic`:
  models of 10 080 triangles on a map of 14 112.
- **5.1** (2026-10-05): **two jobs in flight** (`gpu3d_queue=2`, *3D frame queue: On, 2
  jobs*): a job's memory in two blocks, the next job is filled in the other
  while the GPU draws the previous one (it is waited for only once, when it starts). The
  depth between jobs, the shaders and the probe stay in the first block. The ARM waits for the
  GPU before touching a page only if it is the one of the job in flight (`gpu3d_sync_page`).
  The emulator now runs a started job when it is waited for, like the V3D: memory
  reused under a job or a page touched before waiting for it would give a wrong
  image. 3D Bench profile GPU+VS+Q2 (`queue`, `split`).
- **5.2** (2026-10-05): **16-bit textures**: a fully opaque sheet goes to the GPU as
  RGB565 in T-format (`gpu3d_tex16=1`, *3D textures: ..., 16-bit*): half the memory and half the
  TMU reads, the same colours. The layout (4 KB tiles of 64×32 texels) is learnt by
  the boot probe as for 32-bit textures: a 128×64 texture where word *i*
  is *i* and comes back on the page as itself (or with red and blue swapped). Sheets with
  transparent pixels stay at 32 bits (RGBA5551 from 7.2). 3D Bench profile GPU+T16.
- **5.3** (2026-10-05): **two-thread pixel shaders** (`gpu3d_fs2=1`, *3D pixel
  shaders: Two threads*): faces with opaque textures (`fs_tex_lit`, `fs_tex_rgb`) have a
  `_t` version that requests the texel, hands the QPU to the other thread (`lthrsw`) and reads it on
  return: while the TMU reads, the QPU shades the other thread's pixels instead of waiting.
  `qpuasm.py` checks Mesa's rules (`vc4_qpu_validate`: no switch with the
  scoreboard held, accumulators lost after the switch, texels read after a switch). The
  boot probe (`probe_fs2`) draws the same quads with both shaders and keeps them
  only if the pixels are identical (`two-thread shaders yes` in the status line); a job
  that does not finish turns them off and the rest goes on. 3D Bench profile GPU+FS2
  (`spheres_tex`, `heroes_tex`, `quad_tex`, `match`).
- **5.4** (2026-10-05): **draw order** (`gpu3d_sort=1`, *3D draw order: Nearest
  first*): the vertex shader mesh draws that test and write z are set
  aside (each with all its state) and go into the job from nearest to farthest
  (the depth of the bone's origin) at the first draw of another kind (r3d
  triangles, shadows, 2D, `zclear()`) or at the end of the job: the V3D's early z discards the
  hidden pixels instead of shading them. Same image (except where two faces have the
  same depth; the groups of a mesh stay in their order). In the emulator eight
  boxes from far to near: 99 960 pixels shaded instead of 416 546, 0 pixels different.
  3D Bench profile GPU+VS+S (`spheres`, `heroes`, `match`, `big`, `big_logic`).
- **5.5** (2026-10-05): **what is visible, for actors too**: `visible3d(x, y, z, r)`
  says whether a sphere can be on screen (r3d's test on models, `r3d_visible`) and,
  with the map's precomputed visibility given by `pvs3d{...}` (cells on the ground, the
  pieces seen from each one, the pieces' boxes), whether the camera's cell sees a piece it
  stands on. Overbit gives it at map start and does not draw the heroes behind walls
  (previously only those outside the view); shadows stay (at sunset they come out from the
  walls). It also holds on the ARM and on the RGB30. Tests in `make test-gameapi`.
- **5.6** (2026-10-05): with the same option (*3D draw order: Nearest first*) the triangles of
  each group of an indexed mesh go in the order that best reuses the V3D's cache of already
  shaded vertices (Tom Forsyth's linear-speed optimization for a cache of 32), and the
  vertices are renumbered in order of first use (the VCD reads them in sequence). In the emulator, which
  now has a model of the cache (16, FIFO): a grid of 1800 triangles in scattered order goes
  from 2.96 to 0.69 shaded vertices per triangle, same pixels. Changing the option redoes the copies
  of the meshes.
- **6.0** (2026-10-05): **the RGB30's Mali-G52 GPU**, first step (`src/rgb30/mali.c`;
  its *Dev > GPU test* page was removed on 24 Oct 2026, `mali3d=1` probes the Mali now): vdd_gpu, clocks and the PD_GPU power domain, identity, reset,
  powering up the cores, MMU (address space 0, Mali LPAE tables as panfrost on the RK3568) and job
  manager jobs (WRITE_VALUE and a chain of two). It does not draw yet: the RGB30's 3D stays on the ARM
  and the 3D Bench says so in the machine line. Tests on the PC with a simulated GPU
  (`make TARGET=rgb30 test-mali`).
- **6.1** (2026-10-05): **the Mali writes pixels**: a fragment job on slot 0 without
  draws (no tiler, no shaders) clears the 16×16 tiles and writes them through the
  render target: Bifrost's (v7) framebuffer descriptor as it is in Mesa (parameters,
  sample positions, an R8G8B8A8 render target written linear with the channels swizzled for
  the screen's XRGB8888, the "clean" pixels written). First a 64×64 surface checked
  pixel by pixel, then a green square at the top right of the screen of the removed *GPU test* page,
  redrawn last over the text: if it shows, the GPU has drawn.
- **6.2** (2026-10-05): on the Pi the 3D Bench stopped in the `match` test with GPU+FS2 (the
  GPU did not finish the job): the shaders that do not switch threads were declared "multi-threaded"
  in the record (bm3d 2.0–5.3, harmless as long as nobody switched threads), while a multi-threaded
  shader must do LTHRSW once before finishing (Mesa, `vc4_program.c`). Now bit 0 of the
  record ("single-threaded") is set for all except `fs_tex_lit_t` and `fs_tex_rgb_t`, as in
  Mesa; the emulator rejects a record that declares multi-threaded a shader without switches, and the boot
  probe of the two-thread shaders also puts single-thread shaders in the same job. On the
  Pi GPU+FS2 had given +22% in `quad_tex` (148 quads against 121).
- **6.3** (2026-10-05): the Pi's reports showed that 6.2 costs: the fill tests
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
- **6.4** (2026-10-06): the Pi measured GPU+FS2 in every test. With textures the two
  threads always win (`quad_tex` 111 → 148, `quad_alpha` 105 → 136, `quad_texscreen` 102
  → 122; Overbit at 1920×1080 from 62.6 to 56.0 ms); with colour they lose 4–5% (there is no texel to
  wait for). So the textured shaders have **two threads by default** (`gpu3d_fs2=0` puts them
  back to one, *Settings > Screen > 3D pixel shaders*; the 3D Bench's GPU row is
  2.1 single-threaded) and `fs_colour_t`/`fs_colour_screen_t` are removed. And `fs_colour`,
  `fs_colour_screen` wait for the scoreboard last, just before writing, like Mesa
  (previously they took it at the fifth step and held it while computing the colour: overlapping
  pixels queued up, and in some 3D Bench runs the colour quads ran from
  2 to 4 times slower for a whole test).
- **6.5** (2026-10-06): the first report with the score (`v0.2.3-50`) says that with the default
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
- **6.6** (2026-10-06): on the Pi the vertex shader probe draws only with the clipper on
  (without it, `ERRSTAT` says *VPM write range*: the front end then wants a shorter vertex from a
  coordinate shader), so GL draws always keep it on, like Mesa. Step 14 of the
  `g` test gives 0.0% different pixels; in the 3D Bench GPU+VS does spheres 4×, heroes 11×, the
  `mix` scene 9× (79 071 triangles at 60 fps). The vertex shader is **on by default** (`gpu3d_vs`
  is 2 if missing; `gpu3d_vs=0` turns it off); the 3D Bench has the **GPU+VS+FS2** profile (the games'
  driver of 6.6: vertex shader and textured shaders on two threads) in every 3D Bench test, and
  the score uses that.
- **6.7** (2026-10-06): the first 3D Bench with the vertex shader by default (`v0.2.3-52`, score
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
- **6.8** (2026-10-06): with 6.7 (`v0.2.3-79`, the first `bm3d-driver` kernel also with
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
- **6.9** (2026-10-11): **each frame shown a frame later** (`gpu3d_later=1`, *Settings >
  Screen > 3D frame shown: A frame later*, the sixth argument of `gpu3d()`; with the two
  jobs in flight of 5.1, `gpu3d_queue=2`). At the end of a frame its job starts and the frame is
  held: its page and the 2D recorded after the job started (`held` in `runtime.c`). The next
  `_update` and `_draw` run on the next page while the GPU draws it, and its 3D fills the job's
  other block (5.1 had no runtime that did it: the queue's early `_update` cannot draw 3D); at the
  end of that frame its job starts (the V3D still runs one job at a time: `job_wait` before
  `v3d_start`), the held frame gets its 2D and is shown with a flip. Three pages: one shown, one
  held, one drawn. The pictures are the same, a frame later; no `_update` runs early. Before
  something that recorded 2D reads changes (`mset`, `fset`, `sset`, the map's size and layers,
  `cart_load`) the held frame's 2D goes on its page first; a question of the system and the
  debugger show it first. Left out online (lockstep), via RAM, in a square page and in the frame
  of a `screen()` change. The page drawn left the screen at the last flip (without the option:
  two flips before), so it counts on the flip's wait for the vertical blank. `make test-queue2d`:
  the same pictures with the option as without the queue, and the next frame's job filled while
  the GPU draws the one before (cartridge `held`). Off until the Pi measures it.
- **7.0** (2026-10-12): **textured faces as flat colours**. `draw3d` flag 128 draws a
  mesh's textured faces each in one colour, the average of its texels (the centre and three
  points toward the corners; transparent texels skipped, a face of holes only is grey
  `0x808080`); the same mesh, bones and animation, lit faces keep their light, the faces
  without texture do not change. Per draw (the flag) or for the whole game
  (`textures3d(false)`, `r3d_t.notex`). The ARM draws them as colour faces (`flat_prepare` in
  `r3d.c`, colours cached per mesh version and texture version); the GPU backend sends the
  triangles of such a mesh one by one (not its copy of the mesh, which has the textures), a
  little slower. No GPU option is needed.
- **7.1** (2026-10-12): **compact attributes** (`gpu3d_attr=1`, *Settings > Screen and
  sound > 3D corners: Compact*; 3D Bench profile GPU+VS+A8). The corners of the meshes of a
  colour carry their bytes packed in 32-bit words, unpacked by the vertex shader as it reads
  them (the QPU's regfile A unpack, now in `tools/qpuasm.py`: `ra13.8a` .. `.8d`, `.16a/.16b`):
  `vs_baked8` reads x y z, the colour (bytes a b c, byte / 255) and the baked light (bytes a b c,
  read as integers and / 128 exactly as the ARM's): **5 words a corner instead of 9**;
  `vs_lit8` (models lit by the sun) x y z, the normal, the colour with the flags in byte d
  (1 emissive, 2 glossy), the lamps' point: **10 instead of 14**. Positions, normals and the
  textured shaders stay in floats. The boot probe (`probe_attr`) draws baked and sun-lit faces
  both ways and keeps the option only if no pixel moves more than a step of RGB565 (status:
  `compact attributes yes`); the emulator draws the same pixels and reads 28% fewer attribute
  words in the `test-gpu3d` scenes. Off until the Pi measures it.
- **7.2** (2026-10-12): **cut-out sheets in 16 bits**: with `gpu3d_tex16=1` a sheet
  with transparent texels (trees, fences, sprites) goes to the GPU as RGBA5551 (TMU type 3)
  in the same 64×32 T-tiles as RGB565, instead of RGBA8888: half the memory and half the TMU
  reads. bm sheets have 1-bit alpha, so only green's last bit is lost (a step of RGB565 at
  most); the transparent texels are the same and the alpha shaders discard them as before.
  The boot probe (`probe_cut16`) draws a 64×32 texture with holes through the alpha shader
  for each order of the fields (red and blue as RGB565 or swapped; alpha in the low bits, as
  Mesa's vc4, or the top ones) and keeps the first that gives back every pixel and every
  hole; none: the sheets stay 32-bit. RGBA4444 (type 2) is probed the same way and unused (no
  sheet has alpha between). Status: `alpha textures RGBA5551 RGBA4444`; options line
  `tex16=2`. 3D Bench: GPU+T16 now also runs `quad_alpha` and `quad_texscreen`.
- **7.3** (2026-10-12): **opaque sheets as ETC1** (`gpu3d_etc1=1`, *3D textures: ...,
  ETC1*, which also sets `gpu3d_tex16=1`; 3D Bench profile GPU+ETC1 = GPU+T16 with ETC1). A
  fully opaque sheet whose sides are multiples of the ETC1 tile (64×128 texels; 128×64 if the
  probe learned the other utile shape) is encoded to ETC1 (TMU type 8, 4 bits a texel: a
  quarter of RGB565's TMU reads) when its texture is made, and again when the sheet changes
  (`src/gpu/etc1.c`: integer encoder, both modes and splits, table and indices by light; about
  23 ms for a 1024×1024 sheet of noise on a PC, half that for flat areas). The colours are near
  the sheet's, not the same: `make test-gpu3d` draws a 128×128 sheet of gradients and checkers
  at 37 dB PSNR against RGB565, worst channel 9 of 255. Cut-outs stay RGBA5551, other sheets go
  as before. The boot probe (`probe_etc1`) draws a 128×256 texture of varied blocks for each
  candidate layout and keeps the first that gives every sampled texel back (a step of RGB565
  allowed, red and blue as they are or swapped): utiles of 2×4 blocks (Mesa's vc4 size for
  64-bit blocks) then 4×2, each placed in the 4 KB tile where RGB565's utile of that row and
  column is; the block's 8 bytes big-endian (the format's, as Mesa uploads them), as two
  little-endian words, all little-endian, or big-endian with the halves swapped. Status:
  `ETC1 textures yes (utiles 2x4, bytes BE)` (the layout found) or `no`; options line
  `etc1=1`; `gpu3d_stats_t.etc1` counts the sheets made. Which candidate the Pi's TMU takes is
  not known until the Pi runs the probe.
- **7.4** (2026-10-22): **cel light on the ARM**. `cel3d(bands, floor)` and `draw3d` flag 256 cut
  the sun term of the light into 2-8 bands with a table of 64 bytes (`r3d_cel`). The ARM
  lighting stage (`light_fast`, `light_obj`, the impostor's light of the whole) reads the table
  once per face (flat draw) or per vertex (smooth draw). The ambient light stays smooth. The
  highlight of `shine3d` becomes one step. Lamps are not cut. `dither3d(false)` turns off the
  ARM raster's 4x4 dither. A cel draw on the GPU is lit by the ARM (the vertex shader has no
  cel cut yet): the triangles go one by one with their colours, as with `vs` 1. Draws without
  the flag do not change. Not measured on the Pi.
- **7.5** (2026-10-22): **cel light on the V3D, outline by the inverted hull**. `tools/qpuasm.py`
  makes a `_cel` variant of each lit vertex program (`vs_lit`, `vs_lit8`, `vs_lit_tex`,
  `vs_lit_tex2`): the same text with a cut block after the sun term `max(n.sun, 0)`: the band
  `min(N - 1, floor(d * N))` (`ftoi`, `min`, `itof`), then `floor + (1 - floor) * b / (N - 1)`;
  four more uniforms (N, N - 1 as an integer, the scale, the floor) after the highlight's `p`. The
  highlight of `vs_lit`, `vs_lit8` becomes one step (on above 0.5). The programs are 2 KB slots
  of the code block, which grows from 40 to 48 KB. `gpu3d.c` takes the `_cel` program of a lit
  group when `r3d_env_t.cel` is set (`r3d_backend_t.cel` tells `r3d.c` that the backend can);
  the boot probe `probe_lit(�, 1)` draws two faces in the top band and the second band and
  refuses the cel programs if the pixels are not r3d's (status `cel light yes/no`): then a cel
  draw is lit by the ARM, as in 7.4. The band edges are r3d's table without its rounding to 1/255
  (the pictures differ by at most a step of RGB565). Flag 512: `r3d.c` makes the hull of a mesh
  once (a mesh of the same faces turned round, each vertex pushed along the normal averaged over
  the faces round its position, by `cel3d`'s width times the mesh's diameter; the faces keep their
  level of detail bits and the bones), caches it in the mesh (`r3d_mesh_t.hull`, freed with it)
  and draws it after the mesh as an unlit mesh in the outline colour, so the ARM, the V3D mesh path
  and the Mali take it through their ordinary mesh calls. No outline for a shadow, a first-person
  model, an impostor, or a camera inside the mesh's sphere. Not measured on the Pi; the hull costs
  the vertex and bin work of the mesh again (the design estimates +40-60% of a hero draw).

- **7.6** (2026-10-23): **draws of many small static meshes merged**.
  `gpu3d_attr` stays off by default (Pi: no gain, `herotris` 54 056 against 54 013). `r3d_t.merge` (key `gpu3d_merge`, hidden, on by
  default; `gpu3d_merge=0` off): `r3d_draw_flags` holds the draws of meshes with baked light (or flag 2),
  no skeleton and no other flag; the held run is drawn before anything else changes the scene or reaches
  the job (`r3d_flush`; the backend calls it through `r3d_backend_t.merge`, `GPU_PRE` in `gpu3d.c`). The
  second time the same run comes (the same meshes, places, turns and order) its pieces become one mesh
  in world axes and the run is one draw: 64 pieces in 2 draws on the PC. Only a run of at least 32 pieces with at most
  150 faces a piece on average merges (Pi, 23 Oct: 100 pieces of 50 faces +24%, 10 of 500 -26% before the threshold).

- **7.8** (2026-10-25): **the Mali draws triangles**: a Vertex, Tiler and Fragment
  job chain draws a 3D triangle into an offscreen/screen surface (RGB30, RK3566,
  Mali-G52 r1p0). Fixed Tiler Job Primitive Size Constant (1.0f at offset 64)
  and hierarchy mask (0x28, matching hardware max_levels = 2 from register
  GPU_TILER_FEATURES 0x00c). Verified on device (report 80d62d1e): inside
  and outside pixels pass.

- **7.9** (2026-10-25): **Mali flat colours 3D Bench**: with `mali3d=1`, the 3D
  Bench on RGB30 runs flat-colour scenes (`spheres`, `spheres_smooth`, `spheres_unlit`,
  `spheres_baked`, `spheres_shine`, `heroes`, `split`, `queue`, `gpu2d`) on Mali-G52
  hardware, while textured scenes fall back to ARM. Verified on hardware (report `94aae9dd`,
  `3d0017.txt`). Multi-draw batching in `mali_frame` requires alignment fix for subsequent
  draw tiler jobs.

- **7.10** (2026-10-25): **Mali multi-draw batching (phase 2)**: fixed Attribute
  Buffer and Tiler Job position alignment to 64 bytes (`first += (nv + 3) & ~3u;`) in
  `frame_build()`, eliminating `DATA_INVALID_FAULT` (0x58) in multi-draw batches.
  Verified on physical hardware (report `fcfd2933`, `3d0018.txt`): frames of up to
  24 draws (26 triangles) complete cleanly without faults.

- **7.11** (2026-10-25): **Mali BM_VS3 vertex shader with 4x4 matrix (phase 3)**:
  verified `BM_VS3` on physical Mali-G52 hardware (report `b041b71a`): model-space
  vertices transformed by a 16-float row-major 4x4 matrix pushed via UBO 0 (`MALI_FRAME_VPUSH`)
  and mixed in the same frame with a `BM_VS` screen-space draw (16 + 27 us, exact
  vertex output `41000000 42b00000 3f000000 3f800000` and pixel check).

- **7.12** (2026-10-25): **Mali asynchronous 2-set queue (phase 4)**: verified
  `mali_frame_submit`, `mali_frame_busy` and `mali_frame_wait` on physical Mali-G52
  hardware (report `9e4652bd`): two frames in flight across `MALI_SET_BASE(0)` and
  `MALI_SET_BASE(1)` overlapping slot 1 (vertex/tiler) and slot 0 (fragment) complete
  in 204 us with exact pixel checks on both surfaces.

- **7.13** (2026-10-25): **Mali `BM_VS3` mesh path and async queue in `mali_backend`**:
  wired `mali_backend.mesh` (`be_mesh`) with per-bone back-face culling, per-face lighting/cel/fog,
  2-mode batching (`BM_VS3` vs screen-space colour merge when >64 runs), 64-bit 4-pixel `be_resolve`,
  and async Slot 0 Fragment Job overlap (`mali_backend_submit` / `mali_backend_sync`, `gpu3d_vshader=1/2`,
  `gpu3d_queue=1/2`). Verified on RGB30 hardware in 3D Bench (report `7509aa08`): `draws` 7.4x faster
  (107.1 vs 14.4 @ 60 fps), `spheres_unlit` 5.2x faster (17.2 vs 3.3), `split` 2.7x faster (14.6 ms vs 39.0 ms).

- **7.14** (2026-10-25): **Mali cached 2 MB resolve surface (`dc civac`) and 8192-triangle merge-sorted batching**:
  mapped the 2 MB resolve surface (`PLAT_GPU_START - 2 MB`) as `ATTR_NORMAL` (Write-Back Cacheable) with `dc civac`
  D-cache invalidation before an 8-pixel (32-byte) `be_resolve` loop, and expanded `be_tris` to 8,192 triangles with
  an O(n log n) stable merge sort. Verified on RGB30 hardware in 3D Bench (report `e259a8c8`): overall GPU score rose
  10x (`14` -> `138`), `quad_flat` reached `211.5` @ 60 fps (`457.0` @ 30 fps, beating Pi Zero W V3D's `174.5`),
  `draws` reached `509.8` @ 60 fps (`1,112.6` @ 30 fps, 35x faster than `7.10`), and `spheres_unlit` reached `92.5`
  @ 60 fps (`187.3` @ 30 fps, 28x faster than `7.10`).
- **7.15** (2026-10-25): **Default `mali3d=1`, 4 MB single-pass GPU arena (`1,024` draws), and L1-cached descriptor packing**:
  - `src/rgb30/bm_port.c`, `src/rgb30/b3d_rgb30.c`: Enabled `mali3d=1` by default on RGB30 (`gpu3d_vs=2`, `gpu3d_queue=1`, `gpu3d_merge=1`), disabling only when `mali3d=0` or `gpu3d=0`.
  - `src/rgb30/mali.c`: Added `frame_build_ex()` with L1-cached stack buffers (`hdr[0x1000]`, `loc[0x540]`, `frs_buf[80]`, `vrs3_buf[64]`) eliminating uncached `ATTR_NC` byte read-modify-writes, plus a 4 MB single-pass GPU arena at `H->mem - (6u << 20)` (`1,024` draws and `8,192` triangles in 1 Fragment Job and 1 `be_resolve` per frame).
  - Verified on RGB30 hardware in 3D Bench (`docs/bench/mali-2026-10-25/step9-bench3d-singlepass-default.txt`, report `b3cab428`): overall `games` score rose from `192` to **`247`**, `GPU` score rose **16.3x** (`14` in `7.10` -> `138` in `7.14` -> **`228`** in `7.15`), `draws` (`GPU+VS`) reached `533.8` @ 60 fps (`1 job`), `spheres` (`GPU+VS`) reached `22.9` @ 60 fps (`141.5` @ 30 fps), and `gpu2d` reached `1,849.2` @ 60 fps (`5,354.8` @ 30 fps).

- **7.22** (2026-10-28): **Bilinear textures on the Mali** (RGB30, `gpu3d_filter`). The Sampler of each sheet filters (four texels a pixel) when the scene asks for it; before, those faces went to the ARM. 3D Bench `bilinear` test: 126.6 (quads of 320 x 180).
  The option table of the Mali (`gpu3d_*`) is in `docs/progress/mali-parity.md`.

- **7.21** (2026-10-28): **Skinned meshes lit by the Mali vertex shader** (RGB30, key `mali_lit`). `BM_VS_LITB` picks the matrix and the light of a vertex's bone from a palette in the uniform buffer (up to 32 bones): a hero is one draw, not one a bone.
  RGB30 3D Bench: `heroes` GPU+VS 3.4 -> 6.8 (ARM 15.8); the other scenes do not change. The 3D Bench pixel check has a `KC` line and tries four times the load when the first frame is blank.

- **7.20** (2026-10-28): **Rim light, lamps, gloss and fog in the Mali vertex shader** (RGB30, key `mali_lit`). A rigid lit mesh with any of them is one draw with `BM_VS_LITX` (the light block is read from the uniform buffer, 64-byte vertices with the face centre).
  RGB30 3D Bench: `spheres_shine` 48.5 -> 125.7; the other scenes and the pixel checks do not change. Cel levels and skinned meshes stay on the ARM.

- **7.19** (2026-10-28): **Sun and ambient light in the Mali vertex shader** (RGB30, keys `mali_lit` default 1 and `mali_lit_min` default 100). A rigid lit mesh with only the sun and the ambient light is one draw: `BM_VS_LIT` lights each face (flat) and `BM_FS_VCOL` writes the colour.
  RGB30 3D Bench: `spheres` GPU+VS 102.5 -> 132.0, `queue` 71.9 -> 119.7, `split` 9.2 -> 12.9; the pixel check is the same as with the light on the ARM. Rim light, lamps, gloss, cel, fog and skinned meshes stay on the ARM.

- **7.18** (2026-10-28): **Hardware depth test on the Mali** (RGB30, key `mali_z`, default 1). The tile buffer tests "less or equal" against a depth cleared to 1. Each vertex has z = 1 - near / depth.
  The backend does not sort the triangles when one frame holds all of them. RGB30 3D Bench, same kernel: `mali_z=0` games 531, GPU 571; `mali_z=1` games 639, GPU 755.

- **7.17** (2026-10-28): **Opaque textures on the Mali** (RGB30). `BM_VS_TEX` and `BM_FS_TEX` (`tools/mali/mkshader.c`) draw a textured face with a nearest sample of a RGB565 page. `mali_tex=0` sends textures back to the ARM.
  A 2 MB texture pool sits at `mem - 8 MB`. The 3D Bench has a pixel check: the GPU frame against an ARM redraw (report lines `K`, `KD`, `KG`).
  RGB30, same kernel, `bench=all`: `mali_tex=0` games 482, GPU 495; `mali_tex=1` games 530, GPU 570. Alpha, screen and bilinear textures stay on the ARM.

- **7.16** (2026-10-27): **`gpu2d_only=1`: the 2D of a game with no 3D on the GPU** (hidden key, off by default; the default stays what the Pi verified).
  Until 7.15 the 2D went to the V3D only when a 3D call had opened the job (`gpu3d_pending()`), so a 2D game (Yharnam) was all ARM.
  With the key, `gpu2d_only_frame()` (`runtime.c`) makes the 3D state and the GPU at the first frame whose page is whole (a game drawing via RAM: after
  `dark_begin`), `gpu2d_open()` takes the quad of a rectangle, a sprite, a map cell or a row of a line with no job open (the job opens with the quad), and
  `dark_begin()` starts the job (`gpu2d_submit`) so the GPU draws the frame while the ARM makes the lights; `dark_end()` waits for it before the fade. What the GPU
  does not take (circles, slanted lines, stacks) is recorded and drawn after the job, in order, as in a game with 3D. The RAM page is on a 64-byte line
  (`shadow_alloc`). The F11 overlay shows a row `2D: GPU wait x.y` and the session report a line `2D: GPU`.
  Check: `make test-gpu2d-only` (V3D emulated: a plain cart, a cart with light and Yharnam, the same pictures with the key on and off, pixel for pixel),
  `make test-gpu3d`, `test-queue2d`, `test-v3d`, `test-b3d`. `gpu2d_sheet(sx, sy, w, h)` (Lua, `draw` API): a window of a sheet over 2048 x 2048 as a texture; the sources inside it go to the GPU, the others stay on the ARM in order (a `map` call all or nothing). Pi (27 Oct, Yharnam): without the window the sheet of 4096 x 3400 is over the 2048 limit of `tex_get`, so only rectangles reached the GPU (29 quads a frame, draw 17.0 ms against 16.4 on the ARM); with the window the map went to the GPU (about 2650 quads a frame) and it was worse: 44.3 fps against 50.5, because the ARM pays the vertices, the job and the cache upkeep (the light 8.8 ms against 6.3). Off by default.

- **7.7** (2026-10-23): **shadows that darken on the GPU**. Until 7.6 the GPU drew the shadow (`draw3d` flag 8) as
  black on every other pixel, because a fragment shader could not read the screen. `fs_shadow` (`tools/qpuasm.py`) uses the
  colour load signal of `fs_zclear` to read the pixel and keeps 5/8 of each channel, as the ARM's style 0 (`(p >> 1) + (p >> 3)`).
  The depth is tested and not written. `R3D_KIND_SHADE` (r3d to the backend, `r3d_backend_t.shadow_blend`) and
  `r3d_env_t.shade` ask for it; the GPU uses it when the boot probe of `zclear()` in a job passed and MSAA is off, else the
  screen-door as before. `shadow3d(1)` keeps the dither on every backend. Where faces of one mesh overlap after the flattening
  (an arm in front of the torso, seen from the sun) the pixel is darkened once for each face: the ARM darkens it once.
  Not measured on the Pi: the emulator test `shadow darken` (0.02% of the pixels differ from the ARM).

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
- with 16-bit textures (`gpu3d_tex16=1`): **5.2** (opaque sheets), **7.2** (also the cut-outs);
- with two-thread pixel shaders (`gpu3d_fs2=1`): **5.3**;
- with meshes from the nearest and triangles in cache order (`gpu3d_sort=1`, with the
  vertex shader): **5.4** and **5.6**;
- with each frame shown a frame later (`gpu3d_later=1`, with `gpu3d_queue=2`): **6.9**;
- with compact attributes (`gpu3d_attr=1`, with the vertex shader): **7.1** (off by default);
- with the draws of many small static meshes merged (`gpu3d_merge`, on by default): **7.6**;
- with `shadow3d(1)`, the GPU's shadows are the screen-door of **7.6** and before (the default, `shadow3d(0)`, darkens: **7.7**);
- with the opaque sheets as ETC1 (`gpu3d_etc1=1`, with `gpu3d_tex16=1`): **7.3**;
- with cel draws (`cel3d`, flag 256), the ARM lighting of the same version: **7.4** (no setting);
  with `gpu3d_vs=2` the cut is in the vertex programs and the outline (flag 512) is the hull: **7.5**
  (no setting).

What 4.2, 4.3, 4.4 and 4.6 added (screens up to 1080p, the GPU's `cls()`, the
GL record in bytes, no early z after a `zclear()` in the job, 8 textures in a job)
holds in all GPU modes: it cannot be turned off.

0.1 and 1.0 no longer run: their numbers are the ones measured on the Pi at the time
(`docs/GPU-BENCHMARK-REPORT.md`).

## The rule

Each new block (a 3D milestone) raises X, each step that changes what the
driver can do raises Y: change `BM3D_VERSION`, add a line to the table in
`version3d.h` and to this page, and if the step can be turned off, a mode.
