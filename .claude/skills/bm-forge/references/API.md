# bm-forge reference: the game author's API and pipeline

What a generator of bm games (`.bm` for the Pi, `.b16` for the RGB30) and of their assets
has to know. Every claim cites `file:line`; anything inferred and not seen in code or docs
is marked **NON VERIFICATO**. Source of truth: `docs/API.md` (index) and `docs/api/<area>.md`.

## 1. What a cartridge is

- A `.bm` is a game in **Lua 5.4**; the kernel draws in C, 640×360, RGB565, 60 fps
  (`docs/API.md:9-10`).
- One file holding: Lua code (required), sprite sheet PNG, tile map CSV (one per layer),
  tile flags, named sheet zones, cover PNG (`docs/GAME-GUIDE.md:56-68`); plus sound bank
  `--audio`, 3D models `--models` (`scripts/mkbm.py:437-439`).
- Callbacks: `_init()` once, `_update()` 60 Hz before `_draw()`, `_draw()`, optional
  `_exit()` (return `true` closes now) and `_leave()` for network matches (`docs/API.md:80-86`).
- Globals `SCREEN_W`, `SCREEN_H`; the screen is not cleared by itself, `_draw` usually
  starts with `cls()` (`docs/API.md:88-92`).
- Limits: over **20 million Lua instructions** in one frame stops the cart; sandbox has no
  `io`, `os`, `load`, `dofile`; `require` only loads built-ins `"bmlib"`, `"assist"`,
  `"bm3d"`, `"predict"`, `"words"` (`docs/API.md:94-99`).
- Colours are `0xRRGGBB` (`docs/GAME-GUIDE.md:74`).
- Lua gotcha (project rule): `cond and nil or x` always gives `x` (`CLAUDE.md`, *Invarianti*).

## 2. Folder structure of a game in the repo

Single-file game (generic Makefile rule, `Makefile:213-236`):

```
carts/<game>/main.lua        required
carts/<game>/cover.png       optional, menu card (88x88 square, docs/API.md:47-52)
carts/<game>/sheet.png       optional sprite sheet  (sheet8_<game> := 1 -> --sheet8)
carts/<game>/map.csv         optional, layer 1; map_<name>.csv for layers_<game> := ...
carts/<game>/flags.csv       optional tile flags
carts/<game>/sprites.txt     optional named zones
carts/<game>/models.bm       optional 3D models + skeletons + animations (or models.glb)
```

- Registration: add the name to `GAMES` (`Makefile:191`), a `title_<game> := Title`
  line (`Makefile:193-206`), optional `res_<game> := 320x180` etc. (`Makefile:198,203,207`).
  Output: `build/carts/<game>.bm` (`Makefile:223`; `docs/GAME-GUIDE.md:181-186`).
- `models.bm` wins over `models.glb` if both exist (`Makefile:236`).
- Multi-file games: `carts/<game>/src/*.lua` joined in name order by `carts/<game>/build.py`
  into `build/<game>/main.lua`; the first file (`00_*.lua`) stays top level, every other is
  wrapped in `do ... end` to dodge Lua's 200-locals limit; a `.map` file maps output lines
  back to sources (`carts/titan_ex/build/build.py:3-12,22-41`). Such games get an explicit rule
  (`Makefile:248-257` titan, `Makefile:238-246` kitchen).
- **`.bm` vs `.b16` are separate editions with separate sources** (`CLAUDE.md`, *Non fare*):
  `carts/titan/src/*.lua` → `titan.bm` (`Makefile:250-257`); `carts/titan_ex/src/*.lua` →
  `titanex.bm` at `--res 360x360`, sharing `carts/titan/assets/sheet.png` and cover
  (`Makefile:259-268`), then copied byte for byte to `titanex.b16` (`Makefile:369-372`).
  `carts/titan_ex/` also holds a `main.lua` and `titan_ex.b16` in the tree (Glob); whether
  they are stale build outputs is **NON VERIFICATO**.
- Generated-asset games: Overbit builds `build/overbit/models.bm` from Python
  (`carts/overbit/art/models.py`, `Makefile:311-314`) and its `.b16` from a minified script
  (`Makefile:355-364`).
