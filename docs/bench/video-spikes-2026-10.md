# Video spikes on the PC (2026-10-21)

Results of the spikes that need no hardware. Study: [video-feasibility-2026-10.md](video-feasibility-2026-10.md). Area sheet: [video.md](../progress/video.md).
The numbers are measured on the PC unless the row says **I** (inference).

## VID-S3: size of one frame (tool: `tools/video/spike_sizes.py`)

Input: 8 s of finished mp4 files from `video/` and `docs/img/` (the repo has no raw captures). Each frame is scaled with Lanczos and taken at 15 fps. The sources are already lossy, so the noise helps neither codec. RLE+delta is measured on a 256-colour palette made from the whole clip, a key frame every 30 frames. Raw RGB565 is 115,200 bytes at 320x180 and 259,200 bytes at 480x270.

Mean bytes per frame at 320x180:

| Clip (content) | MJPEG q3 | MJPEG q6 | MJPEG q10 | MJPEG q15 | RLE+delta pal8 |
|---|---|---|---|---|---|
| overbit-match (3D game) | 15,061 | 10,071 | 7,238 | 5,477 | 35,326 |
| ep01 (editor, pixel art) | 10,808 | 7,175 | 5,050 | 3,793 | 20,167 |
| ep03 (editor, sound) | 11,445 | 7,452 | 5,162 | 3,810 | 21,665 |
| titan-clash (3D game) | 23,488 | 15,906 | 11,338 | 8,319 | 39,118 |

Mean bytes per frame at 480x270:

| Clip | MJPEG q3 | MJPEG q6 | MJPEG q10 | MJPEG q15 | RLE+delta pal8 |
|---|---|---|---|---|---|
| overbit-match | 27,016 | 18,247 | 13,242 | 10,095 | 67,226 |
| ep01 | 19,815 | 13,536 | 9,844 | 7,412 | 37,331 |

Results:
* MJPEG q6 gives 7:1 to 16:1 against RGB565. RLE+delta gives 2.9:1 to 6.9:1.
* The pass criterion of the study (median 3:1 on pixel-art content) is met by RLE+delta (5.3:1 and 5.7:1 on the editor clips). It fails for 3D content (2.9:1 and 3.3:1).
* MJPEG is smaller than RLE+delta by 2x to 3.5x on every clip.
* Data rate of MJPEG q6 at 320x180 and 15 fps: 0.11 to 0.24 MB/s. This is far below any SD read speed.
* Decision: MJPEG stays the first codec. RLE+delta is useful only if the MJPEG decode time (VID-S2) is too slow.

## VID-S4: offline path

The line below turned a raw RGB565 file and a raw PCM file into an mp4 on the PC (3 s, 320x180, 15 fps, 48 kHz stereo). `ffprobe` showed h264 3.00 s and aac 3.00 s.

```
ffmpeg -f rawvideo -pix_fmt rgb565le -s 320x180 -r 15 -i REC.raw -f s16le -ar 48000 -ac 2 -i REC.pcm -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest out.mp4
```

The byte order is little-endian. `g16_rgb` (`src/bm/gfx16.h:38`) packs `r<<11 | g<<5 | b` in a `uint16_t`, and the ARM cores are little-endian. This matches `rgb565le`. An eye check of the colours on a real recording stays open (VID-REC).

## Container estimate

Layout (draft, see the format sheet when VID-FMT lands): a 32-byte header, a frame index of 8 bytes a frame (offset and size/flags), the frames, then the audio as raw PCM.
The sizes below are for 10 s at 15 fps, 320x180, MJPEG q6 (10,071 bytes a frame, overbit-match):

| Part | 48 kHz stereo s16 | 22.05 kHz mono s16 | 22.05 kHz mono u8 |
|---|---|---|---|
| Header | 32 | 32 | 32 |
| Index (150 frames) | 1,200 | 1,200 | 1,200 |
| Video (150 frames) | 1,510,650 | 1,510,650 | 1,510,650 |
| Audio | 1,920,000 | 441,000 | 220,500 |
| **Total** | **3,431,882** | **1,952,882** | **1,732,382** |

Results:
* The index costs 0.08% of the file. A seek is one index read.
* The audio is the largest part with 48 kHz stereo (56%). A clip should store 22.05 kHz mono by default.
* The clip audio must stream from the file. The cartridge sample bank holds at most 2^20 values (`AU_PCM_MAX`), so a 10 s clip does not fit in it.
* The MJPEG decoder (`src/bm/jpeg.c`) allocates its output on each call and writes 24-bit RGB. A player needs a call that writes RGB565 into a given buffer and keeps its tables between frames.
* **I**: the 320x180 frame has 1,400 blocks. The second IDCT pass is 512 multiply-adds a block, so a frame costs at most about 1.4 million float operations. The decode time on the Pi is open until VID-S2 runs.
