# Screen resolutions — menu, console and games

Italian version: [RISOLUZIONI-IT.md](RISOLUZIONI-IT.md).

Which resolutions bm can use, how they reach the TV and what they cost.
Written to choose the menu resolution (M27, BareMetal UI).

**Sources**
- *(M)* measured on the real Pi; *(D)* firmware or code documentation.
- Measured memory bandwidth: `docs/PRESTAZIONI.md`.

---

## 1. How the image reaches the screen

- bm asks the firmware (mailbox) for a framebuffer at the resolution it wants, with 2
  or 3 pages to draw without flicker.
- The GPU scales that framebuffer up to the HDMI output mode, at no cost to the
  ARM.
- The HDMI mode is in `boot/config.txt` *(D)*:
  - `hdmi_group=1`, `hdmi_mode=16`: **1080p at 60 Hz** (1920×1080);
  - `scaling_kernel=8`: **sharp** scaling (solid pixels, no blur);
  - `disable_overscan=1`: no black border.
- With an **integer** scale (2×, 3×...) every pixel becomes a square equal to the
  others. With a non-integer one (1.5×) some pixels come out wider than others.

## 2. Resolutions used today

| What | Resolution | Colours | Pages | On 1080p |
|------|-------------|--------|--------|----------|
| Text console and monitor | 640×360 (80×22 characters 8×16) | 32 bit | 2 | 3× |
| Menu (BareMetal UI) | 640×360 | 16 bit (RGB565) | 3 | 3× |
| `.bm` games | 640×360, 480×270 (M33) or 320×180 | 16 bit (RGB565) | 3 | 3×, 4× or 6× |
| Square `.bm` games | 256×256 in the centre of 480×270 | 16 bit (RGB565) | 3 | 4× (1024×1024, black borders) |

- The resolutions of `.bm` games are fixed by the format (`src/bm/format.c`):
  "resolution must be 640x360, 480x270, 320x180 or 256x256". 480×270 (M33) is the
  natural resolution for textured 3D in software: 2.25 times the pixels of 320×180,
  a little more than half of 640×360, and an integer scale on 1080p (4×).
- 256×256 (`--res 256x256`): the firmware gives a 480×270 screen (1920×1080 / 4,
  integer pixels on 1080p) and the game draws in the square in the centre (`bm_video_enter`
  in `runtime.c`); the borders stay black. `SCREEN_W` and `SCREEN_H` are 256. The 3D of
  these games is drawn by the ARM (the GPU writes whole pages, not a square in the centre).
- Game covers in the `.bm` are 88×88 squares (`BM_COVER_SIZE`, 2026-10-04; before
  128×80: the menu fits any size).
- **Resolution change while the game runs** (`screen(w, h)`, 2026-10-04): the
  cartridge starts with the one in the header and can switch to 320×180, 384×216,
  480×270, 640×360, 960×540, 1280×720 or 1920×1080 between one frame and the next. On
  1080p they are all integer-pixel (6×, 5×, 4×, 3×, 2×, 1×) except 1280×720 (1.5×: irregular
  pixels with `scaling_kernel=8`). The triple-page framebuffer at 1920×1080 is
  12.4 MB of GPU memory (64 MiB). Above 640×360 the GPU is needed: the ARM
  rasteriser pays for every pixel. Overbit offers it in the menu (RESOLUTION) and saves it.

## 3. Possible resolutions for the menu

The menu draws directly into video memory, which **has no cache**. Every
frame it copies the whole background (the blurred cover) from RAM to the screen: that is the
item that grows with the resolution.

Estimate of the background copy with `memcpy` at ~100 MB/s *(M)*, at 16 bits per pixel (2
bytes). At 60 fps a frame lasts **16.7 ms**; the numbers below are only the copy, to
which covers, text and bar are added.

### With 1080p output (the current one)

| Resolution | Scale | Bytes per frame | Background copy | Notes |
|-------------|-------|---------------------|--------------------|------|
| 320×180 | 6× | 0.12 MB | ~1.2 ms | 128×80 covers are huge: 2 per row, a single row |
| 384×216 | 5× | 0.17 MB | ~1.7 ms | 2 covers per row |
| 480×270 | 4× | 0.26 MB | ~2.6 ms | 3 covers per row |
| **640×360** | **3×** | **0.46 MB** | **~4.6 ms** | **current**: 4 per row, like games and console |
| 960×540 | 2× | 1.04 MB | ~10.4 ms | possible but tight (see below) |
| 1280×720 | 1.5× | 1.84 MB | ~18.4 ms | non-integer scale: irregular pixels; beyond 16.7 ms |
| 1920×1080 | 1× | 4.15 MB | ~41 ms | does not fit in 60 fps; the 8×16 font becomes tiny |

### With 720p output (`hdmi_mode=4`)

| Resolution | Scale | Notes |
|-------------|-------|------|
| 320×180 | 4× | |
| 640×360 | 2× | today's menu and games, without changing anything |
| 1280×720 | 1× | ~18.4 ms of copy: as above, beyond the frame |

At 720p the TV or monitor in turn scales up to the panel (often
1080p): the image may come out less sharp than with 1080p output.

### 32 bits per pixel

With 32 bits (XRGB8888) the bytes double: 640×360 costs ~9.2 ms of copy, 960×540
~20.7 ms. The menu stays at 16 bits.

## 4. What 960×540 would need

- **Background copy** with DMA (M14 driver) instead of the CPU, or only
  when the background changes (cover change, fade) instead of every
  frame.
- Larger **covers**: scaled 1.5× (192×120, a bit blurred) or a new
  192×120 cover in the `.bm` format.
- Redone **layout**: grid, bar, panels and text on 120×33 characters.
- **Mode change** at every switch between menu and game (games stay at 640×360 or
  320×180). Today it already happens (the menu is 16 bit, the console 32), but with the same
  size.
- GPU memory: 960×540 × 2 bytes × 3 pages = 3.1 MB, well within the default 64 MiB
  *(D)*.

## 5. Choice

**640×360** stays the menu resolution:
- 60 fps with margin (~4.6 ms of copy out of 16.7);
- same size as the `.bm` games and the console: no difference in proportions
  when going from one to the other;
- integer scale (3×) on the 1080p output, sharp pixels.

960×540 is the only realistic alternative to get more definition in the menu alone, with
the conditions of section 4.

## 6. Menu on the two systems (decision of 2026-10-04)

- The menu is **a single one** (`src/kernel/menu_ui.c`) for the Pi and the RGB30: same bar, same
  covers and panels. The height is 360 for all; the width decides the columns.
- **Pi, the ARM draws**: 640×360, as above (4 covers per row, 3× on 1080p).
- **Pi, `menu_scale=3`** in `bm/config.txt`: 1920×1080 with the same layout scaled 3×.
  The ARM scales it every frame (~4 MB written to video memory, like the background copy
  at 1080p in section 3): it is a test, it does not run at 60 fps. The next step is to have
  the GPU draw the menu (M37: covers, bars and text as V3D quads), and then
  1080p will also be able to have finer text and icons.
- **RGB30**: 360×360 RGB565, scaled 2× by the video controller onto the 720×720 panel (integer
  pixels, no CPU cost); 2 covers per row.
