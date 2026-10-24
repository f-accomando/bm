# bm native formats (reference for bm-forge)

Every claim cites `file:line`. "NON VERIFICATO" = inferred, not seen in code.
All integers **little endian** (`src/bm/bm.h:2`, `src/bm/format.c:13-14`). Floats are IEEE f32 LE
(`src/bm/format.c:125-131`). Strings are fixed-size, zero padded, UTF-8.

## Map of the code

| Format | C (kernel) | Python (PC) | Lua (console tools) |
|---|---|---|---|
| Container `.bm`/`.b16`/`.bme`/BMRES | `bm_parse`, `bm_parse_any`, `bm_rewrite_with` (`src/bm/format.c:525,760,765,874`) | `mkbm.pack` (`scripts/mkbm.py:364`), `bmres.load/dump/write` (`scripts/bmres.py:133,230,257`), `bmmesh.cart_sections/rewrite_cart` (`scripts/bmmesh.py:179,196`) | `cart_write` (`src/bm/runtime.c:6337-6350`), `cart_data` (`src/bm/runtime.c:6794`) |
| MESH (8) | `bm_mesh_check/model/inset`, `bm_model_vertex/face/face_light` (`src/bm/format.c:136-233`) | `bmmesh.encode/decode` (`scripts/bmmesh.py:30,160`), `bmres.mesh_parse/mesh_build` (`scripts/bmres.py:553,576`) | `T.encode_mesh/split_mesh/decode_model` (`src/script/bm3d.lua:148,224,290`), bm Mesh `encode_model` (`carts/mesh/main.lua:130`) |
| ANIM (9) | `bm_anim_check/rig`, `bm_rig_read/bone/clip`, `bm_clip_key` (`src/bm/format.c:245-407`), `pose_at`, `skin` (`src/bm/runtime.c:2230,2283`) | `bmmesh.encode_anim/decode_anim` (`scripts/bmmesh.py:100,124`), `bmres.anim_parse/anim_build` (`scripts/bmres.py:610,631`) | `T.encode_anim/split_anim/decode_rig` (`src/script/bm3d.lua:195,238,256`), bm Mesh `encode_rig` (`carts/mesh/main.lua:152`) |
| AUDIO (6) | `au_parse` (`src/audio/player.h:169`) | `bmaudio.pack/unpack` (`scripts/bmaudio.py:416,479`) | bm Sound (`carts/sound/main.lua:176,216`) |
| `.bme` project | `bm_make_copy`, `bm_build` (`src/bm/project.c:136,162`) | `mkbm.mark_project` (`scripts/mkbm.py:418`), `bmres.write` (`scripts/bmres.py:257-262`) | via `cart_write` / `cart_build` (`src/bm/runtime.c:6467-6468,5941`) |

## Container (`.bm` cartridge, header 128 bytes)

Spec: `src/bm/bm.h:1-19`. Parser: `parse()` `src/bm/format.c:525-758`.

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | char[8] | magic | `"BMCART\0\0"`; old `"BM33CART"` still read (`src/bm/bm.h:4-5`, `src/bm/format.c:8-10`). Writers always write `BMCART\0\0` (`src/bm/format.c:953`, `scripts/mkbm.py:411`, `scripts/bmres.py:241`) |
| 8 | u16 | version | must be 1 (`src/bm/format.c:530`) |
| 10 | u16 | header size | must be 128 (`src/bm/bm.h:139`, `src/bm/format.c:530`) |
| 12 | u16 | width | |
| 14 | u16 | height | allowed pairs: 640x360, 480x270, 320x180, 256x256, 360x360 (`src/bm/format.c:552-555`) |
| 16 | u8 | pixel format | must be 1 = RGB565 (2 = XRGB8888 reserved, rejected) (`src/bm/bm.h:140-141`, `src/bm/format.c:556-557`) |
| 17 | u8 | section count | max 255 by type; `bm_rewrite_with` keeps at most 32 (`src/bm/format.c:879`) |
| 18 | u16 | flags | "reserved (0)" in `bm.h:13`; bit 0 = project `.bme` (`src/bm/project.h:15`) |
| 20 | u32 | CRC-32 | of every byte from 128 to EOF (table + bodies + padding) (`src/bm/bm.h:15`, `src/bm/format.c:532-541`). zlib CRC-32 (poly `0xEDB88320`, `src/lib/crc32.c:27,54-57`; Python uses `zlib.crc32`, `scripts/mkbm.py:242-243`) |
| 24 | char[48] | title | writers put at most 47 bytes + NUL (`src/bm/format.c:960`, `scripts/mkbm.py:413`); bmres up to 47 + NUL, no cut UTF-8 (`scripts/bmres.py:122-130,248`) |
| 72 | char[32] | author | at most 31 bytes (`src/bm/format.c:961`, `scripts/mkbm.py:414`) |
| 104 | char[16] | built-from | project file name a game was built from (`src/bm/project.h:16`); else zero |
| 120..127 | | reserved 0 | (`src/bm/bm.h:18`) |

