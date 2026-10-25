# The `.bm` container: the format as implemented

Reference of what the code accepts today (audit of 2026-10-13, branch `pdf-manuals`; `src/bm/bm.h` has the
original comments, `src/bm/format.c` the parser). Where the header comment of `bm.h` and the code
disagree, this page follows the code and says so. The problems found and what to do about them are in
[FORMATS-AUDIT.md](FORMATS-AUDIT.md). Items the audit could not confirm are marked **UNVERIFIED**.

One container serves four kinds of file; the kind is told by the **file extension**, not by the content:

| Extension | What | Bit 0 of the flags (`project`) | Bit 1 (`b16`) |
|---|---|---|---|
| `.bm` | game | 0 | 0 |
| `.bme` | editable project (only the tools edit it) | 1 | 0 |
| `.b16` | game, 360x360 edition for the handhelds | 0 | 1 (when made by the tools) |
| `.bmm .bmi .bms .bmt .bmc .bmk` | resource file, magic `BMRES` | - | - |

All numbers are little endian.

## Header (128 bytes)

| Offset | Size | Field | Rule in the parser (`format.c:535-579`) |
|---|---|---|---|
| 0 | 8 | magic | `BMCART\0\0` (or the old `BM33CART`); resource files `BMRES\0\0\0` |
| 8 | u16 | version | must be 1 |
| 10 | u16 | header size | must be 128 (otherwise "unsupported .bm version") |
| 12, 14 | u16, u16 | width, height | cartridge: only 640x360, 480x270, 320x180, 256x256, 360x360. In a resource file offset 12 is the **kind** (1..6) |
| 16 | u8 | pixel format | only 1 (RGB565); 2 is reserved and refused |
| 17 | u8 | number of sections | up to 255 |
| 18 | u16 | flags | bit 0 project, bit 1 `.b16`, bits 2-3 the type declared in `bm.info` (0 game, 1 app, 2 utility, 3 reserved = game), bit 4 `BM_FLAG_COMPAT`: the compatibility block at 120 is present (bm.h); defined in `project.h`, not in `bm.h`, whose comment still says "reserved (0)". `mkbm.py` writes the type bits and `bm_type_of_head()` reads them; no kernel uses them for the menu yet (D09). The project/`.b16` kind still comes from the extension. Writers keep the other bits |
| 20 | u32 | CRC-32 | of the bytes from 128 to the end of the file. The **header is not covered** |
| 24 | 48 | title | UTF-8, up to 47 bytes + NUL; shown by the menu |
| 72 | 32 | author | up to 31 bytes + NUL |
| 104 | 16 | built from | 8.3 name of the project a game was built from, in lowercase (`project.h`; compared in any case, older games have capitals); `bm.h` still says "reserved" |
| 120 | 8 | compatibility block | Valid only when flag bit 4 is set. All zero in an old file. See the next section |

The title and author are also the key of the saves (`/bm/save/<CRC32(title, author)>.SAV`).

## Compatibility block (bytes 120..127)

Flag bit 4 (`BM_FLAG_COMPAT`, value 16) says the block is present. The header is not covered by the CRC.

| Byte | Field | Values |
|---|---|---|
| 120 | targets | bit 0 Pi Zero W, bit 1 RGB30 (`BM_TARGET_*`). Zero with bit 4 set is invalid and reads as an old file |
| 121 | input | bit 0 pad, 1 keyboard, 2 text, 3 mouse, 4 mouse optional (`BM_IN_*`). Zero = not declared |
| 122 | aspect | bit 0 wide, 1 square, 2 any, 3 uses `safe_area()` (`BM_ASPECT_*`). Zero = not declared |
| 123 | needs | bit 0 3D, 1 audio, 2 network, 3 mouse device (`BM_NEED_*`) |
| 124-125 | min RAM | u16, MB. Zero = none |
| 126 | min spec | the `BM_SPEC_REV` the cartridge needs. Zero = none |
| 127 | min width | minimum screen width in units of 16 pixels. Zero = none |

