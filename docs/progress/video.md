# Video: clips for cartridges

Study: [video-feasibility-2026-10.md](../bench/video-feasibility-2026-10.md). PC spikes: [video-spikes-2026-10.md](../bench/video-spikes-2026-10.md). Overview: [progress.md](../progress.md).

## Rules (do not break)
* The Pi Zero W (RGB565, no 32-bit colour) is the benchmark.
* Mali paths on the RGB30 stay untouched.
* The first codec is MJPEG with `src/bm/jpeg.c` (baseline only). Then RLE+delta palette. MPEG-1 comes last.
* `src/bm/jpeg.c` only decodes. The player reads the raw codecs and RLE565 (codec 4, lossless, intra only, see [BM-VIDEO.md](../BM-VIDEO.md)).
* Any change of the RLE565 token format needs a new codec number. Keep the old one readable.

The screen recorder was removed from the kernel on 2026-11-09. The full description, and the commit to build it again from, are in [video-recorder.md](../archive/video-recorder.md).

## Open work
* VID-S3, VID-S4: done on the PC (see the spikes sheet).
* VID-S1, VID-S2: done on the Pi Zero W (22 Oct, reports branch). SD write 2.50 MB/s mean, worst write 603 ms: **NO GO** for raw recording at 15 fps (needs 5.00 MB/s and 100 ms). SD read 5.14 MB/s. JPEG decode: 320x180 in 53 ms (under 66 ms: MJPEG at 15 fps is a go), 480x270 in 116 ms. The Video bench that took them was removed on 24 Oct 2026.
* VID-FMT slice 1 is done: format [BM-VIDEO.md](../BM-VIDEO.md), reader `src/bm/vid.c`, tool `scripts/mkvid.py`, `jpeg_decode565` (RGB565 into a given buffer), `make test-vid`.
* VID-FMT slice 2 is done: the Lua player ([api/video.md](../api/video.md)): `video_open`, `clip:info/frame_at/draw/close`. The clip stays whole in memory (48 MB at most) in `/bm/clips`. Test: `test_video_player`.
* VID-FMT slice 3 is open: the clip's audio (a PCM stream voice in the mixer), clips inside the cartridge, a frame read on demand for long clips.
* Decoder guard: `jpeg_decode565` checks the picture size against the buffer after the scan. A hostile frame that declares a huge size makes it allocate its planes first. Check the size from the SOF header before the scan.

## The Video bench (removed)
The Video bench (Settings > System > Utilities, monitor line `:video`) was removed from the kernel on 24 Oct 2026. It measured the SD write and read speed, the JPEG decode time and the RLE565 encoder. The numbers below stay as measurements. The test frame of the clip tests is in `tests/video/vidframes.c`.

## Pi Zero W numbers of the Video bench (24 Oct, API-4, report `10993a4b` in the `reports` branch)

- SD write, 128 files of 256 KB: 1.82 MB/s mean, worst 1060 ms, 35 writes over 100 ms. Raw 15 fps does not fit. SD read: 2.29 MB/s.
- JPEG decode: 320x180 53.15 ms mean (fits the 66 ms of 15 fps); 480x270 115.74 ms mean.
- RLE565 encoder: a pixel-art frame is 9253 bytes (8.0% of 115 KB) in 0.78 ms; a photo frame is 70306 bytes (61.0%) in 1.75 ms. 15 fps needs 0.13 to 1.05 MB/s and fits the card. The round trip is exact.
- Decision at that time: RLE565 (codec 4) at 15 fps for the recorder, which is now removed ([video-recorder.md](../archive/video-recorder.md)).

## Tutorial videos (`video/`, skill `claude/skills/bm-video-tutorial`)
`video/lib/bmvideo.py` drives `bmhost` from a key script and lays out the page with
ffmpeg/libass; storyboard and script come from the same key script. Series *Skyvale World*
(`video/SERIE.md`), episodes 01–07 complete, each with a `verify.py`. `bmhost --tool` runs a
tool; `make bmhost-ai` builds `bmhost` with the real `ai` table. On the PC: no kernel menu.