**Section table** at 128: `count x {u32 type, u32 offset, u32 size, u32 reserved}` (`src/bm/bm.h:19`,
`src/bm/format.c:565-569`). Offsets absolute. Bodies are written one after another, each padded with
zeros to a multiple of 4; `size` is the unpadded length (`src/bm/format.c:939-952`,
`scripts/mkbm.py:400-407`, `scripts/bmmesh.py:203-207`). Reader only requires `offset+size <= len`
(`src/bm/format.c:568`).

**Order written**: mkbm: COVER first (menu peeks it, `src/bm/bm.h:26-27`, `src/bm/format.c:416-446`), then
LUA, SHEET/SHEET8, MAP, LAYERS, FLAGS, SPRITES, BOXES, AUDIO, MESH, extra (ANIM) (`scripts/mkbm.py:371-398`).
`bm_rewrite_with` keeps the old order, replaces in place, appends new ones at the end (`src/bm/format.c:891-938`).

**Section types** (`src/bm/bm.h:143-156`): 1 LUA, 2 SHEET, 3 MAP, 4 COVER, 5 SHEET8, 6 AUDIO, 7 OLD_ANIM,
8 MESH, 9 ANIM, 10 INFO, 11 SPRITES, 12 LAYERS, 13 FLAGS, 14 BOXES. Unknown types are ignored
(`src/bm/format.c:692-693`) and kept by rewrites (`src/bm/bm.h:337-339`, `scripts/bmres.py:133-168`).
Legacy: type 6 without `"BMAU"` at 0 is read as MESH, type 7 as ANIM; always written as 8/9
(`src/bm/bm.h:77-79`, `src/bm/format.c:667-691`, `scripts/bmmesh.py:188-191`).

**Rules checked by `parse`**: a cartridge needs LUA (`src/bm/format.c:734-736`); not SHEET and SHEET8
together (`src/bm/format.c:698-699`); LAYERS must match MAP w/h (`src/bm/format.c:700-701`). Broken INFO,
SPRITES, BOXES (or zones out of the sheet, boxes of unknown zones) are dropped in a cartridge but reject a
resource file (`src/bm/format.c:575-607,704-732`). Duplicate known sections: C keeps the last one read
(each `case` overwrites, `src/bm/format.c:574-691`); `bmres.check` rejects them (`scripts/bmres.py:187-189`).

### Simple sections

- **LUA (1)**: UTF-8 source, no header (`src/bm/bm.h:22`, `src/bm/format.c:608-611`).
- **SHEET (2)**: `u16 w, u16 h`, then `w*h` RGBA8888; alpha < 128 = transparent; `size == 4+w*h*4`;
  w,h in 1..4096 (`src/bm/bm.h:23,169`, `src/bm/format.c:612-620`).
- **MAP (3)**: `u16 w, u16 h`, then `w*h` u16 cells row by row (sprite index, 0 = empty)
  (`src/bm/bm.h:24`, `src/bm/format.c:635-642`).
- **COVER (4)**: `u16 w, u16 h`, RGBA8888; w,h 1..512 (`src/bm/format.c:658-666`); mkbm writes 88x88
  (`src/bm/bm.h:170-173`, `scripts/mkbm.py:246,271-304`); 128x80 in older files.