- bm-forge writes this layout (`main.lua`, `models.bm`, optional `cover.png`, `make.txt` as a
  note only) with `bm_core/export.py` `export_folder` (`export.py:232-308`).

## 3. Packing: scripts/mkbm.py

```sh
python3 scripts/mkbm.py -o hello.bm --lua carts/hello/main.lua --title "Hello" --author "me"
```
(`docs/API.md:41`). Options (`scripts/mkbm.py:428-443`):

| Option | Meaning |
|---|---|
| `-o`, `--lua` | output, code (both required) |
| `--sheet PNG`, `--sheet8` | sprite sheet; `--sheet8` = palette + RLE (≤256 colours) |
| `--map [name=]CSV` | a map layer (repeatable) |
| `--flags`, `--sprites` | tile flags; named zones `name x y w h [frames [fps]]` |
| `--audio` | `.json` (`scripts/bmaudio.py`) or `.bmau` bank |
| `--cover PNG` | menu picture |
| `--models` | `.glb` or a `.bm` with models |
| `--uv-inset` | texture margin in sheet px (default 0.25) |
| `--title`, `--author` | header strings |
| `--res` | `640x360` `480x270` `320x180` `256x256` `360x360` |

- `--models X.bm`: copies its MESH section (8), its ANIM section (9) if present, and its
  sheet (SHEET8 or SHEET) when no `--sheet` is given (`scripts/mkbm.py:453-467`).
- `--models X.glb`: the glb's image becomes the sheet if none is given, and SHEET8 if ≤256
  colours (`scripts/mkbm.py:469-475`); models via `bmmesh.models_from_file`
  (`scripts/mkbm.py:476-478`). No ANIM is added on the glb path in these lines: whether a
  glb skeleton/animation is imported is **NON VERIFICATO** (looks like no).
- Python API: `mkbm.pack(lua, sheet=None, map_=None, title, author, res=(640,360), cover,
  sheet_packed, audio, mesh, extra=(), layers, flags, sprites, boxes)`
  (`scripts/mkbm.py:364-368`); `extra` takes `(section_id, bytes)` pairs, used for ANIM by
  Overbit (`carts/overbit/art/models.py:83-84`).
- Section ids: LUA 1, SHEET 2, MAP 3, COVER 4, SHEET8 5, AUDIO 6, MESH 8, ANIM 9, SPRITES 11,
  LAYERS 12, FLAGS 13, BOXES 14 (`scripts/mkbm.py:50-52`).
- Contradiction: `docs/RISORSE.md:351` says `mkbm.py --res FILE` integrates resources, but
  `--res` is the resolution with fixed choices (`scripts/mkbm.py:443`). Resource integration
  at build time is **NON VERIFICATO**; use `scripts/bmres.py add` (`docs/RISORSE.md:343-349`).

## 4. Screen and shapes (docs/api/draw.md)

`cls([c])`, `screen(w,h)` (16:9 list; on the RGB30 only 360×360 and 720×720),
`pset/pget`, `line`, `rect/rectfill(x,y,w,h,c)`, `circ/circfill(x,y,r,c)`,
`tri(x0,y0,x1,y1,x2,y2,c,[c1,c2])`, `print(text,x,y,[c,scale])` (8×16 font, returns x),
`font([name])`, `prompt(name,x,y,[small,scale,player,menu])` (button chip), `camera([x,y])`,
`clip([x,y,w,h])` (`docs/api/draw.md:9-20`).

## 5. Sprites and map (docs/api/draw.md)

- Sheet = 8×8 cells, cell `n` at column `n % (w/8)`; up to 4096×4096; alpha < 128 is
  transparent; without a PNG a transparent 256×256 sheet to draw with `sset`
  (`docs/api/draw.md:24-30`).
- `spr(n,x,y,[w,h,flip_x,flip_y])`, `sspr(sx,sy,sw,sh,dx,dy,[flip_x,flip_y,zoom])`,
  `sget/sset`, `zspr(name,x,y,[frame,…])`, `zone(name)`, `zones()`,
  `zboxes(name,[frame,kind])` (hit/hurt/body boxes), `map(mx,my,[x,y,mw,mh,layer,mask])`,
  `mget/mset`, `fget/fset`, `mflags(x,y,[w,h,layer])`, `msize`, `mlayers`
  (`docs/api/draw.md:34-47`).