Reading (`bm_compat_read`, and `bm_cart_t.compat` after `bm_parse`): a file with bit 4 clear is an old file.
An old `.bm` reads as targets = Pi. An old `.b16` (flag bit 1) reads as targets = Pi + RGB30. `declared` is 0 for both.
The menus (Pi, RGB30) act on the block: `bm_compat_classify` applies `docs/spec/compat-policy.toml` and greys a cartridge that does not list the kernel, with a reason and a question before the start. The Market list uses the file name (it has no header yet). The load check in `runtime.c` does not read the block yet.

Writing: `mkbm.py --targets pi,rgb30 [--input ..] [--aspect ..] [--needs ..] [--min-ram MB] [--min-spec N] [--min-width PX]`
(or the INFO keys `targets`, `input`, `aspect`, `needs`, `min_ram`, `min_spec` and `min_width` of `bm.info`, with the same syntax; a flag wins),
and `bm_compat_write`. The SDK build of a `.bme` (`bm_build`, `src/bm/project.c`) reads the same INFO keys (`bm_compat_from_info`, `src/bm/format.c`) and writes the block of the `.bm`; a project whose INFO has no `targets` keeps the bytes it has. Every writer keeps the block: `bm_rewrite_with` copies it from the old file, `project.c` clears only
bytes 104..119 (`bm_set_built_from`), `mkbm.py --from` clears only bytes 104..119, `bmres.py` and `bmmesh.py` copy the header.

## File size

The kernel has no cartridge size limit of its own. It reads the whole file into RAM and checks the CRC in 64 KB pieces.
The hard ceiling is the Market: 100 MB a file (`CATALOG_MAX_FILE`, `src/net/catalog.h`), which GitHub also enforces.
A cartridge of 100 MB must fit in the heap of the device. Load time on the Pi Zero W is `not verified`.
The old 8 MB check of the `.b16` is removed from `mkbm.py`, `project.c`, the Makefile and `mkmarket.py`.
The sound bank holds at most 8,388,608 PCM values (`AU_PCM_MAX`, 16 MB decoded).

## Section table and bodies

At offset 128: `count` entries of 16 bytes: `u32 type, u32 offset (from the start of the file), u32 size, u32 reserved`
(always written 0, never read). Bodies are padded with zeros to a multiple of 4. The parser requires only
`count*16 <= length` and `offset+size <= length`: **no** alignment, order, overlap or "after the table" rule
(`COVER` should be first so the menu can read it without the CRC: `bm_cover_peek`). Unknown section types are
ignored. If a type is repeated, the **last** one wins in the kernel (the Python tools refuse duplicates).

## Sections

