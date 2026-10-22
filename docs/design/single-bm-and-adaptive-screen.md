# One cartridge type, input profile, adaptive screen, compatibility header

Status: design study, 2026-10-22. No code changes. Nothing here is built.
Author's note: this document is the answer to four requests from the user. Decisions of the user are marked **Decided**. Everything else is a proposal.

The four topics:

1. Remove the `.b16` edition. Keep one cartridge type, `.bm`. First step: remove the 8 MiB limit and review the other format limits.
2. A `.bm` declares its input profile (pad, keyboard and mouse, or both). The kernel adapts.
3. One game or app must work on the Pi (640x360, 16:9) and on the RGB30 (720x720, 1:1).
4. A compatibility header in the `.bm`. An app made for one kernel must not run on a kernel that cannot run it.

Related: [B16 sheet](../progress/b16.md), [BM-FORMAT.md](../BM-FORMAT.md), [B16.md](../B16.md), [ui-reference.md](../spec/ui-reference.md), [input API](../api/input.md), games input standard (`docs/design/games-input-standard.md` on branch `claude/games-input-standard-pr`, PR 86, not yet in `bm-core`).

---

## 0. Rule that must change

`CLAUDE.md` section 2 says: "A `.bm` and a `.b16` are different editions. Each edition has its own Market channel and its own build." This rule is wrong for the target. The rule must change to: "There is one cartridge type, `.bm`. A cartridge declares the kernels it runs on in its header." The same rule appears in `docs/progress/b16.md` (Rules: "A `.b16` never overwrites ... the `.bm`") and in `src/bm/project.h:2,53`. Do this change in the stage that removes the second edition (stage 6 below), not before. This PR does not edit `CLAUDE.md`.

---

## 1. Facts: what `.b16` is today

| Fact | Where |
|---|---|
| A `.b16` is the `.bm` container with flag bit 1 set. "Nothing reads" the flag. The kernel tells the kind by the file extension. | `src/bm/bm.h:14-16`, `docs/B16.md:8` |
| The flag is written by `bm_build_b16`, `mkbm.py --b16`, `Makefile` targets. | `src/bm/project.c:219`, `scripts/mkbm.py:609`, `Makefile:348-371` |
| `bm_build_b16` forces a non-square cartridge to 360x360. | `src/bm/project.c:220-224` |
| The runtime applies no `.b16` profile. "its profile's limits are not applied yet". | `src/rgb30/ui.c:426-427`, `docs/progress/b16.md:46` |
| The Pi lists `.bm` and `.b16`. The RGB30 lists `.b16` by default and `.bm` only with `show_bm`. | `src/kernel/carts.c:154-157`, `src/rgb30/ui.c:245-261` |
| The Market tells the edition by the path extension. The RGB30 filter keeps `.b16` unless `market_bm=1`. The Pi keeps the `.bm` when a game has both, unless `show_b16=1`. | `src/kernel/market.c:233-241`, `:352-381`, `src/net/catalog.c:136-166` |
| `mkmarket.py` allows at most one `.bm` and one `.b16` per game. It prints the badges ".b16 RGB30" and ".bm Pi". | `scripts/mkmarket.py:206-207`, `:273-274` |
| Carts with both editions today: Overbit (`.bm` and `.b16`). `.b16` only: Yharnam, Titan Clash EX, `sdk_b16`. | `docs/B16.md`, `Makefile:348-371`, `carts/titan_ex/titan_ex.b16` |
| The header has 24 reserved bytes (104..127) after the 16-byte "built from" field. Flags bits 4..15 are free and "kept as they are by every writer". | `src/bm/bm.h:14-23` |
| The header is not covered by the CRC. `project.c` patches it in place. | `docs/BM-FORMAT.md:30` |
| `bm_build_b16` and the `.bm` build clear bytes 104..127 with `memset`. Any new header field there is lost unless this code changes. | `src/bm/project.c:143,178,225` (the `memset(data + BM_BUILT_FROM, 0, ...)`) |
| A cartridge already says what it needs with the INFO key `spec: N`. The kernel refuses a newer one with a message. | `src/bm/runtime.c:9382-9390`, `src/bm/bm.h:138-145` (`BM_SPEC_REV 3`) |
| The menu reads only the first 48 KiB of a file (header, table, cover). | `src/kernel/carts.c:204`, `src/rgb30/ui.c:206` |

Conclusion: `.b16` is a name, a flag nobody reads, an 8 MiB build check and a 360x360 size. It is not a runtime profile. Removing it is cheap in the kernel. Most of the work is in tools, Market and docs.

---

## 2. Topic 1: limits (current -> proposed)

### 2.1 Where the 8 MiB limit lives