- Tile flags (bmlib convention): 0 solid, 1 platform, 2 ladder, 3 water, 4 hurts
  (`CLAUDE.md`, *Invarianti*).

## 6. Input (docs/api/input.md)

- `btn(i,[p])`, `btnp(i,[p])`; `i` a number (0–3 dirs, 4 A, 5 B, 6 X, 7 Y:
  `docs/GAME-GUIDE.md:106-112`) or a name (`"a"`…`"r3"`, `"ok"`, `"back"`, a `keymap`
  action) (`docs/api/input.md:9-10`).
- `keymap(t)`, `controls([scheme])` (the player's remap, `docs/api/input.md`), `controller([p])`, `online([on,note])`, `players()`, `stick([p,n])`
  (`docs/api/input.md:11-15`); `mouse(on,[arrow])`, `mouse()`, `mousep([i])`
  (`docs/api/input.md:24-26`).
- RGB30 rule: confirm/back via `"ok"`/`"back"`, never raw A/B (`CLAUDE.md`, *Invarianti*;
  `docs/GAME-GUIDE.md:119-124`).

## 7. System, saves, network (docs/api/system.md)

- `time()`, `stat(n)` (0 Lua KB … 4 triangles … 9 GPU on … 16 GPU ms; measurements only,
  never game logic), `frameskip([n])`, `devkit`, `profile`, `devinfo`, `battery`,
  `battery_low`, `code_tokens`, `log`, `report`, `quit`, `timeslice(co,[k])`
  (`docs/api/system.md:9-21`).
- Random: `math.random`, seed with `math.randomseed(stat(3))` on a key press
  (`docs/api/system.md:23-24`); bmlib has `lib.rnd` (`docs/api/bmlib.md:50`).
- UDP: `udp_open/udp_send/udp_recv/udp_close/net_ip/net_resolve`, 1024 B packets, asks
  permission (`docs/api/system.md:28-43`); prefer `bmnet` (`docs/api/system.md:28-29`).
- Saves: `save(t,[slot])`, `saved([slot])`, `saves()`, `delsave([slot])`; 8 slots × 32 KB
  (`docs/api/system.md:49-54`).

## 8. bmlib (require "bmlib")

- `local lib = require "bmlib"`, call `lib.update()` once per `_update`; times in seconds,
  speeds in px/frame (`docs/api/bmlib.md:7-25`). Groups: numbers (`lib.clamp`, `lib.lerp`…),
  random (`lib.rnd`, `lib.chance`, `lib.choose`, `lib.rng`), overlap/hit, tiles
  (`lib.move`, `lib.step`, `lib.ray`), hits (`lib.hits`, `lib.box`), tweens/timers
  (`lib.tween`, `lib.after`, `lib.every`, `lib.script`), particles, camera, states, text/UI
  (`lib.printc`, `lib.menu`, `lib.pause`), players (`lib.pads`, `lib.party`, `lib.split`),
  `lib.jingle`, `lib.store`, `lib.best`, `lib.anim` (headings `docs/api/bmlib.md:27-281`).
- States: `lib.states(defs, first)` with `S:go`, `S:push/pop`, `S:update/draw`
  (`docs/api/bmlib.md:180-200`).
- **3D builder**: `lib.builder()` then `:piece(points,tris,[c])`,
  `:box(x0,y0,z0,x1,y1,z1,c,[top])`, `:tetra`, `:quad`, `:build()` → a `mesh()`; faces are
  turned outward by themselves (`docs/api/bmlib.md:285-300`).

## 9. 3D: meshes in code

- `mesh(v, f, [uv])`: `v = {x,y,z,…}`, `f = {a,b,c,colour,…}` (1-based, **clockwise** from
  the visible side), up to 65535 verts/faces; colour `-1` + `uv` (6 numbers/face, sheet
  pixels) = textured (`docs/api/light3d.md:69`).
- `mesh_sphere([r,segments,c1,c2])`, `mesh_cube([c])` (`docs/api/light3d.md:70`).
- Material bits in the colour: `0x40000000` emissive, `0x20000000` glossy, `0x10000000`
  screen door, `0x08000000` flat, bits 24–26 LOD (`docs/api/light3d.md:98-111`).
- Build meshes in `_init` (`docs/GAME-GUIDE.md:326`); budget ~1200 triangles at 60 fps
  (`docs/GAME-GUIDE.md:321-322`; `sdk/README.md:60`).

## 10. 3D: loading models from the cartridge

- `model(name)` / `model(n)` (from 1): a model of the MESH section as a mesh, textured from
  the sheet; `nil` if missing; **each call builds a new mesh: do it in `_init`**
  (`docs/api/light3d.md:71`). Baked-light models (MESH "lit" bit) draw smooth with that
  light plus lamps and fog (`docs/api/light3d.md:71`).
- `models()` names in order; `bounds3d(m)` → `x0,y0,z0,x1,y1,z1` (`docs/api/light3d.md:72-73`).
- Typical loop: `for _, name in ipairs(models()) do m[name] = model(name) end`
  (`carts/overbit/src/40_actor.lua:38` loads a model the same way).
- Textured faces read the cart's sheet: `sset` changes the models too (`sdk/README.md:44-45`).
  If the game has no `sheet.png`, the sheet comes from `models.bm`/`.glb`
  (`sdk/README.md:49-52`).
- Limits: 65535 corners/triangles per model; one bone per corner (no weights); 64 bones,
  255 animations, 1024 keyframes per animation, each key a whole pose; no repeating uv
  (`sdk/README.md:58-64`); up to 256 models (`scripts/bmmesh.py:27`), names ≤15 bytes,
  unique (`scripts/bmmesh.py:26,37-39`).
- `cart_data(8|9)` gives the running cart's MESH / ANIM bytes, or `nil`; `cart_data(8|9, bytes)`
  replaces them in memory after the kernel checks them (`bm_mesh_check` / `bm_anim_check`,
  more than 0x1000000 bytes refused), returning `true`, or `false` and a message; `model()` and
  `animate()` see the new ones at once (`docs/api/tools.md:46`; `src/bm/runtime.c:6793-6797,
  6872-6927`). Kinds 11 and 14 are SPRITES and BOXES (`src/bm/runtime.c:6801-6803`). It is
  registered in the one function table for every cart, and `l_cart_data` has no tool gate
  (`src/bm/runtime.c:4670,6798-6928`). Not needed for packed assets; bm-forge export never uses
  it. Whether Market sandbox rules restrict it is **NON VERIFICATO** (open question below).

## 11. 3D: skeleton and animation playback

| Function | Use (`docs/api/light3d.md`) |
|---|---|
| `animate(m, [anim, t, anim2, t2, k, bone])` | pose of `anim` (name or number) at `t` seconds, looping if the clip loops; blend with `anim2,t2` by `k` 0..1; `bone` limits the 2nd clip to that subtree; no anim = rest pose; returns length; error without skeleton/clip (`:74`) |
| `bone_turn(m, bone, [rx,ry,rz])` | extra turn applied by every later `animate`; no angles removes it (`:75`) |
| `bones3d(m)` | bone names (`:76`) |
| `clips(m)` | `{ {name=, length=, loop=}, … }` (`:78`) |
| `bone3d(m, bone)` | head `x,y,z` and tail `cx,cy,cz` in the last pose, model coords (`:79`) |
| `hit3d(m, x,y,z, ry, scale, ox,oy,oz, dx,dy,dz, [maxd])` | ray vs bone capsules of the last pose → `t, bone` (`:77`) |

- Call `animate` before `draw3d` each frame (`docs/GAME-GUIDE.md:401-415`). Rigid skinning:
  `animate` costs only the bones (`docs/api/light3d.md:74`).
- Blend example `animate(v, "walk", t, "wave", t, k)`; attaching a light to `bone3d(v,"arm.L")`
  with the model's yaw applied by hand (`docs/GAME-GUIDE.md`, the animated model section).
- Invariant: `animate()` in `runtime.c` and the pose in `carts/animator/main.lua` do the same
  maths (`docs/progress/graphics.md:88-89`).

## 12. 3D: drawing, camera, light, GPU options

- `draw3d(m, x,y,z, [rx,ry,rz, scale, flags])`; flags add up: 1 no z-buffer, 2 no light,
  4 smooth, 8 shadow on plane y, 16/32/48 LOD 2/1/0, 64 in front (FP weapons), 128 flat
  textures (`docs/api/light3d.md:80`).
- `camera3d(x,y,z,[yaw,pitch,fov,roll])` (default z=−5, fov 60), `light3d(x,y,z,[ambient])`,
  `zclear()` every frame before `draw3d` (`docs/api/light3d.md:88-90`).
- `sky3d(sun,sky,ground)`, `shine3d(spec,exp,rim)`, `textures3d([on])`, `shadow3d(style)`,
  `fog3d(c,near,far)`, `lamp3d(i 1–4, x,y,z, radius, [k, colour])`
  (`docs/api/light3d.md:81-84,92-93`).
- Effects: `point3d`, `line3d`, `sprite3d`; helpers `project3d(x,y,z)` → `sx,sy,depth`,
  `visible3d(x,y,z,r)`, `pvs3d(t)` (`docs/api/light3d.md:85-87,94-96`).
- Collision: `world3d()`, `world_box`, `world_ray`, `world_move`, `world_floor`
  (`docs/api/light3d.md:121-125`).
- `gpu3d([on, aa, vs, queue, impostor, later])` → `on, aa, vs, version, queue, impostor,
  later`: GPU on/off, 4× MSAA, vertex shader (`false`/1/2), frame queue (`true` = console's
  level, 2 = two jobs), impostor size 2–64 px of a 1080-line screen, `later` = show a frame
  later (only with queue 2, never online) (`docs/api/light3d.md:91`). Keep the console's
  queue with `gpu3d(true, false, 2, true)` (`docs/GAME-GUIDE.md:380-383`). Impostors alone:
  `gpu3d(nil, nil, nil, nil, 16)` (`docs/GAME-GUIDE.md:364-372`).
- Same functions on GPU and ARM; draw all 3D first, then the HUD (`docs/api/light3d.md:130-139`).
  Textured + screen-door faces together force the ARM for the rest of the run
  (`docs/api/light3d.md:145-147`). Sheet sides multiple of 32 go to the GPU tiled, faster
  (`docs/api/light3d.md:148-150`). A frame starting with `cls()` is cheaper
  (`docs/api/light3d.md:147-148`). State changes in `_update`, HUD after 3D, for *a frame
  later* (`docs/api/light3d.md:168-170`).
- Driver version: `BM3D_VERSION "7.3"` (`src/gpu/version3d.h:26`);
  `docs/progress/graphics.md:42` still says "7.2" (stale). Console-only options (config
  keys, no Lua argument): `gpu3d_wc`, `gpu3d_2d`, `gpu3d_filter`, `gpu3d_tex16`, `gpu3d_fs2`,
  `gpu3d_sort`, `gpu3d_attr`, `gpu3d_etc1` (`docs/progress/graphics.md:53-56`).
- No GPU in QEMU: 3D is software there (`docs/progress/platforms.md:64`). On the RGB30 the
  Mali path is still bring-up (`docs/progress/graphics.md:80`); that RGB30 games draw 3D with
  the ARM rasterizer is **NON VERIFICATO**.

## 13. 3D assets: making models.bm off-console

- On the console: bm Studio (tile models), bm Animator (skeletons, clips, pre-rendered
  sprites), bm Mesh, bm Pixel, working on a `.bme` project (`sdk/README.md:5-13`).
- PC tools: `scripts/mkbm.py --models`, `scripts/bmmesh.py`, `scripts/bmres.py`,
  `tools/bmreduce.py`, `tools/cutout2mesh.py`, `tools/meshy2mesh.py`, `tools/local2mesh.py`,
  `tools/img2mesh.py` (`sdk/README.md:15-18`). Preview PNG: `tools/bmrender.py`
  (`docs/RISORSE.md:350`).
- `scripts/bmmesh.py` model dict: `{"name", "verts": [(x,y,z)], "faces": [(a,b,c,colour,
  (u0,v0,u1,v1,u2,v2))]}`, **0-based**, clockwise, y up; colour `0xRRGGBB` or `TEXTURED`
  (`0x80000000`) with uv in sheet pixels (`scripts/bmmesh.py:7-13,25`).
  `encode(models, inset=0.25)` → MESH body (`scripts/bmmesh.py:30`); optional `"lit"` with
  baked corner light (`scripts/bmmesh.py:46-50`).
- `encode_faces(name, faces, bones)` turns Studio-style faces `{p, c, b}` (tris or quads)
  into the dict plus per-vertex bone list (`scripts/bmmesh.py:75-97`).
- `encode_anim([(name, bones, vb, clips)])` → ANIM body: bones `{name, parent (0-based, -1
  root), head, tail}`, `vb` bone per vertex, clips `{name, loop, length, keys: [{t, pose:
  [{q=(x,y,z,w), t=(x,y,z)}]}]}`; a missing bone pose is identity
  (`scripts/bmmesh.py:100-121`). Rig name binds to the model by name (`docs/RISORSE.md:81`).
- Write a models-only cart: `mkbm.pack(lua, sheet, mesh=..., extra=[(9, anim)])`, as Overbit
  does (`carts/overbit/art/models.py:77-87`; it uses its own `rig.encode`).
- Resource files `.bmm` (MESH + ANIM + used sheet cells), `.bmi`, `.bms`, `.bmt`, `.bmc`,
  `.bmk` (`docs/RISORSE.md:68-110`); `bmres.py list|extract|add|convert|info`
  (`docs/RISORSE.md:343-349`), e.g. `extract GAME.bm --model villager -o VILLAGER.bmm`.
- Blender: a `.glb` in `carts/<game>/models.glb` is enough for mesh+texture
  (`docs/GAME-GUIDE.md:422-424`); glTF z flips sign and winding reverses
  (`docs/progress/graphics.md:86-87`).

## 14. Projects (.bme) and cart_build (console tools)

- A `.bm`/`.b16` is a game; tools edit a `.bme` project (same container, bit 0 of the u16
  at header offset 18); saving a game asks and writes `NAME.BME` (`docs/api/tools.md:7-18`).
- `cart_build(project)`: builds the project's game as `.BM` next to it, keeping the project
  name at offset 104; returns `true, path` or `false, msg`; SDK Ctrl+B
  (`docs/api/tools.md:30`).
- `cart_save`, `cart_write(path, {lua, title, author, res, from, sections, sheet, palette})`
  (`sections = {[8]=MESH, [9]=ANIM}`), `cart_load`, `cart_read`, `cart_meshes(path)` (meshes
  the code builds), `mesh_reduce` (`docs/api/tools.md:26-34`). These are for bm's tools;
  other carts write only `.bm`/`.bme` they made in the same run (`docs/api/tools.md:17-18`).
- `cart_data(kind, [bytes])`: MESH 8, ANIM 9, SPRITES 11, BOXES 14, read or replace in memory
  (`docs/api/tools.md:46`; `src/bm/runtime.c:6798-6803`); see section 10.

## 15. .bm vs .b16 profile

- Same container as `.bm` (`BMCART`), `.b16` extension (`docs/B16.md:20-23`). Today the
  `.b16` is the `.bm` bytes copied (`Makefile:366-372`); the profile field and
  `mkbm.py --b16` do **not** exist yet (`docs/progress/lua.md:63`; no b16 option in
  `scripts/mkbm.py:428-443`).
- Screen 360×360 or 720×720, fixed, no `screen()` (`docs/B16.md:24-27`); build with
  `--res 360x360` (`Makefile:268`).
- Decided profile (`docs/B16.md:28-49`): 256-colour palette (SHEET8, use `--sheet8`); Lua
  4 MB; one 1024×1024 graphics bank resident; map ≤8 levels, world ≤8×8 screens; 8 voices;
  save 64 KB; per-frame CPU budget with drop to 30 fps; 3D allowed with a triangle cap;
  SNES pad + stick; no files (`cart_*`, `ls`), seeded `rnd()`; **8 MB** file cap; no token
  limit.
- Enforced today: only the 8 MB size check in the Makefile (`Makefile:364,372`). The SDK's
  `.b16` target only lists reminders (math.random, time(), saves, ls/cart_*, udp_*,
  require, load, screen(), 3D) (`docs/B16.md:609-618`). Memory, CPU and sandbox limits are
  **NON VERIFICATO** as enforced (no b16 check found in `src/bm/runtime.c`, Grep).
- Where they show: Pi shows `.bm` and `.b16`; RGB30 only `.b16` (`docs/B16.md:50-51`).
  `market/README.md:23-24` says the Pi lists `.bm` (inconsistent with B16.md; Overbit's `.b16`
  is "for the Pi and the RGB30", `Makefile:348`).

## 16. Market and manifests

- `market/about.txt`: one line per game `id: license | about` (≤120 chars), license
  required (`market/about.txt:1-3`; `CLAUDE.md`, *Build*).
- `make market-seed MARKET=<clone>` adds every `GAMES` `.bm` and the `.b16` of
  `MARKET_B16 := overbit yharnam titanex`; `MARKET_NO_BM` skips the `.bm`; `MARKET_ONLY`
  adds games not in `GAMES` (`Makefile:968-982`).
- Market repo layout: `games/<id>/<id>.bm`, optional `<id>.b16`, `info.txt` (`version:`,
  `license:`, `about:`); id `a-z0-9-`, ≤23 chars, never changes (`market/README.md:15-36`).
- `manifest-rgb30` is the **kernel release manifest** (kernel8.img, `bm/ca.pem`), not a
  game manifest (`Makefile:760-769`; `src/kernel/update.c:32`; `scripts/mkrelease.py:15`).
  A game needs no manifest entry (**NON VERIFICATO** beyond these reads).
- bm-forge writes the Market folder (`<id>.bm` / `<id>.b16`, `info.txt`) with
  `bm_core/export.py` `export_market` (`export.py:311-402`) and the `about.txt` line with
  `about_line` (`export.py:405-416`).

## 17. Minimal examples (path lists)

- Smallest 2D game: `carts/pong/main.lua` (+ `carts/pong/cover.png`)
  (`docs/GAME-GUIDE.md:886`); inline example `docs/GAME-GUIDE.md:78-102`.
- 3D in code only: `carts/astrowing/main.lua` (`docs/GAME-GUIDE.md:889`).
- **Mesh load + animation**: `carts/overbit/src/40_actor.lua` (`model()` at `:38`, `animate()` at
  `:340-341`). The test cart `tests/fixtures/mktestkit.py` makes (the Makefile builds it as
  `build/carts/testkit.bm`) is a small cart with 8 models and one skeleton (`walker`).
- Inline snippets: `sdk/README.md:25-37` (model), `docs/GAME-GUIDE.md:405-415` (animate).
- `.bm` / `.b16` twin: `carts/titan/src/*.lua` + `carts/titan/build/build.py` and
  `carts/titan_ex/src/*.lua` + `carts/titan_ex/build/build.py` (`Makefile:248-268,369`).
- API test cart: `tests/gameapi/cart.lua` (`docs/GAME-GUIDE.md:896`; `make test-gameapi`,
  `docs/api/bmlib.md:303-304`).

## Domande aperte

1. Does `mkbm.py --models X.glb` import skeleton and animations, or only mesh + texture?
   (Only the mesh path is visible at `scripts/mkbm.py:468-478`.)
2. Are any `.b16` limits (Lua 4 MB, CPU budget, sandbox, triangle cap) enforced at runtime
   yet, or is the 8 MB Makefile check the only one?
3. How do RGB30 games draw 3D today (ARM software only?), and at what triangle budget?
4. Are `carts/titan_ex/main.lua` and `carts/titan_ex/titan_ex.b16` stale outputs or
   sources a generator must keep?
5. `docs/RISORSE.md:351` (`mkbm.py --res FILE`) conflicts with `--res` as resolution: is
   build-time resource integration planned, and under which flag?
6. Does the Pi Market list `.b16` games? `market/README.md:23-24` and `docs/B16.md:50` disagree.
7. Should bm-forge-generated games use `"ok"`/`"back"` and `--sheet8` by default for both
   editions, so a later `.b16` edition is easy?
8. May a non-tool game (one in the Market) call `cart_data`? `l_cart_data` has no tool gate
   (`src/bm/runtime.c:6798-6928`); any Market sandbox rule is NON VERIFICATO.
