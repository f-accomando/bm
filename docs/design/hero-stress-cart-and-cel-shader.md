# Hero stress cart and cel shader

> Removed 25 Oct: `carts/stages3d`, named below as the model of the layout, was deleted (user decision). The body is history.

> Design only. No code is written by this page. Written 2026-10-22 from `bm-core` at `993ae1f7`.
> Everything that says "proposal" is a decision for the owner. Everything that says "not verified" was not run.

Two goals:

- **A.** A separate cartridge, `herocart`: a plaza with heroes that walk. The player changes the hero count, the hero polygon level and the render options in real time. The cart shows the cost (ms, GPU ms, triangles, fps).
- **B.** A cel shader API (toon shading): the light is cut into a few bands, with an optional outline. The cart tests it.

The Pi Zero W (V3D, RGB565) is the benchmark. The RGB30 (Mali) follows later.
Today the RGB30 draws 3D with the ARM raster (`src/bm/r3d.c`): the Mali triangle step has not run on the device ([graphics-mali](../progress/graphics-mali.md)). So the ARM path comes first, the V3D path second.

## 1. What exists (read before this page)

| Need | Where | Fact |
|---|---|---|
| Options the bench toggles | `src/bm/b3d.c:78` (`prof[]`) | ARM, GPU, AA, VS1, VS, Q, Q2, WC, 2D, T16, FS2, S, A8, ETC1. See [BENCH3D](../BENCH3D.md). |
| Options a Lua cart can set | [light3d.md](../api/light3d.md), [draw.md](../api/draw.md) | `gpu3d(on, aa, vs, queue, impostor, later)`, `textures3d`, `fog3d`, `sky3d`, `shine3d`, `lamp3d`, `light3d`, `screen(w, h)`, `draw3d` flags. |
| Options only the console sets | `bm/config.txt` | `gpu3d_tex16`, `gpu3d_etc1`, `gpu3d_fs2`, `gpu3d_attr`, `gpu3d_filter`. The cart cannot change them. It prints them (the `options` line). |
| `draw3d` flags | [light3d.md](../api/light3d.md) | 1 no z, 2 no light, 4 smooth, 8 shadow, 16/32/48 LOD 2/1/0, 64 front, 128 flat textures. **256 and up are free.** |
| Material bits of a face colour | `r3d.h:85` | Bits 24-31 are used. No free bit. |
| Lighting stage (ARM) | `r3d.c:1333` `light_fast`, `r3d.c:1374` `light_obj`, `r3d.c:1711` (light of the whole model) | Build a `lit_t` (RGB light `l`, specular `s`). `shade_rgb` (`r3d.c:938`) and `vertex_rgb` (`r3d.c:1104`) multiply it with the face colour. The GPU receives the result: RGB per corner (`fs_colour`) or a grey `k` per corner (`fs_tex_lit`). |
| Lighting stage (QPU) | `vs_lit`, `vs_lit8`, `vs_lit_tex`, `vs_lit_tex2` in `src/gpu/shaders.h`, source in `tools/qpuasm.py` | Used with `gpu3d(.., vs = 2)`. Code buffer limit 2048 bytes (`gpu3d.c:71`). The biggest, `vs_lit8`, has 428 of 512 words. |
| Dither | `r3d.c:287` `dither565` | Always on in the ARM raster. **No Lua switch exists.** |
| Level of detail | face bits 24-26 and flags 16/32/48 | A mesh that carries all levels. The loop still visits every face. |
| Polygon reducer | `src/bm/decimate.c`, `scripts/bmdecimate.py`, `tools/bmreduce.py` | Quadric edge collapse. Skeleton and texture seams follow. Needs `libbmdecimate.so` (`make`, WSL). |
| Hero rig and walk clip | `.claude/skills/bm-forge/examples/charkit`, `walk.json` | Organic skin, one bone per vertex. |
| Cost per hero, per LOD (host) | `tests/bm/herobench.c`, `tests/bm/herocost.py` | ARM instructions under `qemu-arm`, LOD 0-3, ARM and GPU backend. Reuse it. |
| Cart layout with two editions | `carts/stages3d` (branch `claude/stages3d-port`, PR 95) | `common/` + `bm/` + `b16/`, `build/build.py`, not in `GAMES`. Reuse it. |
| Dev kit numbers | [system.md](../api/system.md) `stat()` | 1 ms, 2 fps, 4 triangles, 7 vertices, 10 Lua instructions, 16 GPU ms, 18 ARM wait ms. |
| Saving text from Lua | `doc_write(name, bytes)`, `report(kind, text)` | `doc_write` writes in `/docs`. `report` writes in `bm/reports`. **No Lua call writes in `bm/bench/`** today. |

