# Video on bm: feasibility of a native clip codec and chunked screen recording (VIDEO-1)

Study, 2026-10-19. Research only: no code was changed and nothing was measured on hardware.
Every number is marked **F** (fact, with its source), **I** (inference, with the reasoning) or **S** (needs a spike).
Confidence: high / medium / low.

## 1. Facts about the two machines

| Point | Pi Zero W | RGB30 | Source | Conf. |
|---|---|---|---|---|
| RAM | 512 MiB, 64 MiB of it for the GPU (`gpu_mem=64`) | **1 GiB** LPDDR4 | F: `docs/HARDWARE.md:34-35,93`; `docs/RGB30.md:6`; `src/rgb30/plat.h:14` | high |
| CPU | ARM1176 1 GHz, VFP on (`-mfpu=vfp`, CPACR in `src/boot/start.S:108`) | Cortex-A55 | F: `Makefile:38`; `HARDWARE.md:92` | high |
| memcpy / fill | ~100 / ~430 MB/s | not found | F, measured: `HARDWARE.md:40-41`, `docs/PERFORMANCE.md:17` | high |
| Opaque row blit | ~14 ns/pixel | not found | F, measured: `PERFORMANCE.md:60` | medium |
| Screen the game draws | Pi: cartridges 640x360, 480x270 or 320x180 RGB565 (the 360x360 of the test header is a test value); RGB30: 360x360 | 360x360, 16 bpp RGB565, 3 buffers; panel 720x720 shows each pixel as 2x2 | F: `docs/HARDWARE.md:49` (Pi sizes); `docs/PERFORMANCE.md:58-59` (320x180 for lighting games, 640x360 otherwise); `src/rgb30/fb.c:85-95`, `plat.h:56-57` (RGB30) | high (Pi), medium (RGB30, not run) |
| Audio | 48000 Hz stereo, 256-sample chunks, 16/24/32-bit | same | F: `src/audio/audio.h:22-23`, `audio.c:94-96` | high |
| SD driver | EMMC2 / SDHOST, 512-byte blocks, multi-block with CMD12 | dwmmc, 512-byte blocks, multi-block with CMD12 | F: `src/drivers/sd_emmc.c:284,366`; `src/rgb30/rk_sd.c:95-178` | high |
| SD throughput | **not measured anywhere in the repo** | same | F: grep of `docs/ src/ tests/` found only RAM figures | high |
| FAT write | clusters first, then the directory entry; `fat_write_runs` batches whole clusters up to 32 KiB; one-sector cache; **no preallocation** | same code | F: `src/fs/fat.c:38-39`, `fat.h:86-96` | high |
| Screenshot | `shot_png` (fixed-Huffman deflate, one block) then `fat_write_file` to `/bm/shots`, synchronous, whole PNG | same | F: `src/bm/shot.c:171-179`, `src/bm/runtime.c:7244-7253` | high |
| Existing video | QEMU + Xvfb + ffmpeg, about 3.4x slower than real time in a full 3D scene, about 19x in idle menus and editors at `shift=1` (7.4x at `shift=2`, 2x at `shift=3`); the two figures are two scene types, not a contradiction | same tool | F: `video/lib/qemurec.py:1-26`; `docs/video/PIANO.md:113,117` | high |

**RAM discrepancy settled.** The 512 MiB at `docs/HARDWARE.md:115` is in the Pi section (the "RAM" row of the Pi table), not an RGB30 value. The RGB30 has 1 GiB (`plat.h:14` `PLAT_RAM_END 0x40000000`). The QEMU test uses `-m 512M` (`rgb30.mk:210`), which is a test value. The framebuffer takes the top 60-64 MiB and Mali 4 MiB (`plat.h:19,39`; `RGB30.md:383-384`), so about 0.9 GiB is left on the RGB30 and about 448 MiB on the Pi, before the runtime and the game.
Nothing in the repo contradicts the RGB30 number, so no doc fix is needed; a one-line note in `HARDWARE.md:115` naming "Pi" would stop the misreading.

## 2. Data rates (all **I**, plain arithmetic, 1 MB = 10^6 bytes)

Frame = width x height x bytes per pixel. Rates are the same on both machines; only the budget differs.

| Format | Bytes/frame | MB/s at 15 fps | MB/s at 30 fps |
|---|---|---|---|
| 360x360 RGB565 (what the game framebuffer holds) | 259,200 | 3.89 | 7.78 |
| 360x360 8-bit palette | 129,600 | 1.94 | 3.89 |
| 180x180 RGB565 | 64,800 | 0.97 | 1.94 |
| 180x180 8-bit palette | 32,400 | 0.49 | 0.97 |
| Audio 48 kHz stereo 16-bit | 192,000 per second | 0.19 | 0.19 |