- **SHEET8 (5)**: `u16 w, u16 h, u16 ncol (1..256), u16 0`, palette `ncol x RGBA8888`, then indices row by
  row as runs: byte `t<128` -> `t+1` literal indices follow; `t>=128` -> one index repeated `t-126` times
  (3..129) (`src/bm/bm.h:28-33`, `src/bm/format.c:37-63`). Must consume exactly to `size`, every index
  `< ncol` (`src/bm/format.c:46-62`). Canonical encoder (C and Python give the same bytes): runs of >=3
  equal indices become a repeat, literals flushed in chunks of <=128 (`src/bm/format.c:87-121`,
  `scripts/mkbm.py:307-343`, `src/bm/bm.h:283-287`). mkbm maps every alpha<128 pixel to `00000000`
  (`scripts/mkbm.py:313`).
- **AUDIO (6)**: the `BMAU` bank, own format in `src/audio/player.h:8-64` (magic, u8 version 3/2/1,
  counts at 5..9, 48-byte sounds, sfx, patterns, songs, samples). Recognized only if `size>=16` and
  starts with `BMAU` (`src/bm/format.c:668`). Three implementations must agree (CLAUDE.md invariant):
  `au_parse`, bm Sound, `scripts/bmaudio.py`.
- **INFO (10)**: UTF-8 text <= 16 KB, `key: value` lines, then blocks `[type name]`
  (type in model, sprite, sound, sfx, song, map, palette); unknown keys kept (`src/bm/bm.h:80-86,159`,
  `docs/RISORSE.md:144-172`). Lookup trims spaces/tabs/`\r` (`src/bm/format.c:810-863`). Writer:
  `bmres.info_dump` (blank line between blocks, trailing `\n`) (`scripts/bmres.py:293-303`).
- **SPRITES (11)**: `u16 n (1..1024), u16 0`, then n x 28 bytes: `char[16] name` (unique, non empty),
  `u16 x,y,w,h` (w,h>=1), `u8 frames (1..16)`, `u8 fps (0 still)`, `u16 0`; `size == 4+28n`
  (`src/bm/bm.h:87-91,160-162`, `src/bm/format.c:469-485,792-806`, `scripts/mkbm.py:222-223`). Frames are
  the w x h boxes to the right on the same row; must fit the sheet (`src/bm/format.c:704-714`).
- **LAYERS (12)**: `u16 w, u16 h (= MAP's), u16 n (2..8), u16 0`, `n x char[16]` names (layer 1 first,
  non empty, unique), then cells of layers 2..n (`w*h` u16 each) (`src/bm/bm.h:92-97,157-158`,
  `src/bm/format.c:506-523,770-783`). Without LAYERS layer 0 is called `"main"` (`src/bm/format.c:778`);
  mkbm omits LAYERS for one layer named main (`scripts/mkbm.py:134-142`).
- **FLAGS (13)**: `u16 per_row, u16 rows`, then `per_row*rows` bytes (one per 8x8 sheet cell); trailing
  zero rows may be omitted; `per_row >= 1` (`src/bm/bm.h:98-102`, `src/bm/format.c:651-657,785-790`).
  mkbm: `per_row = max(sheet_w//8, 1)` (`scripts/mkbm.py:163-172`).
- **BOXES (14)**: `u16 n (1..4096), u16 0`, then n x 28: `char[16] zone`, `u8 frame (0 all, 1..16)`,
  `u8 kind (0 hurt, 1 hit, 2 body, 3..255 game)`, `i16 x, i16 y`, `u16 w, h (>=1)`, `u16 0`
  (`src/bm/bm.h:103-109,163-164,228`, `src/bm/format.c:489-502,967-980`, `scripts/mkbm.py:232-233`).

## MESH section (8): bm Mesh / bm Studio models

Spec `src/bm/bm.h:36-60`; limits `src/bm/bm.h:174-178`.

Section head (8 bytes, `src/bm/format.c:133`):

| Off | Type | Field |
|---|---|---|
| 0 | u16 | models, 1..256 |
| 2 | u16 | texture inset in 1/256 sheet pixel (`bm_mesh_inset`, `src/bm/format.c:192-195`); default 0.25 px = 64 (`scripts/bmmesh.py:30,34`, `scripts/mkbm.py:477`) |
| 4 | u32 | reserved 0 |

Per model (head 24 bytes = `BM_MODEL_NAME + 8`, `src/bm/format.c:134`):