## 2. The cart: `herocart`

### 2.1 Name and sources

- Name: **Hero Stress** (`bm.info` name), folder `carts/herocart/`. The file is `herocart.bm` on the Pi. The `.b16` comes in stage 5 from its own sources (rule: the two editions never share a build).
- Layout (as `carts/stages3d`):

```
carts/herocart/
  common/00_core.lua      locals, constants, the OPT table (every option, its values, how to apply it)
  common/10_world.lua     plaza map (map3d), camera presets
  common/20_heroes.lua    hero state, waypoint walk, LOD pick
  common/30_options.lua   apply() of each option, one function per row
  common/40_stats.lua     the 1 s window, the printed line, the report
  common/90_main.lua      _init / _update / _draw
  bm/50_front.lua         panel layout 640x360, key map
  bm/bm.info
  assets/                 plaza tiles (sheet.png, map_*.csv), hero models (made by build/)
  build/build.py          joins common/ + <edition>/ into src/ (as stages3d)
  build/mkplaza.py        tilemap
  build/mkheroes.py       hero + 4 LODs + 4 hulls (section 3)
```

- The cart is **not** in `GAMES` (`Makefile`): it does not go in `dist/bm` or the Market. Target: `make build/carts/herocart.bm`.
- It is a `.bme` project in the editors. The repository holds the sources; `.bm` files stay read-only.
- Lua only. No C change in stage 1.

### 2.2 The scene

- **Plaza.** A 32x32 tile map built with `map3d_build` (floor, a low wall ring, a fountain, 8 pillars). The map costs a fixed number of triangles. The cart prints it once (`map_tris`), read from `stat(4)` with 0 heroes.
- **Heroes.** The array holds 64 heroes (`MAXH`). The default count is 20. Only the first `n` are drawn.
- **Walk.** All 64 heroes always run the same logic, so the logic cost does not change with `n`:
  - 12 waypoints on a ring and a cross of the plaza. Each hero has a start waypoint and a speed of 0.9-1.5 units/s from a fixed table (no `math.random` in play).
  - Each frame: move toward the next waypoint, turn the yaw by at most 8 degrees, take the next waypoint at 0.3 units. No collision, no avoidance.
  - Animation: one walk clip, one `animate()` call per **drawn** hero, phase = hero index x 0.37. This cost belongs to the render side and grows with `n`: it is printed apart (`anim_ms`, measured with `stat(8)` before and after the loop, display only).
- **Camera.** Three presets: 1 overview (all heroes), 2 orbit (turns round the fountain in 40 s), 3 close (four heroes fill the screen, to see LOD 0). A free mode moves with the stick.
- **Fixed state.** Seed, waypoints and speeds are constants. Two runs with the same inputs draw the same frames. That gives repeatable Pi numbers.

### 2.3 Options (the list comes from `b3d.c`)

Each row has a short name, its values, how the cart applies it and which of the bench profiles it reproduces.

| # | Row | Values | Applied with | Bench equivalent |
|---|---|---|---|---|
| 1 | Heroes | 0-64 | count | `n` of `heroes_tex` |
| 2 | LOD | 0, 1, 2, 3, auto | the model set used (section 3) | `herocost.py` detail 0-3 |
| 3 | Backend | ARM / GPU | `gpu3d(on)` | ARM, GPU |
| 4 | AA | off / 4x | `gpu3d(nil, aa)` | GPU+AA |
| 5 | Vertex shader | off / 1 / 2 | `gpu3d(nil, nil, vs)` | GPU+VS1, GPU+VS |
| 6 | Queue | 0 / 1 / 2 | `gpu3d(nil, nil, nil, queue)` | GPU+VS+Q, +Q2 |
| 7 | Frame later | off / on (needs queue 2) | `gpu3d(.., later)` | bm3D 6.9 |
| 8 | Impostor | off / 8 / 16 / 32 px | `gpu3d(.., impostor)` | `r3d_impostor` |
| 9 | Texture | on / flat colours | `textures3d(bool)` | `heroes` vs `heroes_tex` |
| 10 | Lighting | none / flat / smooth | draw flags 2 / 0 / 4 | `spheres_unlit`, `spheres`, `spheres_smooth` |
| 11 | Z test | on / off | draw flag 1 (heroes sorted far to near in Lua when off) | `R3D_NOZ` |
| 12 | Dither | on / off | **needs `dither3d(on)`** (stage 2). Greyed before. | none |
| 13 | Fog | off / on | `fog3d(colour, near, far)` | `spheres_shine` |
| 14 | Sky and rim | off / on | `sky3d`, `shine3d` | `spheres_shine` |
| 15 | Lamps | 0 / 2 / 4 | `lamp3d(i, ..)` | `spheres_shine` |
| 16 | Shadows | off / on | draw flag 8 | `heroes_shadow` |
| 17 | Alpha | off / cut-out cape / screen-door | cape faces: cut-out texture, or material bit 28 | `quad_alpha`, `quad_screen` |
| 18 | Resolution | 320x180 ... 1280x720 | `screen(w, h)` from the list in [draw.md](../api/draw.md) | the `640x360` bench |
| 19 | Outline | off / on (hull) | draws the baked hull model (section 3.3); from stage 4 a flag | none |
| 20 | Cel bands | off / 2 / 3 / 4 / 5 | `cel3d(bands)` and flag 256. Greyed before stage 2. | none |