The kernel has **no** 8 MiB limit when it loads a cartridge. The limit exists only when a `.b16` is built, published or described.

| Place | What | Action |
|---|---|---|
| `src/bm/project.h:17` `BM_B16_MAX (8u << 20)` | constant | delete with the `.b16` build |
| `src/bm/project.c:203-205` | `bm_build_b16` refuses a file over 8 MiB | delete with the `.b16` build |
| `scripts/mkbm.py:700-701` | `--b16` size check | delete the check |
| `Makefile:363`, `:371` | `test ... -le 8388608` for `overbit.b16` and `%.b16` | delete the two lines |
| `scripts/mkmarket.py:67`, `:121-126`, `:226` | `MAX_B16`, `check_b16` | replace with the catalog limit (100 MiB) |
| `src/kernel/carts.c:926`, `src/rgb30/ui.c:825`, `src/kernel/market.c:1008` | text "up to 8 MiB", "8 MiB ceiling" | change text |
| `src/ai/kb/guide_sdk.txt:881,897,991,996,1001,1006`, `docs/API.md:176`, `docs/B16.md:10,57,418-422,445,621,707,713,728`, `docs/progress/b16.md:17,31,38,48` | docs and assistant knowledge base | change text. The assistant weights (`src/ai/assist.weights`) must be rebuilt after a change of `src/ai/kb/*`. |
| `src/net/catalog.h:19` `CATALOG_MAX_FILE (100u << 20)` | Market file limit. GitHub refuses files over 100 MiB. | keep. It is the real ceiling for the Market. |
| `src/kernel/lib.c:13` `LIB_FILE_MAX (8u*1024*1024)` | limit of a Lua library file on the SD card. Not a cartridge limit. | keep, out of scope |
| `src/kernel/monitor.c:152` | 8 MiB download buffer of the monitor. Not a cartridge limit. | keep, out of scope |

Search command used: `grep -rn '8 MiB\|8u << 20\|8388608\|BM_B16_MAX' src scripts Makefile docs`. The pattern `8 \*` in the request matches thousands of unrelated lines, so it was not used.

**Consequence of removing it on the Pi Zero W.** The kernel loads the whole file into RAM (`carts_play_buffer(fb, data, len)`, `src/kernel/carts.c:559`). It computes CRC-32 over the whole file in 64 KiB pieces (`src/bm/format.c:541-548`). Then it decodes sheets to RGB565. The Pi Zero W has 512 MB RAM. The GPU split and the real free heap are not measured in this study (`not verified`). A 100 MiB cartridge would still fit in RAM, but would use a large part of the heap and take seconds to read from the SD card and to check. CRC time on the ARM1176 is not measured (`not verified`).

Recommendation: no hard kernel limit. Add a **soft check** in the SDK and `mkbm.py`: a warning above 32 MiB ("slow to load on the Pi Zero W"), an error above 100 MiB (the Market ceiling). The kernel already reports "out of memory" if the load fails.

### 2.2 Other limits of the format and the runtime

All values found in the code. "Keep" means: no known game needs more, the cost of raising is real.

