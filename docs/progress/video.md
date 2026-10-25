# Video: clips for cartridges and screen recording

Study: [video-feasibility-2026-10.md](../bench/video-feasibility-2026-10.md). PC spikes: [video-spikes-2026-10.md](../bench/video-spikes-2026-10.md). Overview: [progress.md](../progress.md).

## Rules (do not break)
* The Pi Zero W (RGB565, no 32-bit colour) is the benchmark.
* Mali paths on the RGB30 stay untouched.
* The first codec is MJPEG with `src/bm/jpeg.c` (baseline only). Then RLE+delta palette. MPEG-1 comes last.
* `src/bm/jpeg.c` only decodes. The recorder does not write MJPEG. It writes RLE565 (codec 4, lossless, intra only, see [BM-VIDEO.md](../BM-VIDEO.md)).
* The recorder default format is `rle565`. A raw format (`rec_format=rgb565` or `rgb332`) stays readable and writable.
* A chunk of RLE565 frames ends at 256 frames or at about 256 KB of frame bytes. The frames of a chunk have different sizes, so the index is written after the last frame.
* Any change of the RLE565 token format needs a new codec number. Keep the old one readable.

## Open work
* VID-S3, VID-S4: done on the PC (see the spikes sheet).
* VID-S1, VID-S2: done on the Pi Zero W (22 Oct, reports branch). SD write 2.50 MB/s mean, worst write 603 ms: **NO GO** for raw recording at 15 fps (needs 5.00 MB/s and 100 ms). SD read 5.14 MB/s. JPEG decode: 320x180 in 53 ms (under 66 ms: MJPEG at 15 fps is a go), 480x270 in 116 ms. The Video bench that took them was removed on 24 Oct 2026.
* VID-FMT slice 1 is done: format [BM-VIDEO.md](../BM-VIDEO.md), reader `src/bm/vid.c`, tool `scripts/mkvid.py`, `jpeg_decode565` (RGB565 into a given buffer), `make test-vid`.
* VID-FMT slice 2 is done: the Lua player ([api/video.md](../api/video.md)): `video_open`, `clip:info/frame_at/draw/close`. The clip stays whole in memory (48 MB at most) in `/bm/clips`. Test: `test_video_player`.
* VID-FMT slice 3 is open: the clip's audio (a PCM stream voice in the mixer), clips inside the cartridge, a frame read on demand for long clips.
* Decoder guard: `jpeg_decode565` checks the picture size against the buffer after the scan. A hostile frame that declares a huge size makes it allocate its planes first. Check the size from the SOF header before the scan.
* VID-REC slice 1 is done (below): the recorder, off by default. The RLE565 encoder is done (lossless, host-tested round trip, QEMU tests). The Pi numbers of the encoder are measured (below). Open: a palette (8-bit) format with a real palette (it needs a palette per frame or per clip and a quantizer: a separate slice), a Dev tab switch, audio in the recording, recording from a cartridge's own call, a delta against the previous frame if the Pi numbers show that intra frames are still too big.
* Kratos Build demo (`carts/kratos`, branch `claude/kratos-mesh-pointer-demo`): a recording target for the recorder. Not run on the Pi Zero W yet. Open: run it with `rec=on` (480x270, 15 fps), read the `rec:` report lines, check frame time (2164 faces on screen at the end).