Notes:

- Rows 12 and 20 exist from stage 1 as greyed rows, so the panel does not move when a stage lands.
- `tex16`, `etc1`, `fs2` and `attr` are console options. The panel shows them on the `options` line in the header (the same text as `gpu3d_options_line` puts in a bench report), with the hint "Settings > Screen > 3D textures".
- A row the console cannot do (no GPU in QEMU and on the RGB30, `vs` refused, `aa` refused) shows the value that is really in force. The cart reads it back from `gpu3d()` after each change.
- Rows 4 to 8 call `gpu3d` once per change, never every frame.

### 2.4 Controls

The cart uses button names (`btn("name")`) and a `keymap`. The keyboard is read with `keydown()` for the extra keys.

| Action | Pad | Keyboard |
|---|---|---|
| Row up / down | up / down | Up / Down |
| Value less / more | left / right | Left / Right |
| Toggle the row (first/last value) | A | Enter |
| Heroes -1 / +1 | L1 / R1 | `[` / `]` |
| Heroes -5 / +5 | L2 / R2 | Shift `[` / Shift `]` |
| LOD down (more detail) / up | X / Y | `,` / `.` |
| Cel bands -/+ (stage 2) | Select + left / right | `-` / `=` |
| Camera preset 1-2-3, next | Select | `C` |
| Camera move / look (free mode) | left stick / right stick | `W A S D` / `I J K L` |
| Hide the panel | B | `H` |
| Run the sweep (section 2.7) | Start + A | `F5` |
| Save the report | Start | `F6` |
| Quit | system (Ctrl+Esc, Start+Select) | Ctrl+Esc |

A row can also be set directly: digits `1`-`9` and `0` on the keyboard jump to rows 1-10.

### 2.5 The panel

Font: the system 8-pixel font. Rows are 10 pixels. The 3D scene fills the whole screen. The panel is drawn after `draw3d`, on a dark dithered rectangle (screen-door look, no alpha cost).

**640x360 (Pi):**

```
x=0..215            OPTIONS (left, 21 rows x 10 px = 210 px from y=4)
  row 1 title line:  HERO STRESS  draw=gpu vs2 q1
  rows 2-21:         20 option rows:  "> Lighting      smooth"
  last line:         options: tex16 fs2   (console options)
x=420..639, y=250..359   NUMBERS (bottom right, 11 rows)
  ms  9.8  max 12.1  fps 60
  gpu 7.2  wait 0.4  anim 1.1
  tri 31200  vtx 18400  lua 210k
  n 20  lod auto(0:4 1:9 2:7 3:0)  640x360
  base 0.6 ms  map 3400 tri
```

The numbers are the average of the last second (60 frames). `max` is the worst frame in the second. The "lod auto" line counts the heroes drawn at each level.
The bar at the top right (x=420..639, y=4..14) shows 16.7 ms as a full bar: green below, red above. It never decides anything.

**RGB30 720x720 (stage 5):** the 3D fills the screen. Options panel across the top (720 x 11 rows x 14 px: two columns of 10 rows), numbers block at the bottom (720 x 6 rows). The 360x360 mode shows only the numbers block and a six-row window of the options that scrolls with the cursor.

The dev kit overlay (`devkit()`) stays under the player's control. The cart never calls it.

### 2.6 What is printed