| Limit | Current | Where | Proposed | Pi Zero W / RGB30 consequence |
|---|---|---|---|---|
| Cartridge file size | 8 MiB (`.b16` only) | see 2.1 | none in kernel; SDK warn 32 MiB; Market 100 MiB | RAM + CRC time, see 2.1 |
| Sheet width and height | 4096 each | `bm.h:180`, `format.c:626,634` | keep 4096 | 4096x4096 is 64 MiB as RGBA and 32 MiB as RGB565 after decode. Larger would hurt the Pi. |
| SHEET8 colours | 1..256 | `format.c:634`, `BM-FORMAT.md:55` | keep. Use the true-colour `SHEET` (RGBA, no palette) when a game needs more. | The output is RGB565 (65536 colours). No 32-bit colour for now (decision 2026-10-21). More palette entries gain nothing on screen. |
| Palette resource `.bmc` | a SHEET8 of N x 1, N <= 256 | `BM-FORMAT.md` resource kinds | keep | same |
| COVER | up to 512x512; `mkbm.py` writes 88x88 | `BM-FORMAT.md:56` | keep | the menu reads it in the first 48 KiB |
| MAP | no upper bound, u16 cells | `BM-FORMAT.md:55` | keep | cells: 2 bytes each |
| LAYERS | 8 (MAP counted) | `bm.h:168` | keep | each layer is a full map |
| SPRITES zones | 1024, frames 1..16, name 16 bytes | `bm.h:171-173` | keep | |
| BOXES | 4096 | `bm.h:174` | keep | |
| MESH | 256 models, 65535 vertices and faces per model, name 16 bytes | `bm.h:188`, `BM-FORMAT.md` | keep (the vertex and face counts are the u16 format limit) | the Pi draws far fewer; see the 3D bench |
| ANIM | 64 bones, 255 clips, 1024 keys | `bm.h:190-192` | keep | |
| INFO | 16 KiB; an oversized one is silently dropped | `bm.h:170` | raise to 64 KiB **only if** the compatibility data goes in INFO (see 5.3). With the binary block it stays. | text, parsed at load |
| AUDIO samples (PCM) | 1,048,576 16-bit values per bank (2 MiB): 22 s mono at 48 kHz | `src/audio/player.h:81` | raise to 8,388,608 values (16 MiB decoded, about 3 min mono at 44.1 kHz) | The samples are converted to 16-bit when the bank is read, so RAM = 2 bytes per value. 16 MiB is small against 512 MB. Load time grows with the size. |
| AUDIO sample slots | 64 | `player.h:82` (`AU_SAMPLES`) | keep | |
| AUDIO sounds / sfx / patterns / songs | 32 / 64 / 64 / 8 | `player.h:86-89` | keep | |
| Song length, pattern steps, sfx steps | 64 / 64 / 32 | `player.h:92-94` | keep | |
| Voices | 8 | `src/audio/synth.h:34` | keep | the mixing cost on the ARM1176 scales with voices |
| Long music (above the PCM cap) | no stream | | later stage: a stream read from the SD card in pieces | needs a reader fiber; not in stage 1 |
| Sections per file | 255 (u8) | `BM-FORMAT.md:28` | keep | |
| `bm_rewrite_with` / `cart_save` extra sections | 32 old + 8 new / 16 extra | `BM-FORMAT.md:86`, `runtime.c:5392` (`PROJ_EXTRA_MAX 16`) | raise when a new section type (section 15, see 5) needs it. Not before. | |
| Title / author / name | 47 / 31 bytes; names 16 bytes | `BM-FORMAT.md:31-32` | keep | |
| Header sizes | width x height from a list: 640x360, 480x270, 320x180, 256x256, 360x360, 720x720 | `bm.h:9-11` | add 960x540 and 1280x720 only if a cart needs a fixed size. `screen()` already offers them at run time. | |
| Lua heap | 64 MiB | `src/script/luavm.c:13` | keep. `docs/progress/b16.md:16` says 4 MiB; that line is out of date (decision D6 keeps 64 MiB). Fix the line in stage 1. | |
| Save | 32 KiB (`SAVE_MAX`) | `runtime.c:3383` | keep. `docs/progress/b16.md:18` says 64 KiB; out of date. | |
| Document read | 4 MiB (`DOC_MAX`) | `runtime.c:3714` | keep | not a cartridge limit |
| Tokens | none | `src/bm/tokens.h:9` | keep | |
| Market catalog | 256 games, 256 KiB index, 100 MiB per file | `catalog.h:15-19` | keep. Check the index size after compatibility lines are added (5.5). | RAM of the catalog on the Pi |

Summary: the only limit that must go is the 8 MiB `.b16` check. The audio PCM cap is the only format limit worth raising now. The palette needs no change. **Proposed, not decided.**

---

## 3. Topic 2: input profile

### 3.1 Facts: how a cartridge sees input today

| Mechanism | What it gives | Where |
|---|---|---|
| `btn(i)`, `btnp(i)`, with a name (`"a"`, `"ok"`, `"back"`), optional player `p` | 10 logical buttons. A keyboard, a USB pad and a Bluetooth pad all feed it. | `docs/api/input.md:8-9, 60-74` |
| `keymap(t)` | game actions on buttons; `btn("jump")` | `docs/api/input.md:10` |
| `controller([p])` | kind (`keyboard`, `ds4`, `xbox`, `pad`, `builtin`, `none`), layout, `ok` and `back` | `docs/api/input.md:11` |
| `stick([p, n])` | analog sticks; the keyboard gives the cross | `docs/api/input.md:14` |
| `mouse(on)`, `mouse()`, `mousep()` | the pointer, only if the cartridge asks. A controller never shows it. `mouse(true)` returns `false` if the console has the mouse off. | `docs/api/input.md:18-35` |
| `rawkeys`, `keydown`, `keys`, `pad`, `lastinput` | raw keys and bits; `lastinput()` for button prompts | `docs/api/input.md:90-96` |
| `prompt("act1", x, y)` | draws the right symbol for the active device | `docs/api/input.md` (PR 86 text) |
| `lib.input({scheme=...})` (PR 86, `claude/games-input-standard-pr`) | schemes `scroller`, `topdown`, `stick`; actions `act1..act8`; menus `confirm="ok"`, `cancel="back"`, `pause="start"` | `docs/design/games-input-standard.md` on that branch; `src/script/bmlib.lua:1093` on that branch |
| `controls(scheme)` (PR 87, `claude/controls-remap-pr`) | the player's remap in Settings > Controls replaces the game's buttons for that scheme | `docs/api/input.md:12` on that branch |
| `bmui` | a mouse layer for the editors: "the mouse does what the keys do" | `docs/api/input.md:37-57` |