**Required compression** to stream to the SD with no RAM buffer: `ratio >= rate / W`, W being the sustained SD write speed (unknown, **S1**).

| W (sustained) | 360x360 RGB565 at 15 fps | at 30 fps | 180x180 RGB565 at 30 fps |
|---|---|---|---|
| 2 MB/s | 1.9:1 | 3.9:1 | 1:1 |
| 5 MB/s | 1:1 | 1.6:1 | 1:1 |
| 10 MB/s | 1:1 | 1:1 | 1:1 |

**RAM buffer**, 64 MiB (67.1 MB) set aside: 360x360 RGB565 raw holds 17.3 s at 15 fps and 8.6 s at 30 fps; with 4:1 compression 69 s and 35 s. 64 MiB is a proposal (**I**): the Pi has about 448 MiB and the RGB30 about 0.9 GiB, but the game's own use is not measured here.

**Capture cost on the Pi (I, medium).** Copying one 259,200-byte frame at the measured ~100 MB/s memcpy takes about 2.6 ms, so about 8% of a frame at 30 fps. It is a lower bound (cache effects not counted).

**Pi rates at the sizes cartridges really use (I).** Bytes per frame x fps, 1 MB = 10^6:

| Format | Bytes/frame | MB/s at 15 fps | MB/s at 30 fps |
|---|---|---|---|
| 640x360 RGB565 | 460,800 | 6.91 | 13.82 |
| 640x360 8-bit palette | 230,400 | 3.46 | 6.91 |
| 480x270 RGB565 | 259,200 | 3.89 | 7.78 |
| 320x180 RGB565 | 115,200 | 1.73 | 3.46 |
| 320x180 8-bit palette | 57,600 | 0.86 | 1.73 |

Opaque blit at ~14 ns/pixel (`PERFORMANCE.md:60`): 640x360 palette expand 3.2 ms, 320x180 0.8 ms (I). 640x360 raw at 15 fps already needs 6.9 MB/s of SD writes, above the 5 MB/s line of the next table, so on the Pi the recorder should default to 320x180 or 480x270 (I, medium).

## 3. Playback codecs (scope 1)

| Candidate | License | Decode cost on the Pi Zero W | Compression (typical) | Verdict |
|---|---|---|---|---|
| RLE + delta on a palette | own code | Memory bound: a 129,600-pixel palette frame expands at the ~14 ns/pixel blit rate, 1.8 ms (**I**) | Content dependent: high on flat pixel art, poor on noisy 3D (**S3** measures it) | Best fit for short pixel-art clips and cutscenes; lossless |
| MJPEG with the decoder already in the tree | `src/bm/jpeg.c` is our own code, no new license | Not measured (**S2**). It is baseline only: progressive files are rejected (`jpeg.c:181-182`), so the encoder must write baseline | About 15-25 KB per 360x360 frame at quality 70-80 (**not sourced**: a typical-JPEG guess, no file was measured; **S2** measures it): 0.2-0.75 MB/s at 15-30 fps | Best general fit; needs only a container and an `ffmpeg -c:v mjpeg` recipe |
| MPEG-1 with `pl_mpeg` | The header says `SPDX-License-Identifier: MIT` (I fetched `pl_mpeg.h` from the upstream master). I found no `LICENSE` file at `LICENSE`, `LICENSE.md`, `LICENSE.txt` and the header carries no license text: **read the license on the project page before vendoring** (the rule in `CLAUDE.md` section 2 asks for the license to be confirmed) | Not measured (**S2**). It uses `double`/`float` and malloc (**not sourced**: from memory of the library, the source was not read for the IDCT); VFP is on, so it would run, but a float IDCT on a VFP11 would be the risk if the claim holds. It also decodes MP2 audio | 1 Mbit/s class: about 0.13 MB/s (**not sourced**: a typical MPEG-1 bitrate, not a measured clip) | Smallest files, most work, license text to confirm; try last |
| H.264 / H.265 | not applicable | Not on device (task) | | Out |

Recommendation (**I**, medium): ship in this order MJPEG (reuse), then RLE+delta palette, then MPEG-1 only if S2 shows it decodes a 360x360 frame in under 33 ms on the Pi.
A container is needed in every case: header (fps, size, frame count, audio rate), a frame index and the audio as raw PCM or the cartridge's own audio format. Seeking is by the index, so no codec needs keyframe logic beyond RLE+delta.

## 4. Chunked screen recording (scope 2)