Once per second, `log()` (serial and console) and the ring buffer get one line:

```
herocart 1 t=12 n=20 lod=auto res=640x360 opt=gpu,vs2,q1,aa0,tex,smooth,z,dith,fog0,shd0,cel0,out0 | ms=9.8 max=12.1 fps=60 gpu=7.2 wait=0.4 anim=1.1 tri=31200 vtx=18400 lua=210 kb=1840
```

- `ms` is `stat(1)`, `fps` `stat(2)`, `tri` `stat(4)`, `vtx` `stat(7)`, `lua` `stat(10)` (thousands), `kb` `stat(0)`, `gpu` `stat(16)`, `wait` `stat(18)`.
- **Rule.** `stat(16)`, `stat(17)` and `stat(18)` are read only for the panel and the printed line. No `if` in the game logic uses them (the cart has no quality governor). The sweep (2.7) is driven by frame counts, not by `stat`.
- Start saves the last 120 lines plus a header (kernel version, board, the `options` line, the map base) in two places that exist today: `doc_write("HEROCART.TXT", text)` (the player copies it from `/docs`) and `report("herocart", text)` (`bm/reports`, sent to the reports branch when the network is on).
- Stage 1b (small, optional): a call `bench_save(name, text)` that writes `bm/bench/` like the 3D Bench (`3D0001.TXT`). It needs a C change in `runtime.c`. The proposal is to wait: `doc_write` is enough for the first Pi runs.

### 2.7 The sweep

A built-in measurement, so the owner does not have to press keys on the Pi.

- Start + A runs it. For each hero count `n` in 0, 4, 8, 12, 16, 20, 28, 36, 48, 64: wait 30 frames (warm-up, like the bench), then 120 frames, then print a table line `sweep n=.. ms=.. max=.. gpu=.. tri=..` (median `ms`, as the bench).
- The options in force stay as set. LOD is fixed to the chosen level (not auto) during the sweep.
- At the end it prints the load at 60 fps and at 30 fps, interpolated as the bench does, and offers to save.
- Total time: about 25 s.

## 3. Hero meshes and LODs

### 3.1 Source hero

- One textured, rigged hero of about 1500 triangles. Source: the `charkit` template of bm-forge (organic skin, shared vertices at the joints, one bone per vertex), built with `bm_forge.py mesh/skel/anim`, plus `walk.json`.
- The texture is one 128x128 sheet shared by all heroes (as `heroes_tex`). Four tints come from four vertex-colour sets (no extra texture).
- The triangle count of `charkit` today is **not verified**. If it is lower than 1500, the build script subdivides nothing: the level table is scaled from the real count (LOD 0 = source).

### 3.2 LOD levels

| Level | Triangles | Made by | Used |
|---|---|---|---|
| 0 | about 1500 (source) | none | near (under 8 units) |
| 1 | about 800 | `bmreduce --faces 800` | 8-16 units |
| 2 | about 400 | `bmreduce --faces 400` | 16-28 units |
| 3 | about 150 | `bmreduce --faces 150` | over 28 units |

- `build/mkheroes.py` calls `scripts/bmdecimate.py` (`reduce_model`) once per level. Each level is a separate model in the MESH section: `hero0`, `hero1`, `hero2`, `hero3`. The skeleton and the animation are shared (`bmreduce` keeps them). A separate model gives the real vertex and triangle cost of each level.
- The bit-based LOD (face bits 24-26, flags 16/32/48) is not used: the loop visits all faces of the mesh, so it hides the saving. It stays an option for a later "LOD mode: models / bits" row.
- If `libbmdecimate.so` is missing (it needs `make`), a fallback in Python stdlib: **vertex clustering** (grid in the bounding box, merge vertices per cell, keep the bone and the UV of the first vertex of the cell, drop degenerate and duplicate faces, binary-search the cell size to reach the face count). It is lower quality than the quadric reducer, so it is a fallback only. It is a proposal, not needed if the library builds.
- The stored result is committed (`assets/`) so a build needs no tool. `mkheroes.py` also prints the real triangle counts; `tests` check them (section 7).

### 3.3 Outline hulls (stage 1 prototype of the outline)

- For each level, `mkheroes.py` also writes `hero0_o` ... `hero3_o`: the same mesh, vertices pushed 0.02 of the hero height along the vertex normal in the rest pose, **winding reversed** (visible faces are the back faces), one flat colour, emissive bit set, no UV.
- With `Outline = on` the cart draws the hull right after the hero with the same bones. This costs what the real outline costs (a second pass over the mesh), so stage 1 measures the price of stage 4 on the Pi before any C is written.
- Limits: the offset is fixed in the rest pose, so thin parts can show gaps when they bend. It is a measurement tool, not the final look.