Device facts: the RGB30 has a pad only. The USB port is not driven. A Bluetooth pad, keyboard or mouse can pair (`src/rgb30/bm_port.c:185`, `src/rgb30/main.c:271`). The RGB30 pointer block is suspended (`docs/spec/divergences.toml:65-67`) and the on-screen keyboard is "in work" (`:41`). The Pi has a USB or Bluetooth keyboard, mouse and pad. Menu confirm is `pad_ok` on both (`docs/spec/ui-reference.md:22,41`).

How games are optimised today: a game is written for the pad (button names, `prompt()`), and the keyboard is mapped onto the pad. The editors (Code, Pixel, Studio, Animator, Sound, SDK) are written for the keyboard and the mouse (`bmui`). They do not work well on a pad alone. Nothing in the cartridge tells the kernel which case it is.

### 3.2 What a cartridge should declare

A 1-byte **input profile** in the compatibility block (section 5). Bits:

| Bit | Meaning |
|---|---|
| 0 | `pad`: playable with a pad alone |
| 1 | `keyboard`: playable with a keyboard (the keyboard acts as a pad by default) |
| 2 | `text`: needs real text entry (a keyboard, or the on-screen keyboard) |
| 3 | `mouse`: uses `mouse()` as the main control |
| 4 | `mouse_optional`: uses the mouse but works without it |

Typical values: a game = `pad|keyboard` (1+2). Pixel or Code = `keyboard|text|mouse` (2+4+8). A tablet-style app = `pad|mouse_optional`.

### 3.3 How the kernel adapts

| Case | Option 1 (recommended) | Option 2 |
|---|---|---|
| Device has no matching input (for example, the RGB30 with a `text` + `mouse` app) | Show the cart greyed out with the reason "needs a keyboard and a mouse" (**Decided**: grey, not hidden). Let the user start it anyway if a Bluetooth keyboard is paired. | Start it and give it the adapters below |
| `mouse` app on a pad | **Pad cursor**: `mouse(true)` is answered by the kernel with the right stick as pointer and B as left click, Y as right click. Cost: one poll per frame, no draw cost. Reuse the suspended RGB30 pointer block (`divergences.toml:65`). | Do nothing. The cart gets `false` from `mouse(true)` and must handle it. |
| `text` app on a pad | **On-screen keyboard** drawn by the kernel over the frame. It feeds `keys()` and `typed text`. Cost: a small overlay on the ARM; it is already planned (`divergences.toml:41`). | The cart draws its own. Too much work for each cart. |
| Keyboard on a game | Already works: the keyboard is a controller for `btn()`. PR 86 adds per-scheme key maps. | |
| Hints | `prompt()` already follows `lastinput()`. A cart with the profile gets the hint text from the kernel. No change. | |
| Remap | PR 87 `controls()`. The profile decides which schemes show in Settings. | |

Cost on the Pi Zero W: all adapters are small. The pad cursor and the on-screen keyboard run only when the profile asks for them. **Recommendation: opt 1.** Order of work: profile field and grey-out first (small), pad cursor second, on-screen keyboard third (the RGB30 keyboard is already planned).

---

## 4. Topic 3: aspect ratio

### 4.1 Facts

