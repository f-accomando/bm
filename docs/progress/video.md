# Video: clips for cartridges and screen recording

Study: [video-feasibility-2026-10.md](../bench/video-feasibility-2026-10.md). PC spikes: [video-spikes-2026-10.md](../bench/video-spikes-2026-10.md). Overview: [progress.md](../progress.md).

## Rules (do not break)
* The Pi Zero W (RGB565, no 32-bit colour) is the benchmark.
* Mali paths on the RGB30 stay untouched.
* The first codec is MJPEG with `src/bm/jpeg.c` (baseline only). Then RLE+delta palette. MPEG-1 comes last.

## Open work
* VID-S3, VID-S4: done on the PC (see the spikes sheet).
* VID-S1, VID-S2: done on the Pi Zero W (22 Oct, reports branch). SD write 2.50 MB/s mean, worst write 603 ms: **NO GO** for raw recording at 15 fps (needs 5.00 MB/s and 100 ms). SD read 5.14 MB/s. JPEG decode: 320x180 in 53 ms (under 66 ms: MJPEG at 15 fps is a go), 480x270 in 116 ms. Next: an encoder (MJPEG or RLE+delta) for the recorder. Steps to repeat the run on another card are below.
* VID-FMT slice 1 is done: format [BM-VIDEO.md](../BM-VIDEO.md), reader `src/bm/vid.c`, tool `scripts/mkvid.py`, `jpeg_decode565` (RGB565 into a given buffer), `make test-vid`.
* VID-FMT slice 2 is done: the Lua player ([api/video.md](../api/video.md)): `video_open`, `clip:info/frame_at/draw/close`. The clip stays whole in memory (48 MiB at most) in `/bm/clips`. Test: `test_video_player`.
* VID-FMT slice 3 is open: the clip's audio (a PCM stream voice in the mixer), clips inside the cartridge, a frame read on demand for long clips.
* Decoder guard: `jpeg_decode565` checks the picture size against the buffer after the scan. A hostile frame that declares a huge size makes it allocate its planes first. Check the size from the SOF header before the scan.
* VID-REC slice 1 is done (below): the recorder, off by default. Open: MJPEG or RLE+delta encoding (the Pi numbers are in VID-S1 above: raw writes are too slow), a palette (8-bit) format with a real palette, a Dev tab switch, audio in the recording, recording from a cartridge's own call.

## Screen recorder (VID-REC)
Code: `src/bm/rec.c`. It is off by default. Keys in `bm/config.txt`: `rec=on`, `rec_fps` (1 to 30, default 15), `rec_size` (WxH, default 320x180), `rec_format` (`rgb565` default, or `rgb332`), `rec_ring_mb` (1 to 256, default 64).
* **Capture.** At the present point of the game (`shot_grab`), when a frame is due, the frame is scaled by nearest pixel to `rec_size` and copied into a chunk of the ring in RAM. A chunk holds frames up to about 256 KiB (3 frames at 320x180 RGB565).
* **Write.** A fiber writes finished chunks as `bm/rec/Rnnnmmmm.BMV` (nnn the run, mmmm the chunk; each file is a `.bmv` clip of codec 2 or 3, see [BM-VIDEO.md](../BM-VIDEO.md)). It runs in the time the game would wait for the next frame, at least 1 ms a frame when there is work. A write piece is a run of clusters (up to 32 KiB): the fiber cannot stop inside it, so the game can wait for one piece (about 16 ms at 2 MB/s). The report gives the longest wait.
* **Never blocks.** When every chunk of the ring is in use, the due frame is dropped and counted (`dropped`). The game's own `save()` and screenshots hold the writer back for 300 ms (one sector cache for the card).
* **FAT cost.** There is no preallocation: each chunk file allocates clusters while it is written. Each file is one `fat_write_file` call, so a power cut loses at most the chunks in the ring.
* **Report.** When the game ends (or is suspended), the console prints two lines (`rec: run N done ...` and `rec: card X MB/s while writing, worst chunk ..., game waited at most ... in a pump, ring high A of B chunks.`). A red `REC captured dropped` text shows in the top left corner of the screen while it records (it is not in the recording).
* **Play a recording** in a cartridge: `video_open("rec/R0010001.BMV")` ([api/video.md](../api/video.md)). On the PC, join the chunks with `ffmpeg` (each chunk is raw frames: read the header, or convert with a small script).
* **Tests:** `test_video_rec` (2 s of a cart, chunks read back, same colours through the cart player), `test_video_rec_drops` (a game that saves every frame, a ring of 1 MiB: drops are counted, nothing blocks), `make test-vid` (the raw codecs).

## To run on the Pi (VID-S1, VID-S2; done once, repeat on another card)
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

## To run the recorder on the Pi (VID-REC)
Do this after the Video bench above. It needs a game that you want to record (for example Overbit).
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
