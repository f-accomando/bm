# API: nano8, light, 3D and collision worlds

Part of the game API. Index and basics: [API.md](../API.md).

### nano8 (the `n8` library)

Every cartridge also sees the `n8` table: the **nano8** machine (`src/bm/n8*.c`), with the
functions of the `.p8` cartridges (`n8.spr`, `n8.map`, `n8.print`, `n8.peek`…, with their
arguments and numbers as they want them) and those to load, start and show them
(`n8.load`, `n8.power`, `n8.buttons`, `n8.blit`, `n8.preview`, `n8.compile`). It is made
for `carts/nano8` (see the comment in `src/bm/n8lua.c`); a `.bm` game does not need it.

### Light

Dark scenes lit only by lamps, candles, torches: the frame's drawing is multiplied by a
"light map" computed in C (a grid every 4 pixels, interpolated and slightly dithered). From
the first `light_begin()` the cartridge draws in RAM instead of directly on the screen.

| Function | Description |
|---|---|
| `light_begin([ambient])` | starts the frame's lights: `ambient` is the colour of the background light (`0x000000` pitch dark, `0xFFFFFF` no effect) |
| `light(x, y, radius, colour, [intensity])` | a soft light in world coordinates (`camera` holds); more lights add up, up to 2× the brightness |
| `light_end()` | applies the light to everything drawn; what you draw after (HUD, text) stays at full light |

```lua
cls(0); map(...); spr(...)                  -- the scene
light_begin(0x0A0A16)                        -- dark blue night
light(lx, ly, 50 * (0.95 + math.random() * 0.1), 0xFFB060)   -- a flickering street lamp
light(px, py, 40, 0xFFC888, 0.9)             -- the player's lantern
light_end()
print("life", 4, 4, 0xFFFFFF)                -- the HUD is not darkened
```