| Fact | Where |
|---|---|
| The Pi frame is 640x360 (16:9) for the menu and for games by default. | `docs/spec/ui-reference.md:29-30` |
| The RGB30 panel is 720x720. The menu is 360x360 shown x2. Games: 360x360 x2 or 720x720. | `ui-reference.md:9-11` |
| On the Pi a square cartridge (256x256, 360x360, 720x720) is shown in the middle of a 16:9 frame with **black bars** (`boxed`). On a square panel a page is never boxed. | `src/bm/runtime.c:7205-7215`, `:3076-3081` |
| `screen(w, h)` accepts 7 sizes, all 16:9 (320x180 ... 1920x1080) on the Pi. On the RGB30 it accepts only 360x360 and 720x720. `SCREEN_W` and `SCREEN_H` follow. `screen()` returns the size now; `screen(i)` lists the modes. | `docs/api/draw.md:11`, `runtime.c:3076-3100` |
| So a 16:9 cartridge cannot run at 16:9 on the RGB30. It would get a square screen. | same |
| Carts read `SCREEN_W`/`SCREEN_H` (about 52 uses in 27 carts). Many fix a layout at load: `local W, H = SCREEN_W, SCREEN_H`, `VIEW_W = 624`. The suites differ: Pixel uses the width, `sdk_b16` uses a layout helper for 360x360. | `grep -rn SCREEN_W carts`, `carts/pixel/main.lua:13`, `carts/code/main.lua:1098`, `carts/sdk_b16/layout.lua:1-12` |
| `bmlib` has a camera `Cam` with an optional view size and `Cam:screen()`. `lib.split` and `lib.party` take `SCREEN_W/H`. | `src/script/bmlib.lua:821, 897, 992, 1222` |
| `carts/sdk_b16/layout.lua` is a small layout helper (rectangles, split with pixels, rows, shares). It is local to that cart. | `carts/sdk_b16/layout.lua:1-12` |
| Overbit builds its own screen list from `screen(i)`. | `carts/overbit/src/85_quality.lua:17, 102` |

Fill cost (pixels per frame, RGB565 = 2 bytes each): 640x360 = 230,400 px (461 KB); 360x360 = 129,600 px (259 KB); 720x720 = 518,400 px (1.04 MB). A game that draws 640x360 instead of 360x360 draws 78% more pixels. On the Pi Zero W this is the main cost. The 3D games use the GPU, so the cost there differs (`not verified`).

### 4.2 Games (fixed design, for example Yharnam)

| Option | Idea | Pi cost | RGB30 cost | Result |
|---|---|---|---|---|
| G1 (recommended) **Bars** | Keep the designed ratio. Black bars on the other device. Square game on the Pi: already done today. A 16:9 game on the RGB30: 16:9 area of 720x405 inside 720x720 (the area is 56% of the panel). | none (as today) | none | Simple. A 16:9 game looks small on the RGB30. |
| G2 **Extend the view** | The cart draws more world at the sides (16:9) or above and below (1:1). Needs the safe area (4.4): the part that must show everywhere. | +78% pixels for a square game moved to 16:9 | n/a | Best look. Each game needs work. Gameplay can change (the player sees more). |
| G3 **Two layouts** | The cart has two layouts and picks by `SCREEN_W/H`. | none | none | Full control. Double the work. |

Recommendation for games: **G1 as the default for every existing game** (zero work), **G2 as an option for games that opt in** through the safe area API. A game declares in the compatibility block the ratios it supports (`aspect` field, 5.2). **Proposed.**

### 4.3 Apps and suites (Pixel, Code, Animator, Sound, Studio, SDK)

| Option | Idea | Pi cost | Result |
|---|---|---|---|
| A1 **Bars** | Run at the designed size with bars. | none | The 640x360 suites on the RGB30: bars, content at 56% size. Text becomes too small on 720x720. |
| A2 (recommended) **Elastic layout** | The app builds its layout from `SCREEN_W/H` with a layout helper (anchors: left, right, fill, fixed width). On a change of `screen()` the app calls the layout again. | one layout pass at start and on resize; no per-frame cost | The app fills the screen on both devices. Work: each suite moves its fixed numbers into the layout. |
| A3 **Per-device layout** | Two layout files per app. | none | Highest quality. High maintenance. |

Recommendation: **A2**. Promote `carts/sdk_b16/layout.lua` into `bmlib` (`lib.layout`). It is already host-tested (`tests/studio/sdk_b16_host.lua`).

### 4.4 Proposed API (new, not in the code)

| Call | Returns | Notes |
|---|---|---|
| `safe_area()` | `x, y, w, h`: the part of the screen that shows on every supported device. For a game made for 360x360 it is the central 360x360. | Costs nothing. Built from `SCREEN_W/H` and the cartridge header. |
| `aspect()` | `w/h` as a number, and `"wide"`, `"square"` | A convenience over `SCREEN_W/H`. |
| `on_resize(fn)` (bmlib) | calls `fn(w, h)` after `screen()` changes | Lets an app re-layout. |
| `lib.layout` (bmlib) | `rect`, `split`, anchors `left/right/center/fill`, from `sdk_b16/layout.lua` | Pure Lua. No kernel change. |
| `screen()` list on the RGB30 | add a 16:9 letterboxed mode 720x405 | Lets a wide cart run on the RGB30 with bars made by the kernel, not by the cart. Cost: the display scaler already centres (`bm_scale`). |

Order: `lib.layout` (Lua only), then `safe_area()`/`aspect()`, then the RGB30 wide mode. **Proposed.**

---