| Off | Type | Field |
|---|---|---|
| 0 | char[16] | name, non empty (`src/bm/format.c:149`), unique in the section (`src/bm/bm.h:41`; uniqueness enforced by `bmmesh.encode`, `scripts/bmmesh.py:35-40`, not by `bm_mesh_check`) |
| 16 | u16 | nverts 1..65535 (4096 before M39) |
| 18 | u16 | nfaces 1..65535 (16384 before) |
| 20 | u32 | flags: bit 0 = "lit" (baked light) (`src/bm/bm.h:238`) |
| 24 | f32[3] x nverts | x, y, z; y up; each finite and in (-1e6, 1e6) (`src/bm/format.c:154-159`) |
| .. | 24 B x nfaces | faces |

Face, 24 bytes (`src/bm/bm.h:46-60,178`, `src/bm/format.c:203-211`, Python `"<4HI6H"` `scripts/bmmesh.py:71`):

| Off | Type | Field |
|---|---|---|
| 0 | u16 a, b, c | 0-based vertex indices, each `< nverts` (`src/bm/format.c:161-163`); **clockwise seen from the visible side** (`src/bm/bm.h:46-47`, CLAUDE.md invariant on `r3d.c`) |
| 6 | u16 | reserved; in a lit model's textured face: light RGB565, top value = 2x (`src/bm/bm.h:58-60`, `src/bm/format.c:216-225`) |
| 8 | u32 | colour `0xRRGGBB`; bit 31 = textured; bits 24..30 material (`src/bm/bm.h:48-51`) |
| 12 | u16 u0,v0,u1,v1,u2,v2 | texture corners in sheet pixels x 8 (`/8.0` on read, `src/bm/format.c:209-210`); in a lit, untextured face: per corner `u = R | G<<8`, `v = B`, 128 = unchanged (`src/bm/bm.h:52-57`, `src/bm/format.c:227-232`) |

Material bits (`src/bm/r3d.h:74-90`): 31 TEXTURED, 30 EMISSIVE, 29 GLOSSY, 28 SCREEN (screen-door),
27 FLAT, 24-25 LOD k (0..3), 26 = "shows below k" (stand-in) else "k and above".

Section must end exactly at `size` (`src/bm/format.c:167`). Model stride = `24 + 12*nv + 24*nf`
(`src/bm/format.c:151`, `src/script/bm3d.lua:231`).

Writers and their differences:
- `bmmesh.encode`: names cut to **15** bytes + NUL (`scripts/bmmesh.py:37`); uv clamped `round(t*8)` to
  0..65535 (`scripts/bmmesh.py:64`); writes lit flag and lights (`scripts/bmmesh.py:46-70`).
- `bm3d.lua T.encode_mesh`: names up to **16** bytes, no NUL (`src/script/bm3d.lua:135-138`); flags always
  0 (`src/script/bm3d.lua:189`); colour `& 0xFFFFFF` (material bits lost) (`src/script/bm3d.lua:169`);
  quads split `{1,2,3},{1,3,4}` (`src/script/bm3d.lua:142,170`); vertices deduplicated by position at 1e-4
  plus bone (`src/script/bm3d.lua:87-89,153-165`); limits still 4096/16384 (`src/script/bm3d.lua:34,187-188`).
- bm Mesh `encode_model`: flags 0, colour `& 0xFFFFFF`, `-1` = textured (`carts/mesh/main.lua:134-146`);
  limits 65535/65535/256 (`carts/mesh/main.lua:28`).
- Studio's decoder re-merges two consecutive triangles `(a,b,c),(a,c,d)` into a quad
  (`src/script/bm3d.lua:288-339`).

## ANIM section (9): bm Animator skeletons and clips

Spec `src/bm/bm.h:61-76`; limits `src/bm/bm.h:179-183`: 64 bones, 255 clips, 1024 keys per clip,
bone 44 B, pose 28 B.

Section head 8 B: `u16 rigs, u16 0, u32 0` (`src/bm/bm.h:62`, `src/bm/format.c:237`). `rigs` may be
0 (no lower bound in `bm_anim_check`, `src/bm/format.c:306-320`). Must end exactly at `size`
(`src/bm/format.c:319`).

Rig head, 24 B (`src/bm/format.c:238`):

| Off | Type | Field |
|---|---|---|
| 0 | char[16] | model name (the MESH model it moves; non empty) (`src/bm/format.c:247`) |
| 16 | u16 | bones 1..64 |
| 18 | u16 | clips 0..255 |
| 20 | u16 | vertices = the model's nverts, 1..65535 (`src/bm/format.c:250`) |
| 22 | u16 | reserved |