| # | Name | Body | Checked by the kernel |
|---|---|---|---|
| 1 | LUA | UTF-8 source | required for a cartridge; any size |
| 2 | SHEET | u16 w, u16 h, RGBA8888 | w, h 1..4096; size exact |
| 3 | MAP | u16 w, u16 h, w*h u16 cells | w, h nonzero; size exact (no upper bound) |
| 4 | COVER | u16 w, u16 h, RGBA | w, h 1..512; `mkbm.py` writes 88x88 |
| 5 | SHEET8 | u16 w, h, ncol (1..256), u16 reserved, palette (RGBA), RLE indices | decoded to the exact end. Not together with SHEET ("two sheets") |
| 6 | AUDIO | the sound bank `BMAU` (`player.h`) | starts with `BMAU` and size >= 16; the inside is not checked here. Without `BMAU` it is read as a legacy MESH |
| 7 | (old ANIM) | read as ANIM | |
| 8 | MESH | u16 models (1..256), u16 inset, u32 reserved; per model a 24-byte header, vertices (3 f32), faces (24 bytes) | vertices and faces 1..65535; first byte of the name nonzero; finite floats below 1e6; indices valid; size exact. Names are **not** checked to be unique |
| 9 | ANIM | u16 rigs, then rigs | bones 1..64, clips 0..255, keys 1..1024, times non-decreasing and within the length, parents before children; clip mode 0..4 (0 linear, 1 smooth, 2 step, 3 ease-in `u*u`, 4 ease-out `u*(2-u)`); size exact. The rig is **not** checked against its model. A kernel from before modes 3 and 4 refuses the whole ANIM section ("bad skeletons (ANIM)", mode > 2), so the cartridge does not load there |
| 10 | INFO | UTF-8 text `key: value` lines, then `[type name]` blocks | at most 16 KB (an oversized one is silently dropped in a cartridge, an error in a resource file). Keys: `name desc author license` (SPDX) `version tags origin` (SHA-256) `date`; unknown keys are kept. UTF-8 is not validated |
| 11 | SPRITES | u16 zones (1..1024), u16 reserved, 28-byte zones | name unique and nonempty; w, h >= 1; frames 1..16; every zone inside the sheet. The u16 at byte 26 of a zone (zero before) is `slices`, its low byte: 0 for a plain zone, 2..255 for a stack zone (sprite stacking: `slices` boxes of w x h to the right, `frames` 1); the zone must fit `max(frames, slices)` boxes. Bit 0 of the high byte (byte 27) is `vertical`: the slices lie one under the other (`stackv`, the zone must fit `w x frames` by `h x slices`); it counts only with `slices` 2 or more, and the other bits stay zero. The field was reserved as zero, so the format version stays 1 and an older reader sees a plain zone. A broken one is dropped, not fatal, in a cartridge |
| 12 | LAYERS | u16 w, h, nlayers (2..8, MAP counted), u16 reserved, names (16 each), cells of layers 2..n | needs MAP of the same size; **fatal** when wrong. Without it the map has one layer, `main` |
| 13 | FLAGS | u16 per_row, u16 rows, bytes | size exact; **fatal** when wrong; not compared with the sheet |
| 14 | BOXES | u16 n (1..4096), u16 reserved, 28-byte boxes | each box names a zone of SPRITES and a valid frame. A broken one is dropped, not fatal, in a cartridge |

Tile flags of the standard library `bmlib`: 0 solid, 1 platform, 2 ladder, 3 water, 4 hurts.

Resource files (`BMRES`): the kind says which sections may be present (`.bmm` MESH ANIM sheets; `.bmi` sheet SPRITES BOXES
FLAGS; `.bms` AUDIO; `.bmt` MAP LAYERS FLAGS sheet; `.bmc` a SHEET8 of N x 1 pixels; `.bmk` any mix); a LUA section is
refused. See [RISORSE.md](RISORSE.md).

## Compatibility rules, as they work today

- **Old kernel, new file.** An unknown section type is ignored. A known section whose content breaks an exact-size or a
  limit check makes the whole cartridge fail (example: more than 4096 vertices before the limit was raised). `INFO`,
  `SPRITES` and `BOXES` are the lenient ones. There is no minor version: version 1 only.
- **New kernel, old file.** The old magic, the legacy section types 6 and 7 and any cover size up to 512x512 are accepted;
  a missing `LAYERS` means one layer.
- **Who writes the format**: one C parser and one C writer in `format.c`; three more C writers in `runtime.c`
  (`cart_save`, `new_pack`, `cart_put_audio`); `project.c` patches headers in place (valid because the CRC does not
  cover the header); `mkbm.py`, `bmres.py` and `bmmesh.py` in Python. A change to the header must be made in all of
  them.

## Limits found only in the code

Sheet at most 4096 per side; cover at most 512; the map and the flags have no upper bound but the size match;
`bm_rewrite_with` keeps at most 32 old sections and 8 new ones; `cart_save` keeps at most 16 extra sections.

## Not verified

Direct pointer casts of section data in the runtime or the GPU code (alignment); the inside of the sound bank;
whether a shipped cartridge has duplicate sections; the Lua side of the `.b16` flag.