Design (**I**):
1. At the game's present point, copy the frame into a RAM ring (`shot_grab` already knows where the frame is: `rt.g.px`, `runtime.c:7244`).
2. A cooperative job (the fibers in `src/kernel/fiber.c`, never an IRQ) writes finished chunks of 256 KiB to files `REC/0001.raw`, `0002.raw` ... on the SD with `fat_write_runs`. One file per chunk bounds the FAT work per write and a power loss costs one chunk.
3. Raw RGB565 on the card; audio as a second raw stream per chunk.
4. On the PC: `ffmpeg -f rawvideo -pix_fmt rgb565le -s 360x360 -r 15 -i REC.raw -f s16le -ar 48000 -ac 2 -i REC.pcm -c:v libx264 out.mp4` (the command to check in **S4**; the byte order of the 16-bit pixels must be confirmed against `fb.c` before it is trusted).
Hazards (**I**): no preallocation in the FAT code, so each chunk allocates clusters while recording; a game that also saves to the card shares the same single sector cache (`fat.c:38-39`). The recording must pause or drop frames when the ring is full instead of blocking the game.
At 15 fps raw 360x360 RGB565 needs 3.89 MB/s of SD writes: only safe when S1 shows W above ~5 MB/s on both machines. Below that, record 180x180, palette, or add the RLE+delta encoder to the recorder (the encoder is also what S3 measures).

## 5. Spikes (each is a task of its own; none exists yet as a command)

| ID | Question | Command (to be written by the task) | Pass criterion | Where |
|---|---|---|---|---|
| S1 | Sustained SD write speed | A test cart or kernel mode that writes 64 MiB in 256 KiB chunks with `fat_write_file`, timing each write with the system timer; print MB/s, min and worst write time | Report W (mean) and the worst single write. Go for raw 15 fps if W >= 5 MB/s and worst write <= 100 ms | Pi (via the bridge) and RGB30 (the user) |
| S2 | Decode time of one 360x360 frame | Loop of 100 decodes of a fixed quality-75 JPEG with `src/bm/jpeg.c`, then of an MPEG-1 clip with `pl_mpeg`, timed | Mean <= 33 ms for 30 fps, <= 66 ms for 15 fps, leaving time for the game's own work | Pi first (the slowest), then RGB30 |
| S3 | RLE+delta ratio | A PC script over frames from QEMU console captures (`qemurec.py` writes `console.mkv`, 640x360 lossless) and game screenshots and a few game screenshots: compress each frame against the previous one on the 256-color palette, print bytes per frame | Report min/median/max ratio per content type; go if the median is >= 3:1 on pixel-art content | PC only, no hardware |
| S4 | The offline path | The `ffmpeg` line in section 4 on a 3-second raw file made on the PC | An mp4 that plays with correct colors and A/V sync | PC only |

S3 and S4 need no device and can be done at once. S1 and S2 need hardware: not verified on hardware in this study.

## 6. Draft tasks (VID-S1..VID-DOC)

The "Who" column says how much planning a task needs.

| Task | Content | Who | Why |
|---|---|---|---|
| VID-S3 | The compression script of S3 and its report | small task | One script, PC only, a measurable result |
| VID-S4 | The ffmpeg recipe of S4 plus the pixel byte-order check against `fb.c` | small task | Small, PC only |
| VID-S1 | The SD write benchmark (cart or kernel mode) for Pi and RGB30 | code on the PC, run on the device | Needs the device |
| VID-S2 | The decode-time benchmark for `jpeg.c` and `pl_mpeg` | code on the PC, run on the device | Same |
| VID-FMT | The container format (header, index, audio) and the player API for the cartridge | plan first | Open choices: the Lua API, memory ownership, where clips live in the cartridge |
| VID-REC | The recorder (ring, job, chunk files) | plan first | Depends on S1; touches runtime, fiber and FAT paths |
| VID-DOC | A one-line "Pi" note at `docs/HARDWARE.md:115` | mechanical | Stops the 512 MiB misreading |

## 7. Open points and limits of this study

- The Pi sizes come from `HARDWARE.md:49` and `PERFORMANCE.md:58-59`; the RGB30 framebuffer size was read from `fb.c` but not run. The 360x360 tables are kept because the RGB30 uses that size.
- No number in section 3 about decode speed or compression is measured: they are what S2 and S3 are for.
- The `pl_mpeg` license was read from the header only; the project page was not opened for a license file.
- The runtime's free RAM during a game is not measured, so the 64 MiB buffer is a proposal.
- **S3 input not found.** `video/` in the repo holds only finished mp4 files (`video/*/ep0N.mp4`, `video/titan-clash/*.mp4`) and the scripts that make them; no `console.mkv` or raw capture is committed. S3 must first generate frames with `qemurec.py` (needs qemu-system-arm, Xvfb, ffmpeg) or decode the mp4 files with ffmpeg, which are already compressed and bias the ratio.
- Figures tagged **not sourced** (MJPEG size, pl_mpeg float IDCT, 1 Mbit/s) are guesses until S2 and S3 run.