Then:
- **bones x 44 B**: `char[16] name; i16 parent` (-1 or an earlier index, `parent < i`,
  `src/bm/format.c:257-259`); `u16 0`; `f32 head[3]` at +20, `f32 tail[3]` at +32, rest pose, finite
  (|v| < 1e9) (`src/bm/format.c:241,260-262,359-373`).
- **nverts x u8**: bone index of each vertex (`< bones`), zero padded to a multiple of 4
  (`src/bm/format.c:266-269`).
- **clips**: head 24 B: `char[16] name; u16 keys (1..1024); u8 mode (0 linear, 1 smooth, 2 step, 3 ease-in, 4 ease-out);
  u8 flags (bit 0 loop); f32 length (s, 0 < length < 1e6)` (`src/bm/format.c:239,275-278,375-395`).
  Then `keys x (4 + bones*28)`: `f32 time` (non decreasing, `<= length + 1e-4`), then per bone
  `f32 q[4]` (x,y,z,w, rotation relative to parent) + `f32 t[3]` (translation) (`src/bm/format.c:286-296,397-407`).

Linking: a rig is applied only if a MESH model has the same name and `rig.nverts == model.nverts`
(`src/bm/runtime.c:2161`); name match = first 16 bytes, NUL-terminated or full
(`src/bm/format.c:347-349`).

Semantics (playback, `src/bm/runtime.c:2230-2321`): loop -> `t mod length`, else clamp; interpolation
between bracketing keys, wrap from last key to first in loops; `u` = linear, smoothstep
`u*u*(3-2u)` (mode 1), 0 (step), `u*u` (3 ease-in) or `u*(2-u)` (4 ease-out); quaternion slerp (shortest way) + linear translation; bone matrix
`M = M_parent * T(head + t) * R(q) * T(-head)` (`src/bm/bm.h:75-76`, `src/bm/runtime.c:2273,2296-2317`).
Rigid skinning, one bone per vertex (`src/bm/runtime.c:2319-2320`). The same maths in
`carts/animator/main.lua:772-812` (CLAUDE.md invariant; checked by hand in phase 1b, port
`bm_core/skin.py`, differences in OPEN_QUESTIONS.md items 25-29).

**fps**: the format has no fps: times are f32 seconds. bm Animator's timeline snaps to 12 frames/s
(`src/script/bm3d.lua:32`, `carts/animator/main.lua:831`). Writers: `bmmesh.encode_anim` default mode 1,
loop true, missing poses = identity `(0,0,0,1),(0,0,0)` (`scripts/bmmesh.py:115-120`); `bm3d.lua`
writes `parent-1` (Lua bones 1-based) (`src/script/bm3d.lua:202`). Coordinate note toward glTF: z flips
sign and winding inverts (CLAUDE.md invariant; see `scripts/bmmesh.py:251-388`, `src/bm/glb.c`).

## "bm3d": what the name means

Not a file format. Three meanings in the repo:
1. **Driver version** "bm3d X.Y" of r3d + gpu3d, `BM3D_VERSION "7.3"`, block `"M39"`
   (`src/gpu/version3d.h:5-9,26-27`), history table (`src/gpu/version3d.h:34-81`), docs in `docs/DRIVERS.md`.
   Format-relevant step: **5.0** = meshes up to 65535 vertices (`src/gpu/version3d.h:57`, `src/bm/bm.h:175`).
   No version field in MESH/ANIM: older kernels reject >4096 vertices only by their own check (NON VERIFICATO).
2. **Lua module** `require "bm3d"` shared by bm Studio and bm Animator: MESH/ANIM encode/decode
   (`src/script/bm3d.lua:1-7,133-340`).
3. The 3D Bench `src/bm/b3d.c` (`src/bm/b3d.h:4-17`): benchmark, not a format.

## `.b16`