### 3.4 Size

Four levels x 2 (hull) = 8 models of 150-1500 triangles. At about 20 bytes a vertex the models section stays under 200 KB (estimate, not verified).

## 4. Cel shader API (proposal)

### 4.1 Calls

| Function | Description |
|---|---|
| `cel3d(bands, [floor, outline_rgb, outline_width])` | `bands` 0 turns it off (default). 2-8 cuts the sun light into that many bands. `floor` 0-1 (default 0.35) is the light of the darkest band. `outline_rgb` (`0xRRGGBB`, or `nil`/`false` for none) and `outline_width` (0.005-0.05, a fraction of the model size, default 0.02) are used by the outline flag. With no argument it returns the four values in force. State of the cartridge: it resets when the cart ends, like `gpu3d`. |
| `draw3d` flag **256** (`CEL`) | this draw uses the cel light. It adds to other flags (`4 + 256` = smooth cel). With `cel3d` at 0 it does nothing. |
| `draw3d` flag **512** (`OUTLINE`) | after the mesh, draw its outline (stage 4). |
| `dither3d([on])` | turns the ARM raster's dither off or on (default on). Returns the old state. Stage 2. Needed by row 12. On the V3D the effect is **not verified**. |

- A flag per draw, not a global switch: the plaza floor can stay smooth while the heroes are cel.
- No material bit is free (bits 24-31 are used), so a per-face cel choice is out of scope.
- Old cartridges do not change: nothing happens without the flag.
- Entries needed (all stages that add them): `docs/api/light3d.md` (rows and flag table), `docs/API.md` index line, `docs/spec/specs.toml` (API entries; the revision rule is in [CONSISTENCY](../spec/CONSISTENCY.md): the owner decides whether `BM_SPEC_REV` goes from 2 to 3 in stage 2 and the cart writes `spec: N` in its `bm.info`), `docs/DRIVERS.md` (a new bm3D version for each of stages 2 and 3; the numbers 7.4 and 7.5 are a proposal). **No config key** is needed (cel is cart state). `docs/spec/divergences.toml` gets a row only if the Mali result differs from the Pi after the Mali alignment.

### 4.2 How it maps to the lighting stage (ARM, stage 2)

The light of a face or vertex is `l = ambient_term + (1 - ambient) * dd * sun`, where `dd = max(n . L, 0)` (`light_fast`, `light_obj`). Cel replaces `dd` by a band value:

```
b  = min(N - 1, floor(dd * N))              N = bands
dd_cel = floor_ + (1 - floor_) * b / (N - 1)
```

- A table of 64 bytes holds `dd_cel` for `dd` in steps of 1/64. `cel3d` rebuilds it (once per call, not per frame). The lookup is one index per face (flat) or per vertex (smooth): about 3 ARM instructions.
- Hook points: `light_fast`, `light_obj` and the "light of the whole" at `r3d.c:1711`, all after `dd` is known and before `lit_t` is filled. The ambient term and the sky/ground mix stay smooth. Specular (`shine3d`) becomes one step: on above 0.5, else off.
- Lamps (`lamp3d`) are not cut in stage 2. A cel scene turns them off.
- **Flat draw** (`4` not set): each face gets one band, so the bands have hard edges.
- **Smooth draw** (`4`): the band value is set per vertex and the raster blends it across the triangle. The edge of a band is as wide as one triangle. On a hero at 150-800 triangles that is 3-6 pixels at 640x360: it reads as a hard line from a distance and as a soft one close up. The width falls with the polygon count, which is the effect the cart shows.
- Textured faces: the grey `k` per corner is cut the same way (`gk[]` at `r3d.c:2193-2213`).
- Cost: the ARM lighting loop only. Expected under 1% of a hero draw. **Not measured.**
- The outline is not part of this stage.

### 4.3 V3D path (stage 3): the choice

Three ways were looked at.