## 5. Topic 4: compatibility header

### 5.1 Decided by the user

1. An incompatible cartridge on the device is shown **greyed out with a clear reason**. It is not hidden. This applies to the local menu (Games, Apps).
2. A cartridge **without the new field** is treated as **Pi only** ("old apps are for the Pi").
3. The Market of a kernel lists **only compatible** items (flagged in 5.5 as an open point).

### 5.2 Fields and where they fit

Candidates for the place:

| Place | For | Against |
|---|---|---|
| H. Header bytes 120..127 (reserved today) | The menu scan reads only the first 48 KiB; header is always there. No parse. Old kernels ignore it. | Only 8 bytes. Not in the CRC. `project.c:143,178,225` clears it. |
| I. INFO keys (`targets: pi,rgb30`) | Already used for `spec`. Text, easy for tools. | In the section table; the menu needs a parse of text. INFO over 16 KiB is dropped silently. |
| S. New section 15 `NEEDS` | Room to grow, covered by the CRC. Old kernels ignore unknown types. | The menu scan must find it in the first 48 KiB (the table is there, the body may not be). |

**Proposal (opt 1): H + flag.** Use flags bit 4 = "compatibility block present". Put an 8-byte block in the reserved bytes 120..127:

| Byte | Field | Values |
|---|---|---|
| 120 | `targets` | bit 0 Pi Zero W, bit 1 RGB30, bits 2..7 later kernels. 0 with the flag set is invalid. |
| 121 | `input` | profile bits of 3.2 |
| 122 | `aspect` | bit 0 wide (16:9), bit 1 square (1:1), bit 2 any (elastic layout), bit 3 uses `safe_area()` |
| 123 | `needs` | bit 0 3D (V3D on the Pi; Mali on the RGB30), bit 1 audio, bit 2 network, bit 3 mouse hardware |
| 124-125 | `min_ram` | MiB, u16 (0 = none) |
| 126 | `min_spec` | the `BM_SPEC_REV` needed (like INFO `spec`, in the header so the menu can grey it without a parse) |
| 127 | `min_w16` | minimum screen width in units of 16 pixels (0 = none) |

Opt 2: the same fields in a section 15 `NEEDS`, plus a 2-byte `targets` in the header for the scan. More room, two places to keep equal.
Opt 3: INFO keys only. Smallest change, weakest check.

`audio` need: the RGB30 and the Pi both have sound; the bit is for a later kernel without audio. `3D`: both kernels have a GPU path (Pi V3D, RGB30 Mali); a later kernel may not.

### 5.3 How the kernel checks

| When | Check | Result for the user |
|---|---|---|
| Menu scan (`carts.c:113 read_header`, `rgb30/ui.c:206`) | read bytes 120..127 from the first read. No extra I/O. | Cover shown **greyed**. Options page shows the reason line. The play action is refused with the same text. |
| Load (`runtime.c:9382`, next to the `spec` check) | check again (a file can be copied or patched) | Message: `This game is for the RGB30 only.`, `This game needs a keyboard.`, `This game needs bm spec 4, this kernel has 3: update the kernel.` |
| Build (`mkbm.py`, SDK, `project.c`) | the tool writes the block; `lint` checks that the declaration fits the code (for example `mouse()` used without the `mouse` bit; 3D calls without `needs` bit 0) | warning, not an error |
| Market (`mkmarket.py`) | reads the block from the cart and writes the catalog line (5.5). The block and the catalog cannot differ because the tool derives one from the other. | |

Kernel id: each kernel has a constant (`BM_TARGET_PI = 1`, `BM_TARGET_RGB30 = 2`). A cart runs if `targets & kernel_id`. A cart with flag bit 4 clear (old) counts as `targets = Pi` (**Decided**).

### 5.4 Replacing the `.b16`/`.bm` split

One cartridge, `targets` lists the kernels it supports. A game that needs a different payload per kernel (Overbit: today a `.bm` and a `.b16`) has two ways:

| Way | Idea | Size cost | Notes |
|---|---|---|---|
| T1 (recommended) **One payload, adaptive** | One Lua and one set of assets. The code reads `SCREEN_W/H`, `controller()`, `stat()`. | none | Matches the `.b16` today: the runtime is the same. |
| T2 **Per-target sections** | Two sets of sections (for example a 720x720 sheet and a 360x360 sheet) in one file; the kernel picks by target. | the sum of both sets | Needs a loader change (section selection by target). Only for the few games with big art differences. Not in the first stages. |

Migration of old carts:

| Old file | New meaning |
|---|---|
| `.bm`, no compatibility block | Pi only (**Decided**) |
| `.b16` (flag bit 1 set) | targets = Pi + RGB30. Reason: the Pi plays `.b16` today (`carts.c:154-157`), and the RGB30 plays it by default. This is a proposal that goes against a literal reading of decision 2 ("old = Pi"). See open question 3. |
| Overbit `.bm` + `.b16` | one `overbit.bm` with targets Pi + RGB30 when T1 works; else keep two files until T2 |
| Yharnam, Titan Clash EX, `sdk_b16` (`.b16` only) | rebuilt as `.bm` with targets Pi + RGB30 |

On the RGB30 the old `.bm` files now show greyed (Pi only), so `show_bm` and `market_bm` become unneeded. The RGB30 may still play a greyed `.bm` after a confirm ("not made for this device") if the user wants it. **Open question 4.**

### 5.5 Market

Today (facts): the catalog `index.txt` is signed once with ECDSA P-256 over SHA-256 (`src/net/catalog.c:55-67`). Each entry lists `file path size sha256` and a cover (`catalog.c:253-256`). Unknown lines are ignored ("newer catalogs", `catalog.c:~262`). The filter is in the client, by the file extension (`market.c:352-381`). The site is static GitHub Pages, so there is no server-side filter.

Proposal:

| Item | Design |
|---|---|
| New catalog line | `targets pi,rgb30` after `file`. Old kernels ignore it and list everything (they then see the load-time message). |
| Filter | **Client side** (opt 1). `adopt()` in `market.c` keeps entries where `targets & kernel_id`. An entry with no `targets` line counts as Pi only (**Decided**). One signed index for all kernels. |
| Alternative (opt 2) | `mkmarket.py` writes one signed index per kernel (`index-pi.txt`, `index-rgb30.txt`). Smaller index on each device (the index limit is 256 KiB, `catalog.h:16`). More files and keys to publish. |
| Signature and SHA-256 | Unchanged in form. The signature covers `index.txt`, which now includes the `targets` line. The SHA-256 covers the cart bytes. If the build tool writes the block into the cart, the SHA is computed after that. The tool always derives the catalog line from the cart, so they match. |
| One cart, many targets | One entry, one `file` line, one card. The badge shows the device list ("Pi, RGB30"). The two-edition rule in `mkmarket.py:206-207` and `catalog.c:166` (same id and same kind) is deleted. |
| `ids` | `owned_find(id, is_b16)` in `market.c:247, 302, 607, 1045` uses the edition as part of the key. After the change the key is the id only. Installed records need a migration (7). |
| Conflict with "grey" | The local menu greys. The Market hides incompatible items (**as decided**). Possible conflict: a user cannot discover on the Market what exists for other devices, and cannot download a Pi game to copy it by hand. **Open question 1.** |

### 5.6 Risks

| Risk | Mitigation |
|---|---|
| A wrong declaration hides or greys a working game | The grey state is not a lock: the user can start it after a confirm (open question 4). The lint compares the code and the declaration. A kernel-side `bm/config.txt` key `compat=off` lets the owner ignore the check. |
| A lying header (a game claims every target) | The check is a convenience, not a security gate. The cart already runs sandboxed in the Lua VM. The Market tool derives the target from the signed index and a review step. The header is not covered by the CRC (`BM-FORMAT.md:30`); the SHA-256 in the signed catalog covers it for Market files. |
| `project.c:143,178,225` clear bytes 104..127 | change the `memset` to clear only the "built from" field (104..119) in stage 3 |
| Other writers (`format.c`, three in `runtime.c`, `mkbm.py`, `bmres.py`, `bmmesh.py`) | Each must keep bytes 120..127 and flag bit 4. A host test writes a block, runs every writer, and compares. |
| Old kernel, new file | Old kernels ignore unknown flags and reserved bytes: the cart runs everywhere as today. The new guard works only on new kernels. |
| New kernel, old file | Treated as Pi only. On the RGB30 old `.bm` files show greyed. |

---

## 6. Staged plan

Each stage is one small PR. A stage can ship without the next one.

