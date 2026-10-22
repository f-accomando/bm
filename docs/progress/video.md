# Video: clips for cartridges and screen recording

Study: [video-feasibility-2026-10.md](../bench/video-feasibility-2026-10.md). PC spikes: [video-spikes-2026-10.md](../bench/video-spikes-2026-10.md). Overview: [progress.md](../progress.md).

## Rules (do not break)
* The Pi Zero W (RGB565, no 32-bit colour) is the benchmark.
* Mali paths on the RGB30 stay untouched.
* The first codec is MJPEG with `src/bm/jpeg.c` (baseline only). Then RLE+delta palette. MPEG-1 comes last.

## Open work
* VID-S3, VID-S4: done on the PC (see the spikes sheet).
* VID-S1, VID-S2: the Video bench is built. The run on the Pi is open (steps below).
* VID-FMT slice 1 is done: format [BM-VIDEO.md](../BM-VIDEO.md), reader `src/bm/vid.c`, tool `scripts/mkvid.py`, `jpeg_decode565` (RGB565 into a given buffer), `make test-vid`.
* VID-FMT slice 2 is done: the Lua player ([api/video.md](../api/video.md)): `video_open`, `clip:info/frame_at/draw/close`. The clip stays whole in memory (48 MiB at most) in `/bm/clips`. Test: `test_video_player`.
* VID-FMT slice 3 is open: the clip's audio (a PCM stream voice in the mixer), clips inside the cartridge, a frame read on demand for long clips.
* Decoder guard: `jpeg_decode565` checks the picture size against the buffer after the scan. A hostile frame that declares a huge size makes it allocate its planes first. Check the size from the SOF header before the scan.
* VID-REC: chunked screen recording. It depends on VID-S1.

## To run on the Pi (VID-S1, VID-S2)
1. Build and copy the kernel: `make`, then `./easy_install.sh` (SD card in the PC).
2. Boot the Pi Zero W with a card that has at least 100 MiB free. Use the card that you use for games.
3. Open the Dev tab. Select **Video bench**. Or send the monitor line `:video`.
4. Wait about 1 to 3 minutes. The bench writes 128 files of 256 KiB in `/bm/vidbench`, reads 16 back, deletes all, and decodes two JPEG frames 100 times each.
5. Send the report: the console sends the numeric report to the `reports` branch. Or copy the lines from the screen.
6. Read the result:
   * SD write: go for raw recording at 15 fps if the mean is at least 5.00 MB/s and the worst write is at most 100 ms.
   * JPEG decode: go for MJPEG at 15 fps if the 320x180 mean is at most 66 ms. If it is more, write the RLE+delta palette codec first, or add a fast IDCT to `jpeg.c`.
7. Repeat step 3 to 6 on a second card type if you have one. The result depends on the card.
`:video quick` runs a sixteenth of each count (the QEMU test uses it).