| Way | What changes | Cost on the Pi (estimate) | Verdict |
|---|---|---|---|
| **A. Cut the light before the GPU** (chosen) | Nothing in the fragment shaders. The ARM already sends lit RGB (`fs_colour`) or `k` (`fs_tex_lit`). With `vs` 0 or 1 the ARM computes the light, so the stage 2 code is the whole V3D path. With `vs` 2 the QPU vertex shader computes it: add the same cut to `vs_lit`, `vs_lit8`, `vs_lit_tex`, `vs_lit_tex2`. | per vertex: `fmul`, `ftoi`, `itof`, `fmul`, `fadd`, `fmin`: about 6-8 QPU instructions on 210-215 (3%). Per pixel: 0. | **Cheapest. It works for flat and smooth.** |
| B. Ramp texture in the fragment shader | One more TMU fetch per pixel, a new unlit-colour varying for `fs_colour`. | per pixel: 1 fetch and 4 instructions on a fill-bound GPU. | Rejected. The Pi is fill-bound ([BENCH3D](../BENCH3D.md): `quad_*`). |
| C. Arithmetic cut in the fragment shader | `ftoi`/`itof` on the interpolated `k` in each `fs_tex_lit*` (+5 instructions on 22) and a new shader for colour faces. | per pixel +20% ALU on `fs_tex_lit`. | Only if flat cel looks wrong in smooth draws (stage 3b), after a Pi measurement. |

Way A details:

- `tools/qpuasm.py` gets a `CEL` variant of each of the four vertex programs (the same text with the cut block inserted after the `dd` clamp, one more uniform pair `N, floor` or `1/(N-1)`). The host emulator tests (`self_test`, `check_same_writes`) run both variants.
- Code buffer: `vs_lit8` becomes about 444 words of 512. If a variant does not fit, the runtime keeps the old rule for that program: **a cel draw with `vs` 2 is lit on the ARM** (the same fallback as a model lit by the sun with `vs` 1). The numbers then show the price and the cart prints it.
- `gpu3d.c` picks the `_cel` program when the draw has flag 256 and `cel3d` bands > 0. The probe at boot (`lit_ok`) must also test the cel variant, and refuse it if it does not match the ARM result; then the ARM lights it.
- Impostors (`gpu3d` fifth value): a far hero drawn as one quad takes one band from the average light. Same rule on every device.

### 4.4 Outline (stage 4): the choice

| Way | Pi cost | Verdict |
|---|---|---|
| **Inverted hull** (chosen): after the mesh, draw it again with the winding reversed, each vertex pushed along its normal by `outline_width`, flat emissive `outline_rgb`, no texture, no light | The vertex work of the hero twice and the bin work twice. Only back faces are filled, so about half of the pixels. Estimate +40-60% of a hero draw. **Stage 1 measures it with the baked hull.** | Works on ARM, V3D and Mali with no new shader. |
| Edge detect on depth/normal | A pass over every pixel (230 400 at 640x360). The V3D keeps depth in its tile buffer: the ARM cannot read it. On the ARM, about 15 instructions a pixel is 3.5 M instructions a frame. | Rejected. |

- Normal for the push: the smooth vertex normal `vn` (already built for smooth draws). For a flat draw: the area-weighted average of the face normals, built once per mesh and cached with it.
- Gate before stage 4: if the stage 1 hull costs more than +60% of the hero draw at LOD 0 and 1 on the Pi, the outline is limited by distance (LOD 0 and 1 only) and by the impostor rule (no outline on an impostor).
- On the V3D: the hull goes through the same vertex-shader program with one more uniform (`grow`), so it does not need a new program. If it does not fit, the ARM transforms the hull (one more row in the bench).

### 4.5 Mali and RGB30

- Today the RGB30 runs the ARM raster. Stages 2 and 4 run there with no extra work. The `.b16` cart gets the options that exist on that kernel (no `vs`, no queue, no AA rows).
- When the Mali backend draws triangles it takes the same corner data as the V3D backend (a lit colour or a grey `k` per corner). Way A needs **no Mali shader**: the cut is in the ARM lighting stage.
- If Mali ever gets a vertex-shader lighting stage, the rule is: cut the light in that shader as in 4.3, and until it does, a cel draw is lit on the ARM. The result must match the ARM picture (same bands). A test compares them (section 7).
- The 720x720 mode pays the fill on the ARM ([draw.md](../api/draw.md)): it is not a cel cost. Document, do not fix.
- Nothing is verified on the RGB30 hardware.

## 5. Plan: one PR per stage

Each PR runs the checks of the closing list in `CLAUDE.md`: `python scripts/docs_audit.py score`, `python scripts/consistency.py check`, and (stages 2, 3) `consistency.py gate`.

### Stage 1: the cart, existing options only (done; state in [herocart](../progress/herocart.md))