Complete example: `carts/hunt` (Hunter's Night).

### Light by levels (as in Dank Tomb)

The other light, that of the PICO-8 game *Dank Tomb*: each pixel has a **light level** (0
the darkest) and its colour becomes the one a **fade table** gives at that level. Lamps
make concentric rings of levels, from their level in the middle down to 0 at the edge; the
rings' edges are mixed with a 4×4 ordered dither; where two lamps meet the stronger wins.
The tables are chosen by the cartridge: shadows can turn night blue and colours near the
lamps orange, staying on the colours of its own palette. All in C (`g16_fade_*` in
`src/bm/gfx16.c`).

| Function | Description |
|---|---|
| `fades(tables)` | the tables: `{ {colour, l0, l1, ...}, ... }`, for each colour of the palette what it becomes at level 0 (the darkest), 1, ...; all rows have the same number of levels (2–16, up to 255 colours). The colours without a table are scaled as the average of the tables. Returns the number of levels |
| `dark_begin([ambient])` | starts the frame: every pixel at level `ambient` (default 0); from here the cartridge draws in RAM |
| `glow(x, y, radius, level, [dither])` | a lamp in world coordinates (`camera` holds): `level` in the middle, 0 at `radius`; `dither` 0–1 (default 0.5) is how much the rings' edges mix (0 sharp rings, 1 a continuous dithered shade) |
| `dark_end()` | applies the levels to everything drawn; what you draw after (flames, sparks, HUD) stays as it is and "shines" |

```lua
fades(TABLES)                                -- once, in _init
cls(0); map(...); spr(...)                   -- the scene at full light
dark_begin(1)                                -- night: level 1 everywhere
glow(lx, ly, 72 + math.random(2), 6)         -- a street lamp
glow(px, py, 28, 3)                          -- the little light round the player
dark_end()
spr(FLAME, fx, fy)                           -- the flames are not darkened
```

Complete example: `carts/yharnam`; a small one: `SQUARE_CART` in `tests/qemu_test.py` (a lamp on a 256×256 cartridge).

### 3D (software)

| Function | Description |
|---|---|
| `mesh(v, f, [uv])` | a mesh from tables (up to 65535 vertices and 65535 faces: 4096 and 16384 before bm3d 5.0): `v` = {x,y,z, x,y,z, …}, `f` = {a,b,c,colour, …} (indices from 1; a face shows from the side where its vertices appear **clockwise**). With `uv` (6 numbers per face: u,v of the three vertices in sprite sheet pixels) the faces with colour `-1` have the sprite sheet's **texture** (perspective correct, transparent pixels stay empty). The colour can have the **material bits** (table below) |
| `mesh_sphere([r, segments, c1, c2])`, `mesh_cube([c])` | ready-made meshes |
| `model(name)` / `model(n)` | a **3D model of the cartridge** (made with [bm Studio](../../sdk/README.md), MESH section) as a mesh, with the sprite sheet's texture; `n` counts from 1; `nil` if it is not there. Every call builds a new mesh: do it in `_init`. A model with **baked light** (MESH's "lit" bit, `src/bm/bm.h`: the light of each corner of the faces, made by a script, as Overbit's map) is drawn smooth with that light, without sun or sky but with the lamps (`lamp3d`) and the fog: it costs less than computed light; its textured faces (windows, signs) have one light per face, coloured, and the fog |
| `models()` | the names of the cartridge's models, in order (`{}` if it has none) |
| `bounds3d(m)` | `x0, y0, z0, x1, y1, z1`: the box round a mesh's vertices, in its coordinates (before moving, turning and scaling it with `draw3d`): to centre it, for collisions |
| `animate(m, [anim, t, anim2, t2, k, bone])` | **skeletal animation**: a model with a skeleton from [bm Animator](../../sdk/README.md#bm-animator) takes the pose of animation `anim` (name or number) at time `t` in seconds (looping, if the animation loops); with `anim2, t2` it blends two animations (`k` from 0, the first only, to 1, the second only: to go from one to the other); with `bone` the second holds only for that bone and those under it (a torso that shoots on running legs); without an animation the rest pose. Returns the animation's length. An error if the mesh has no skeleton or the animation is not there. The bones move the vertices while the mesh is drawn (rigid skinning): `animate` costs only the bones |
| `bone_turn(m, bone, [rx, ry, rz])` | from now on every `animate` also turns the bone by these angles (radians, x then y then z, in the parent's frame) over the animation: aiming up and down, legs that follow the walking direction. `bone_turn(m, bone)` takes it away |
| `bones3d(m)` | the names of the skeleton's bones, in order |
| `hit3d(m, x, y, z, ry, scale, ox, oy, oz, dx, dy, dz, [maxd])` | `t, bone`: the ray from `o` along `d` against the mesh's bones in the last pose, drawn at (x, y, z) turned by `ry` and scaled; each bone is a capsule from head to tail as wide as its vertices. The closest hit within `maxd` (`t` in units of `d`), or `nil`: hitboxes that follow the animation (headshot: bone `"head"`) |
| `clips(m)` | a model's animations: `{ {name=, length=, loop=}, ... }` (`{}` without a skeleton) |
| `bone3d(m, bone)` | `x, y, z, cx, cy, cz`: where a bone's head and tail (name or number) are in the last pose, in the model's coordinates (as `bounds3d`); `nil` if the bone is not there. To attach objects to the hands (the head), a sword's tip (the tail), lights, effects |
| `draw3d(m, x, y, z, [rx, ry, rz, scale, flags])` | draws a mesh with z-buffer and per-face light. `flags`: 1 = no z-buffer (neither test nor write: floors and backgrounds drawn first, faster), 2 = no light (flat colours), 4 = **smooth** (Gouraud: light computed at the vertices and shaded over the face, dithered; faces sharing the same vertex indices look like a curved surface, for sharp edges use separate vertices or the "flat" bit), 8 = the mesh's **shadow** on the plane `y` of the point (along the sun: it darkens what is already there, the mesh is not drawn), 16, 32, 48 = **level of detail** 2, 1, 0 (only that level's faces, see the material bits; without: 3, all), 64 = **in front** (first-person weapons and arms: the z-buffer under it is cleared, precise depths from 0.1 units), 128 = **flat textures** (the textured faces drawn as flat colours, each the average of its texels; transparent texels skipped, a face of holes only is grey; the same mesh, bones and animation, lit faces keep their light, the other faces are unchanged; on the GPU such a mesh goes triangle by triangle, a little slower; the colours are kept per mesh and texture version, bm3d 7.0); they add up (e.g. 4+128) |
| `sky3d(sun, sky, ground)` | colours (0xRRGGBB) of the sun's light and of the ambient light coming from above and below (faces turned up take the sky, those turned down the ground); `sky3d()` goes back to white |
| `shine3d(spec, exponent, rim)` | the sun's reflections on glossy faces (`spec` 0–2, `exponent` 4–64: higher, smaller) and light on the shapes' rim (`rim` 0–1) |
| `textures3d([on])` | `on`: `textures3d(false)` makes every `draw3d` behave as with flag 128 (textured faces as flat colours, their average texel), `textures3d(true)` goes back to the textures (default); returns the current state. For a "flat / textured" option in a game (bm3d 7.0) |
| `shadow3d(style)` | the shadows of `draw3d` with flag 8: 0 darken (default), 1 dithered black (without reading the screen) |
| `point3d(x, y, z, radius, colour, [flags])` | a round point of that radius in the world, behind the closer things (it does not write the z-buffer): particles, sparks, bullets. `flags` 1 = every other pixel. Returns the pixels |
| `line3d(x0, y0, z0, x1, y1, z1, colour, [width, flags])` | a 3D line, cut by the near plane and hidden by the closer things: tracers, beams |
| `sprite3d(sx, sy, sw, sh, x, y, z, width, [flags])` | a rectangle of the sprite sheet facing the camera, `width` units wide in the world, hidden by the closer things: explosions, smoke, icons over the characters |
| `camera3d(x, y, z, [yaw, pitch, fov, roll])` | the camera (default at z = −5, fov 60°); `roll` tilts the view (radians) |
| `light3d(x, y, z, [ambient])` | the light's direction and the ambient light (0–1) |
| `zclear()` | clears the z-buffer (every frame, before `draw3d`) |
| `gpu3d([on, aa, vs, queue, impostor, later])` | `on, aa, vs, version, queue, impostor, later`: whether the GPU draws the 3D, whether with 4× MSAA and whether the GPU's vertex shader places the models' vertices (M36): `vs` is `false`, `1` (the scenery: models without light or with light at the corners) or `2` (all, also those lit by the sun and with bones; `true` is 2); `queue` (M35): whether the GPU's job starts without waiting for it and the next frame's `_update` runs meanwhile (where the boot test saw it work); the 2D drawn after the 3D (the HUD) and that of the `_update` are recorded and go on the page when the GPU is done, in the same order (the result is the same; an `_update` that draws 3D or reads the screen with `pget` goes back to running after the frame); `queue` `2`: two jobs in flight (M39); `queue` `true` asks for the console's own level (`gpu3d_queue` in Settings, 1 or 2), `false` for none, and the fifth value returned is the level in force (`1`, `2`) or `false`; `later` (M39, bm3d 6.9): each frame is shown a frame later, so the next frame's `_update` and `_draw` run while the GPU draws it (the 6th argument, only with `queue` `2`; never in a game played `online`): the same pictures, a frame later (see *A frame later* below). With arguments it changes it for this cartridge (a "3D: GPU / GPU + VS / GPU + AA / ARM" menu in the game); `on` stays `false` if there is no GPU (QEMU, `gpu3d=0`) and in 256×256 games (the GPU writes whole pages), `aa` if MSAA cannot be used and `vs` if the boot test did not see the vertex shader (and its clipping) work. `version` is the version of the 3D drivers this choice reproduces (`"0.2"` the ARM, `"2.1"`, `"3.0"`, `"3.4"`, `"4.1"`, `"6.9"` a frame later: `docs/DRIVERS.md`). `impostor` (off by default): a model whose box on the screen is at most that many pixels (2–64) of a 1080-line screen (the game's screen is scaled to the TV's height: at 640×360 that is a third as many of the game's pixels, at 720p two thirds, so a model looks the same size when it is chosen whatever the resolution) is drawn as one quad facing the camera in its average colour, lit as a whole (2 triangles instead of all its faces), by the GPU or the ARM alike; a model with bones too, by the box of its pose (where `animate()` put each bone's part; an animated model out of the view is then also skipped, as one without bones); `false` or `0` turns it off, and it can be given with `on` `nil` (`gpu3d(nil, nil, nil, nil, 16)`); the choice depends only on the camera, the model, its pose and the size of the game's screen, so it is the same on every console of a match. The console's default is `r3d_impostor` in `bm/config.txt` (*Settings > Screen and sound > 3D impostors*). At the end it goes back to the settings' |
| `fog3d(colour, near, far)` | fog: the faces fade into the colour between the two distances; `fog3d()` takes it away |
| `lamp3d(i, x, y, z, radius, [k, colour])` | point light `i` (1–4): the faces with their centre within `radius` get brighter, up to `k` more (default 1) in the middle, of the `colour` given (white if missing); `lamp3d(i)` turns it off, `lamp3d()` all. With `light3d` at a low ambient it makes dark scenes with lanterns |
| `project3d(x, y, z)` | a point of the world → `sx, sy, depth` on the screen (`nil` if it is behind the camera): to draw in 2D things lined up with the 3D (horizon, sights, labels) |
| `visible3d(x, y, z, r)` | `false` if a sphere (centre, radius) cannot be seen: behind the camera, beyond an edge of the screen, or (after `pvs3d`) on pieces of the map that the camera's cell does not see. To skip the characters and effects hidden by the walls before drawing them (bm3d 5.5) |
| `pvs3d(t)` | the map's precomputed visibility for `visible3d`: `t = { x0, z0, cell, nx, nz, max_y, sets, boxes }`, a grid of `nx`×`nz` cells of side `cell` on the ground from (`x0`, `z0`); `sets` a string a cell (row by row along x), each byte the number (from 1) of a piece seen from it; `boxes` six numbers a piece (`x0 y0 z0 x1 y1 z1`). With the camera above `max_y` or out of the grid only the view counts; a sphere off every piece is seen. `pvs3d()` forgets it |

**Material bits** in a face's colour (of `mesh()` and of the models; 0 = the usual face):

| Bit | Value | Effect |
|---|---|---|
| 30 | `0x40000000` | **emissive**: a flat colour, without light (lights, screens, energy) |
| 29 | `0x20000000` | **glossy**: the sun's reflection (`shine3d`) |
| 28 | `0x10000000` | **screen door**: every other pixel, what is behind shows (shields, glass) |
| 27 | `0x08000000` | **flat**: also in a smooth drawing (flag 4) it takes the light of its plane; sharp edges can share vertices |
| 24–26 | | **level of detail**: bits 24–25 a level `k` (0–3); bit 26 at 0 the face shows from `k` up (a detail), at 1 below `k` (a simple version) |

```lua
local SHIELD = 0x40C8FF | 0x40000000 | 0x10000000   -- light blue, emissive, screen door
local DETAIL = 0xFFFFFF | 0x02000000                 -- white, only from detail 2 up
```

### Collision worlds

Solid boxes, rays and bodies that move sliding on the walls (in C: much faster than in
Lua). Complete example: `carts/overbit`. For the 2D map there are the tiles' flags
(`mflags`) and bmlib's `lib.move` / `lib.step`.

| Function | Description |
|---|---|
| `world3d()` | an empty collision world |
| `world_box(w, x0, y0, z0, x1, y1, z1, [tag])` | a solid box; returns its number |
| `world_ray(w, ox, oy, oz, dx, dy, dz, [maxd, ground])` | `t, nx, ny, nz, box`: the first point hit along the ray (the face's normal; box 0 = the ground `y = 0`, which counts unless `ground` is `false`), or `nil` |
| `world_move(w, x, y, z, r, h, dx, dy, dz, [step, on_ground])` | `x, y, z, flags`: a body (feet at `x, y, z`, radius `r`, height `h`) moved by `d`, sliding on the walls and climbing steps up to `step` (0.45) if it was on the ground. `flags`: 1 on the ground, 2 a wall (4 along x, 8 along z), 16 a ceiling |
| `world_floor(w, x, z, y, r, [step])` | the floor's height under `(x, z)` |

Triangles that cross the plane near the camera are cut, not dropped: floors and big objects
stay whole even when they pass by the camera.

**3D on the GPU (M33).** The Pi's GPU (V3D) draws the triangles; with *Settings > Graphics
> 3D of the games* on `ARM` (`gpu3d=0` in `bm/config.txt`), and in QEMU, the ARM draws
them. The same functions, no change in the cartridges. The ARM goes on transforming,
lighting and clipping; the GPU fills the pixels with a 24-bit z-buffer, shading without
dithering and textures with the nearest texel. The waiting 3D is drawn before every 2D
drawing that follows it, before `pget`, `sset` and at the end of the frame. If a cartridge
draws more 3D after the 2D in the same frame, from the next frame the GPU keeps the
z-buffer between the two parts (about 1 MB of memory written and read again per frame; not
the first frame): it is still better to draw all the 3D first and then the HUD. The GPU's
z-buffer starts from zero every frame, even without `zclear()`. `stat(9)` is 1 when the GPU
does the 3D. If the GPU does not answer, the kernel goes back to the ARM by itself and
writes it in the log. The GPU also draws the shadows (`draw3d` with flag 8: always dithered
black, `shadow3d`'s style 1, over the things already drawn thanks to the z-buffer), the 3D
effects (`point3d`, `line3d`, `sprite3d`), the screen-door faces and the textures of the
models with baked light (light and fog shaded at the corners, also with the `lamp3d`s).
Only the faces **textured and screen door** together it cannot do: at the first one the
cartridge goes to the ARM for the rest of the run (a line in the log; the mixed frame is not
shown). A frame that starts with `cls()` costs the GPU less: the tiles start from the colour
of `cls` instead of reading the page again. Textures with sides that are multiples of 32
(128×128 sprite sheets, 256×256, …) go to the GPU in T-format, the tiled format of its
cache, faster to read.

**Anti-aliasing (M34).** *Settings > Graphics > 3D anti-aliasing: 4x* (`gpu3d_aa=1` in
`bm/config.txt`) makes the GPU draw the 3D with 4× MSAA: four samples per pixel, the
average at the end of the tile, triangles' edges without steps. Nothing changes in the
cartridges. It holds only where the GPU bears it (the boot test says *4x: not on this GPU*
otherwise) and only in the jobs without a kept z-buffer: cartridges that draw 3D, then 2D,
then more 3D in the same frame stay without anti-aliasing (the 4-sample z-buffer cannot be
saved). If the test finds the GPU cannot load the page again into the 4 samples, MSAA is
used only in the frames that start with `cls()`. On the ARM there is no anti-aliasing.

**A frame later (M39, bm3d 6.9).** *Settings > Screen and sound > 3D frame shown: A frame
later* (`gpu3d_later=1`, with the frame queue on two jobs, `gpu3d_queue=2`; from a game the
sixth argument of `gpu3d`) shows each frame a frame later: at the end of `_draw` the GPU
starts the frame's 3D and the frame waits, while the next `_update` and `_draw` run on
another page; the 2D drawn after the 3D goes on the waiting frame when the GPU is done, and
that frame is shown at the end of the next one. Nothing changes in the cartridges and the
pictures are the same, a frame later (the input reaches the screen a frame later than
without the queue). Keep the game's state in `_update` (as for online games) and draw the
HUD after the 3D; `pget` reads the page being drawn. Not in games played `online` (left
out there), via RAM (`light_begin`, `dark_begin`) and in the frame of a `screen()` change.
Off by default until it is measured on the Pi.

Complete example: `carts/astrowing` (Star Fox-style flight: models built in code, horizon
with `project3d`, fog, explosions, boss). With bm Studio's models: `carts/village`
(`model()` for each model, terrain drawn without z-buffer, night with `lamp3d` and
`fog3d`; bm Animator's villager with `animate()`, two animations blended, a light in hand
with `bone3d()`, and its version as pre-rendered sprites).