| Stage | Content | Size | Check |
|---|---|---|---|
| 1 | **Remove the 8 MiB limits** (table 2.1): delete `BM_B16_MAX`, the `project.c` check, the `mkbm.py` check, the two `Makefile` lines; `mkmarket.py` uses 100 MiB; fix the texts; SDK/`mkbm.py` soft warning at 32 MiB; fix the stale lines in `docs/progress/b16.md` (heap, save). Raise `AU_PCM_MAX` in a separate commit. | small | host tests for `project.c`, `mkbm.py` check, `consistency.py check` |
| 2 | **Format reservation**: document the 8-byte block and flag bit 4 in `BM-FORMAT.md`; add the constants to `bm.h`; spec revision 4 (`specs.toml`). No behaviour. | small | docs, `consistency.py gate` |
| 3 | **Writers keep the block**: fix the three `memset` calls in `project.c`; all writers preserve bytes 120..127; host test. `mkbm.py --targets pi,rgb30 --input pad,keyboard`. | small | host test |
| 4 | **Check and grey-out**: parse in `read_header`; grey cover and reason in the Pi menu and the RGB30 list; load-time message. Old file = Pi. | medium | QEMU test for Pi and RGB30 menu; one grey cover |
| 5 | **Market**: `targets` line in `mkmarket.py`; client filter in `adopt()`; remove the edition key from `owned_find`; badge text. | medium | catalog host test; sign and verify test |
| 6 | **Retire `.b16`**: rebuild Yharnam, Titan Clash EX, `sdk_b16`, Overbit as `.bm` with targets; accept `.b16` files for read only (rename on the SD card); delete `bm_build_b16`, "Export .b16" and the Market channel; change the rule in `CLAUDE.md` and `docs/progress/b16.md`. | medium | `make`, QEMU tests, Market sample |
| 7 | **Migration of installed records and the Market channel**: `owned` records get the id key; the old `.b16` channel stays listed for one release as a redirect to the `.bm`. | small | host test |
| 8 | **Layout**: `lib.layout` from `sdk_b16/layout.lua`; `on_resize`; `safe_area()`; `aspect()`. One suite (Pixel) as the first user. | medium | host test, QEMU screenshot of both sizes |
| 9 | **Input**: profile bits honoured; the pad cursor for `mouse` carts on the RGB30; later the on-screen keyboard. | medium | QEMU test on the RGB30 |
| 10 | **RGB30 wide mode** (720x405 letterbox in `screen()` list) and elastic suites on the RGB30. | medium | RGB30 QEMU; `not verified on hardware` |

Hardware: nothing here is verified on a Pi or an RGB30. QEMU does not prove hardware behaviour.

---

## 7. Migration of the Market channels

1. Stage 5 adds the `targets` line. The `.b16` entries stay. The client filter treats a `.b16` path as `targets = rgb30 + pi`.
2. Stage 6 publishes the `.bm` for each game that had a `.b16`, with the new block. The catalog then has one entry per game id.
3. Stage 7 keeps the old `.b16` entry for one release. Old kernels (before stage 5) still find a `.b16` and a `.bm` they know. New kernels show one card.
4. After one release the `.b16` files are removed from the Market repository (`f-accomando/bm-market`). The user must do the delete of any branch or file in that repository. This study deletes nothing.
5. Installed `.b16` files on SD cards keep working: the kernel reads them as `.bm` with targets Pi + RGB30. The user can rename them.

---

## 8. Open questions for the user

1. **Market and grey.** The local menu greys incompatible carts. The Market shows only compatible items. Is that right, or should the Market also list the others greyed with the reason ("for the RGB30 only")? The second way helps discovery; the first keeps the list short.
2. **Soft limit.** Do you want a warning in the SDK above 32 MiB, and the Market ceiling at 100 MiB? Or no warning at all?
3. **Old `.b16` files.** Decision 2 says old carts are Pi only. The existing `.b16` carts run on the RGB30 today. Should they become Pi + RGB30 (proposed), or Pi only (literal)? Literal would grey Yharnam on the RGB30 until it is rebuilt.
4. **Start a greyed cart?** Is grey a lock, or may the user start the cart after a confirm ("not made for this device")? The study proposes a confirm.
5. **Audio.** Raise `AU_PCM_MAX` to 8M values in stage 1, or keep it for a separate PR?
6. **Pad cursor.** May the kernel emulate the mouse with the right stick for carts that declare `mouse_optional`? It changes what `mouse()` returns on the RGB30 (today: no pointer).
7. **Games on a new ratio.** For existing games, is "bars" (G1) acceptable on both devices, and is "extend the view" (G2) only for new games?
8. **Aspect on the RGB30.** Add the 720x405 letterbox mode to `screen()`? It lets wide carts run, but the picture is 56% of the panel.
9. **Name.** After the change, is the extension `.bm` for all, or do apps keep their own (`.bme` projects stay)?

---

## 9. What was not verified

- Free heap, CRC time and load time of a large cartridge on the Pi Zero W. No build was run.
- Any behaviour on a Pi or an RGB30. No QEMU test was run for this study.
- Branches `claude/games-input-standard-pr` (PR 86) and `claude/controls-remap-pr` (PR 87) were read from `origin`, not merged here. Their line numbers can move.
- The V3D texture size limit that supports the 4096 sheet limit.