Built as described below, with these differences: one hero (`charkit` rango, 984 triangles, flat colours), so the Texture row switches the plaza; rows 17 (alpha) is greyed too; the test is `tests/herocart/check.py` (`make test-herocart`) and `-k herocart` in QEMU; the console `options` line is not read by Lua.

Scope:

- `carts/herocart/` as in 2.1, `.bm` edition only, rows 1-11 and 13-18 live, row 19 with the baked hull, rows 12 and 20 greyed.
- Hero + 4 LODs + 4 hulls in `assets/`, made by `build/mkheroes.py` (bm-forge `charkit` + `bmdecimate`).
- Sweep, panel, 1 s line, `doc_write` + `report`.
- `Makefile` target `build/carts/herocart.bm` (not in `GAMES`), `docs/progress/herocart.md` (new, linked from `graphics.md`).
- No C, no kernel change.

Tests (no hardware):

- `tests/herocart/check.py` (as the stages3d check): build the cart, run it in the PC runtime (`bmhost`) with a scripted input; check the log line format, `tri` rises with `n`, `tri` falls from LOD 0 to 3, the sweep prints 10 table lines.
- Counts: `mkheroes.py --check` prints the real triangles of the 8 models; the test fails if a level is off by more than 15%.
- QEMU smoke: `python tests/qemu_test.py -k herocart` (ARM path, no GPU): starts, draws 120 frames, no Lua error.
- ARM instruction cost per level: `tests/bm/herocost.py` on the cart's models (LOD 0-3, hull). This gives the hull ratio before the Pi.

Pi numbers to collect (copy `HEROCART.TXT` from `/docs`; one run per row, 640x360):

| Run | Settings | Keep |
|---|---|---|
| 1 | ARM, n sweep, LOD auto | load at 60/30 fps |
| 2 | GPU, GPU+VS (vs 2), GPU+VS+Q (queue 1, 2) at n 20 | ms, gpu, wait |
| 3 | n 20, LOD 0, 1, 2, 3 | ms, gpu, tri, vtx |
| 4 | n 20, one option at a time (rows 4-18) from the base | delta ms against base |
| 5 | n 20, resolution 320x180 ... 960x540 | ms, gpu |
| 6 | n 20, LOD 0 and 1, outline on / off | the hull cost (gate of stage 4) |

### Stage 2: cel on the ARM raster (done; state in [herocart](../progress/herocart.md))

Built as described below, with these differences: bm3D 7.4. The table is built by `r3d_cel`; `cel3d` sets all four values at each call; flag 512 and the outline arguments are kept and draw nothing (stage 4). A cel draw on a backend skips `backend->mesh` and goes triangle by triangle, as with `vs` 1. `dither3d(false)` replaces the dither by a constant half step: it saves no fill time. `herocost.py` was not run; the lighting cost is not measured.

- `r3d.c`: the band table, hooks in `light_fast`, `light_obj`, the whole-model light; flag 256 in `r3d_draw_flags`; `dither3d`.
- `runtime.c`: `cel3d`, `dither3d`, flag 256 passed through `draw3d`. A cel draw with `vs` 2 is lit on the ARM (4.3).
- Docs: `light3d.md`, `API.md`, `specs.toml`, `DRIVERS.md`.
- Cart: rows 12 and 20 live (also in the sweep).
- Tests:
  - Host unit test (a new `tests/bm/test_cel.c`, built like `test_impostor.c`): a lit sphere on a `g16` buffer; with `bands = N` the number of distinct grey levels on the sphere is at most `N + ambient steps`; with `bands = 0` the picture checksum equals the one of before (old carts unchanged).
  - Table test: `dd` just below and above each band edge gives the right band; `floor_` 0 and 1.
  - `herocost.py`: ARM instructions of one hero with and without cel; the difference must be under 2%.
  - QEMU: `-k herocart` with cel rows on.
- Pi numbers: runs 3 and 4 again with `Cel` on and `Dither` off; expected delta near 0 for cel. `Dither` off: ARM fill saving, size not known.

### Stage 3: cel on the V3D (done, bm3D 7.5; state in [herocart](../progress/herocart.md))