Today a `.b16` is **the same `BMCART` container** with the `.b16` extension (`docs/B16.md:20-23`).
Screen 360x360 or 720x720 per `docs/B16.md:24-27`, but the parser accepts only the five pairs incl.
360x360, not 720x720 (`src/bm/format.c:552-555`). B16.md says the container has "a field that states the
profile", but no such field exists in `bm.h` or `format.c`; the sample `carts/titan_ex/titan_ex.b16` has
bytes 18..19 and 104..127 all zero (checked with Python). Detection is by extension only
(`src/kernel/market.c:306-311`, `src/rgb30/ui.c:249`, `scripts/mkmarket.py:68`). Profile rules (Lua 4 MB,
256-colour SHEET8, 8 MB cap, sandbox) are runtime/build policy, not bytes (`docs/B16.md:28-49`).
The `B16CART` 256-byte header with 24-byte section entries (`docs/B16.md:226-367`) is a **design not
implemented**: no code mentions `B16CART` (grep over *.c/*.h/*.py/*.lua). `.b16` and `.bm` are separate
editions with separate sources (CLAUDE.md "Non fare").

## BMRES resource files (`.bmm .bmi .bms .bmt .bmc .bmk`)

Spec `src/bm/bm.h:113-131`, `docs/RISORSE.md:112-140`. Same header as the cartridge except:

| Off | Type | Field |
|---|---|---|
| 0 | char[8] | `"BMRES\0\0\0"` (`src/bm/format.c:411-414`) |
| 12 | u16 | kind: 1 models `.bmm`, 2 image `.bmi`, 3 sounds `.bms`, 4 map `.bmt`, 5 palette `.bmc`, 6 kit `.bmk` (`src/bm/bm.h:117-118,167-168`, `src/bm/format.c:543-547`) |
| 14 | u16 | 0 |
| 16 | u8 | 0 |
| 24 | char[48] | display name (file name is 8.3) |

Read only by `bm_parse_any` (`src/bm/format.c:765-768`); `bm_parse` rejects it (`src/bm/format.c:528-529`).
The kind field wins over the extension (`docs/RISORSE.md:133-134`); Python maps extensions in
`scripts/bmres.py:54,79`. Allowed known sections per kind (`src/bm/format.c:450-462`, Python
`ALLOWED` `scripts/bmres.py:56`):

| Kind | Allowed (INFO always) | Required |
|---|---|---|
| 1 models | MESH, ANIM, SHEET/SHEET8 | MESH (`src/bm/format.c:738-740`) |
| 2 image | SHEET/SHEET8, SPRITES, FLAGS, BOXES | a sheet (`src/bm/format.c:741-743`) |
| 3 sounds | AUDIO | AUDIO (`src/bm/format.c:744-746`) |
| 4 map | MAP, LAYERS, FLAGS, SHEET/SHEET8 | MAP + sheet (`src/bm/format.c:747-749`) |
| 5 palette | SHEET8 | SHEET8 with h == 1 (`src/bm/format.c:750-752`) |
| 6 kit | MESH, ANIM, SHEET/SHEET8, SPRITES, AUDIO, MAP, LAYERS, FLAGS, BOXES | one of mesh/sheet/audio/map (C, `src/bm/format.c:753-755`); Python: any non-INFO (`scripts/bmres.py:185-186`) |

A known but disallowed section, or LUA, is an error (`src/bm/format.c:571-573`); unknown types ignored.
INFO > 16 KB is an error in BMRES (`src/bm/format.c:578-581`). Writer: `bmres.dump` writes a zeroed
header + magic, version 1, size 128, kind, count, CRC, name, author (`scripts/bmres.py:242-250`).
Semantics: textures of `.bmm` hold only the used cells; `.bmt` only the used tiles; `.bmc` pixel i =
colour i (`docs/RISORSE.md:79-110`). Integration/extraction rules: `docs/RISORSE.md:213-242`; CLI
`scripts/bmres.py` (`docs/RISORSE.md:341-354`).

## `.bme` project

`src/bm/project.h:1-16`, `src/bm/project.c`. A `.bme` is a `BMCART` file with **bit 0 of the u16 at 18**
set (`BM_FLAG_PROJECT`); recognized by extension `.bme` (`src/bm/project.c:19-22`), games are `.bm`/`.b16`
(`src/bm/project.c:24-27`). The flag is outside the CRC (CRC starts at 128), so setting it needs no CRC
update (`src/bm/runtime.c:6467-6468` sets it after `bm_rewrite_with` computed the CRC).

- **Copy game -> project** `bm_make_copy`: same bytes, flag set, bytes 104..127 zeroed
  (`src/bm/project.c:136-147`). Name: dir + 8.3 base (A-Z 0-9 _ -, upper case, max 8, default `PROJECT`)
  + `.BME`, then `BASE1..BASE9` with the 8th letter dropped (`src/bm/project.c:38-92`).
- **Build project -> game** `bm_build`: flag cleared, bytes 104..127 zeroed, project file name (upper case)
  at 104 (`char[16]`), written as `BASE.BM`: replaces the game whose 104 matches (case-insensitive), else
  first free name (`src/bm/project.c:150-193`).
- `bm_rewrite_with` builds a fresh header (calloc): bytes 18..19 and 104..127 are **not** carried over;
  callers re-set the project bit (`src/bm/format.c:942-962`, `src/bm/runtime.c:5927,6468,6716`).
- Python: `mkbm.py -o X.bme` and `bmres.write("*.bme")` OR the bit in (`scripts/mkbm.py:418-423,483-484`,
  `scripts/bmres.py:259-262`). Lua rules mirror: `tests/studio/project_rules.lua`.
- Tool writes go through `write_target`: saving a game asks for a copy; a new `.bm` path becomes `.bme`
  (`src/bm/runtime.c:5287-5330`).

## Samples in the repo (golden-test candidates)

- `carts/village/assets/models.bm`: BMCART 320x180, 4 sections LUA, SHEET8, MESH (8 models, inset 64 = 0.25,
  untextured-material bits all 0), ANIM (rig `villager`, 7 bones, 112 verts, clips idle/walk/wave, mode 1,
  loop) (decoded with `bmmesh`).
- `carts/titan_ex/titan_ex.b16`: BMCART 360x360, COVER + LUA + SHEET8 (1.7 MB).
- `video/0{2..7}-*/start.bm`: tiny carts (e.g. `video/02-sdk/start.bm`: SHEET8 + LUA, 640x360).
- `carts/kitchen/models/chef{1..4}.glb`: glTF inputs for `bmmesh.models_from_file` / `src/bm/glb.c`.
- No `.bme` or BMRES file is tracked; tests build them: `tests/res/test_bmres.py`, `tests/bm/test_res.c`,
  `tests/bm/test_bm.c`, `tests/studio/mesh_host.lua`, `tests/studio/anim_figure.py` (NON VERIFICATO: content
  of these tests not read).

## Domande aperte

1. Nomi a 16 byte: Python ne scrive 15 + NUL (`scripts/bmmesh.py:37`), Lua 16 senza NUL
   (`src/script/bm3d.lua:136`). Quale e' il canone per bm-forge? (C accetta entrambi.)
2. `bm3d.lua` ha ancora i limiti 4096/16384 (`src/script/bm3d.lua:34`) mentre C e bm Mesh sono a 65535:
   voluto (Studio/Animator) o residuo?
3. I writer Lua (`bm3d.lua:169,189`, `carts/mesh/main.lua:142`) perdono bit materiale (24..30) e flag
   "lit" quando ricodificano un modello: bm-forge deve preservarli sempre?
4. Unicita' dei nomi modello: dichiarata in `bm.h:41` ma non controllata da `bm_mesh_check`; idem
   nomi di ossa e clip (nessun controllo). Rifiutare in bm-forge?
5. `.b16`: B16.md §0 parla di un campo di profilo nell'header che non esiste nel codice. Va aggiunto
   (dove? byte 18 bit 1? 104?) o l'estensione basta?
6. 720x720 per `.b16` (`docs/B16.md:24`) non e' tra le risoluzioni accettate da `format.c:552-555`.
7. `bm_rewrite_with` non sa scrivere 480x270 (diventa 640x360, `format.c:956-957`) e azzera 104..127:
   bm-forge deve preservare l'header originale come fa `bmmesh.rewrite_cart` (`scripts/bmmesh.py:209`)?
8. Sezioni duplicate: C tiene l'ultima, `bmres` rifiuta. Quale comportamento imitare?
9. ANIM con 0 rig (size 8) e' valido per C: un writer deve ometterlo (bm Mesh scrive `false`,
   `carts/mesh/main.lua:468`)?
10. Il CRC copre il padding a 4 byte: il padding deve essere sempre zero (tutti i writer lo fanno) ma il
    reader non lo verifica; ok come regola "writer"?