## Screen recorder (VID-REC)
Code: `src/bm/rec.c`. It is off by default. Keys in `bm/config.txt`: `rec=on`, `rec_fps` (1 to 30, default 15), `rec_size` (WxH, default 320x180), `rec_format` (`rle565` default, `rgb565`, or `rgb332`), `rec_ring_mb` (1 to 256, default 64).
* **Capture.** At the present point of the game (`shot_grab`), when a frame is due, the frame is scaled by nearest pixel to `rec_size` and copied into a chunk of the ring in RAM. A chunk holds frames up to about 256 KB (3 frames at 320x180 raw RGB565). With `rle565` each row is encoded as it is scaled (run-length, repeat, and copy of the row above), so a frame of a game is a fraction of the raw size and a chunk holds up to 256 frames.
* **Write.** A fiber writes finished chunks as `bm/rec/Rnnnmmmm.BMV` (nnn the run, mmmm the chunk; each file is a `.bmv` clip of codec 4, 2 or 3, see [BM-VIDEO.md](../BM-VIDEO.md)). It runs in the time the game would wait for the next frame, at least 1 ms a frame when there is work. A write piece is a run of clusters (up to 32 KB): the fiber cannot stop inside it, so the game can wait for one piece (about 16 ms at 2 MB/s). The report gives the longest wait. At 30 fps the card must take frame bytes x 30 a second: `rle565` keeps this under the 2.50 MB/s of the Pi test card when a frame is under about 80 KB (the report prints the figure).
* **Never blocks.** When every chunk of the ring is in use, the due frame is dropped and counted (`dropped`). The game's own `save()` and screenshots hold the writer back for 300 ms (one sector cache for the card).
* **FAT cost.** There is no preallocation: each chunk file allocates clusters while it is written. Each file is one `fat_write_file` call, so a power cut loses at most the chunks in the ring.
* **Report.** When the game ends (or is suspended), the console prints three lines (`rec: run N done ...`, `rec: rle565, N bytes a frame (mean), M largest, encode X ms a frame (mean), Y ms worst, Z MB/s at F fps.` and `rec: card X MB/s while writing, worst chunk ..., game waited at most ... in a pump, ring high A of B chunks.`). The second line is the cost of the encoder: bytes a frame, time to scale and encode a frame, and the card rate the recording needs. A red `REC captured dropped` text shows in the top left corner of the screen while it records (it is not in the recording).
* **Play a recording** in a cartridge: `video_open("rec/R0010001.BMV")` ([api/video.md](../api/video.md)). On the PC, convert the chunks with a small script (the header gives the codec; see [BM-VIDEO.md](../BM-VIDEO.md) for the RLE565 tokens).
* **Tests:** `test_video_rec` (raw RGB565: 2 s of a cart, chunks read back, same colours through the cart player), `test_video_rec_rle` (the same with RLE565, and the encoder line in the report), `test_video_rec_drops` (raw RGB565, a game that saves every frame, a ring of 1 MB: drops are counted, nothing blocks), `make test-vid` (the raw codecs, and `test_vid rle`: bit exact round trips of flat, noise and pixel-art frames at many sizes, hostile streams).

## The Video bench (removed)
The Video bench (Settings > System > Utilities, monitor line `:video`) was removed from the kernel on 24 Oct 2026. It measured the SD write and read speed, the JPEG decode time and the RLE565 encoder. The numbers below stay as measurements. To measure the encoder on another card, set `rec=on` and read the `rec:` lines (next section). The test frame of the clip tests is in `tests/video/vidframes.c`.

## Pi Zero W numbers of the Video bench (24 Oct, API-4, report `10993a4b` in the `reports` branch)

- SD write, 128 files of 256 KB: 1.82 MB/s mean, worst 1060 ms, 35 writes over 100 ms. Raw 15 fps does not fit. SD read: 2.29 MB/s.
- JPEG decode: 320x180 53.15 ms mean (fits the 66 ms of 15 fps); 480x270 115.74 ms mean.
- RLE565 encoder: a pixel-art frame is 9253 bytes (8.0% of 115 KB) in 0.78 ms; a photo frame is 70306 bytes (61.0%) in 1.75 ms. 15 fps needs 0.13 to 1.05 MB/s and fits the card. The round trip is exact.
- Decision: RLE565 (codec 4) at 15 fps is the Pi Zero recorder; the Settings row "Screen recorder" sets `rec=on`. Next slice: audio interleaved.

## To run the recorder on the Pi (VID-REC)
It needs a game that you want to record (for example Overbit).
1. Put these lines in `bm/config.txt` on the SD card (or send them with the monitor line `:set rec=on rec_fps=15 rec_size=320x180`):
   ```
   rec=on
   rec_fps=15
   rec_size=320x180
   rec_format=rgb565
   ```
2. Play the game for about one minute. Watch the red `REC n dN` text in the top left corner: `N` after `d` must stay at 0.
3. Leave the game (Start+Select). The console prints the two `rec:` lines. Read them on the screen, or copy `bm/lastrun.txt` from the SD card (the report goes to the `reports` branch if reports are on).
4. Write down: `dropped`, `card ... MB/s while writing`, `worst chunk`, `game waited at most`, `ring high`.
5. Copy the folder `bm/rec` from the SD card to the PC and send it back with the numbers. Delete the folder on the card afterwards (the files fill the card: 1.7 MB/s is 100 MB a minute).
6. Repeat with `rec_size=480x270` (3.9 MB/s) and with `rec_format=rgb332`, and with a 3D game and a 2D game.
7. Read the result: if `dropped` is 0 and `game waited` stays under 20 ms, raw recording at that size is safe on this card. If it drops, use a smaller size, `rgb332`, or wait for the MJPEG encoder slice.

## Tutorial videos (`video/`, skill `claude/skills/bm-video-tutorial`)
`video/lib/bmvideo.py` drives `bmhost` from a key script and lays out the page with
ffmpeg/libass; storyboard and script come from the same key script. Series *Skyvale World*
(`video/SERIE.md`), episodes 01–07 complete, each with a `verify.py`. `bmhost --tool` runs a
tool; `make bmhost-ai` builds `bmhost` with the real `ai` table. On the PC: no kernel menu.