Built as way A, with these differences: the `_cel` programs are four copies of the lit programs with the cut block after the `max(d, 0)` line (`cel_variant` in `tools/qpuasm.py`), four more uniforms (N, N - 1 as an integer, the scale, the floor); the code block grows from 40 to 48 KB; the probe is `probe_lit(…, 1)` (status `cel light yes/no`); the cut is not rounded to 1/255 as the ARM table is. `r3d_backend_t.cel` and `r3d_env_t.cel` carry the request. No `heroes_cel` bench test in this change (the 3D Bench is in another branch). The host test is the scenes `cel *` of `tests/gpu/test_gpu3d.c` (ARM against the V3D emulator).

- `tools/qpuasm.py`: `CEL` variants of the four vertex programs, regenerate `src/gpu/shaders.h`; `gpu3d.c`: choose the variant, probe it, fall back to the ARM light.
- A bench test `heroes_cel` in `b3d.c` (heroes with cel off/on, same profiles) so the 3D Bench tracks it; `BENCH3D.md` line.
- Tests:
  - QPU emulator (`src/gpu/qpu.c`): run each `_cel` program on 1000 fixed normals; compare with the ARM table (skip values within 1e-4 of a band edge); the old programs still match their old output.
  - `make test-b3d`: the new test runs on all profiles; GPU and ARM pictures of a cel hero differ by at most the Gouraud rounding already tolerated.
  - `python tools/qpuasm.py` self-test (the generator checks the programs).
  - QEMU: `-k herocart` (no GPU in QEMU: only the fallback is exercised).
- Pi numbers: run 2 with `Cel` on / off at `vs` 2; ms and `gpu` delta (expected within 3%); the `heroes_cel` row of the bench; probe result in `gpu` status. Stage 3b (way C) only if flat cel looks wrong in smooth draws.

### Stage 4: outline (done, bm3D 7.5)

Built as the inverted hull in `r3d.c` for every backend: `hull_get` makes a second mesh (faces turned, vertices pushed along the position-welded normal, levels of detail kept), caches it in `r3d_mesh_t.hull`, and `r3d_draw_flags` draws it unlit in `cel_rgb` after the mesh. It goes through the ordinary mesh call, so the V3D takes it with its unlit vertex program; no new shader and no `grow` uniform. Mali takes it as any unlit mesh (not touched, not tested). The distance rule of the gate was not applied (the stage 1 hull cost was not measured on the Pi): only the impostor rule, the shadow, the first-person model and the camera inside the mesh's sphere have no outline. Cart row 19 is `Outline`: off / hull (baked) / flag 512.

- Flag 512 in `r3d.c` and `gpu3d.c`; `cel3d` outline arguments; normal cache; the distance/impostor rule from the stage 1 gate.
- Cart row 19 changes from the baked hull to the flag; the baked hull stays as row 19b "hull (baked)" to compare.
- Tests: host unit test (the outline pixels are the outline colour, only on the silhouette side; a mesh seen from inside draws none); `herocost.py` ratio against the stage 1 hull; `test-b3d` `heroes_cel` with outline.
- Pi numbers: runs 3 and 6 with the real flag against the baked hull.

### Stage 5: RGB30 edition (after the Mali alignment)

- `carts/herocart/b16/` front, 360x360 and 720x720 panels, rows the kernel offers. `.b16` is built from its own sources; the `.bm` sources do not change.
- Not verified on hardware until the owner runs it.

## 6. Risks

| Risk | Answer |
|---|---|
| `charkit` hero has fewer than 1500 triangles | The level table scales from the real count; the names stay `hero0-3`. |
| `libbmdecimate.so` not built | Build with `make` in WSL (another runner owns the WSL build; the PR does not run it), or use the clustering fallback. |
| `animate()` for 64 heroes dominates | Printed apart as `anim`; the hero cap is 64; the logic loop is fixed. |
| A bench-style number from the cart differs from the 3D Bench | The cart uses `stat()`, the bench its own timer. Both stay. The cart is for comparing options, not to replace the bench. |
| Per-vertex cel looks soft on high-poly smooth heroes | Flat draw gives hard bands; stage 3b (way C) is the answer if the owner wants hard bands in smooth draws. |
| `BM_SPEC_REV` change | Owner decision in stage 2. |

## 7. Decisions for the owner

1. Is `bench_save()` into `bm/bench/` worth a C change (stage 1b), or is `doc_write` enough? Proposal: enough.
2. Does the cart use the bit-based LOD as a second mode? Proposal: not now.
3. Does adding `cel3d`, flags 256/512 and `dither3d` bump `BM_SPEC_REV` to 3? Proposal: yes, in stage 2.
4. Is a hard band in smooth draws required? Proposal: no. Look at the Pi first.
