# The screen recorder (VID-REC), removed from the kernel

The kernel had a screen recorder for games. It was off by default. It was removed on 2026-11-09 at the request of the user. This file describes it in full, so it can be built again. The last kernel that has it is the commit `eba4dcdb` of `bm-core`. Read any file of the recorder with `git show eba4dcdb:<path>`.

What stays in the kernel: the reader and the player of `.bmv` clips (`core/src/bm/vid.c`, `core/src/bm/api_video.c`, [api/video.md](../api/video.md)), the format ([BM-VIDEO.md](../BM-VIDEO.md)) and the tool `core/scripts/mkvid.py`. The player reads the raw and RLE565 codecs that the recorder wrote. The row encoder `bmv_rle565_row` stays in `core/src/bm/vid.c` (the tests of the codec use it).

## What it did

At the present point of a game, the recorder took the frame, scaled it, and put it in a ring of chunks in RAM. A fiber wrote the finished chunks to the SD card as `.bmv` clips, in the time that the game would wait for the next frame. The recorder never blocked the game: when the ring was full, the frame was dropped and counted.

### Use

1. Put `rec=on` in `bm/config.txt` (or in Settings: "Screen recorder", Enter changed it; or the monitor line `:set rec=on`). The recorder starts when a game starts and stops when the game ends.
2. The keys of `bm/config.txt`:

| Key | Values | Default |
|---|---|---|
| `rec` | `on` or `1`: record every game run | off |
| `rec_fps` | 1 to 30 | 15 |
| `rec_size` | `WxH`, 16x16 to 1920x1080 | 320x180 |
| `rec_format` | `rle565` (lossless), `rgb565`, `rgb332` | `rle565` |
| `rec_ring_mb` | 1 to 256 (MB of the ring in RAM) | 64 |

3. A red text `REC <captured> d<dropped>` showed in the top left corner while it recorded. It was drawn after the frame was taken, so it is not in the recording.
4. The files were `bm/rec/rNNNmmmm.bmv`: NNN the run (one more than the highest run in the folder, 1 to 999) and mmmm the chunk (1 to 9999). Each file was a clip of codec 4 (RLE565), 2 (RGB565) or 3 (RGB332) with the header of [BM-VIDEO.md](../BM-VIDEO.md): `BMV1`, version, codec, size, fps, frame count, index offset.
5. To play a recording in a cartridge: `video_open("rec/R0010001.BMV")` (the player looked in `/bm/rec/` for a name with the prefix `rec/`; that path was removed with the recorder, copy the file to `bm/clips/`).

### How it worked

* **Capture** (`rec_frame`, called at the present point of the game, in `core/src/bm/runtime.c` where the screenshot is taken). A frame was due every `1000000 / fps` microseconds. If the game was late, the next due time was `now + period` (no burst to catch up). The frame was scaled by nearest pixel to `rec_size` (a table of source columns, made again when the size changed). The recorder copied the frame into the chunk being filled.
* **Formats.** `rgb565`: 2 bytes a pixel, little endian. `rgb332`: 1 byte a pixel (`RRRGGGBB`). `rle565`: each row was encoded as it was scaled, with `bmv_rle565_row` (run-length, repeat, and copy of the row above; the tokens are in BM-VIDEO.md), so the frames of a chunk had different sizes and the index was written after the last frame.
* **Chunks.** A chunk held frames up to about 256 KB (`CHUNK_BYTES`). With RLE565 a chunk ended at 256 frames or when it held 256 KB of frame bytes. A chunk was one clip file with its own header and index. The ring had `ring_bytes / slot_bytes` slots (the ring was made smaller if memory was short; at least 2 slots and 1 MB).
* **Write.** `rec_pump(until)` ran in the frame loop of the runtime (`runtime.c`), after the draw, with the time that the frame had left (at least 1 ms when there was work). It ran a fiber job (`fiber_job_start`, stack 16 KB) that called `fat_write_file("/bm/rec", name, chunk, len)` for the oldest finished chunk. One write was a run of clusters (up to 32 KB): the fiber could not stop inside it, so the game could wait for one piece (about 16 ms at 2 MB/s).
* **Never blocks.** If every slot was in use, the due frame was dropped (`dropped_full`). A save or a screenshot of the game (`rec_note_game_io`, called by `save()` in `api_save.c` and by the screenshot path) held the writer back for 300 ms: the card has one sector cache.
* **FAT cost.** No preallocation: each chunk file allocated its clusters while it was written. A power cut lost at most the chunks that were still in the ring.
* **The limit of files.** A FAT directory is read from its start for every name. With 5078 files in `/bm/rec` on the Pi 1, the scan for the run number took 4.4 s at the start of a run and each chunk write took 9 s, and the console served no network meanwhile (see [system-net](../progress/system-net.md), 2026-11-09). So `MAX_FILES = 400`: with more files the scan stopped, the recorder stayed off and printed why. It also printed when the scan took more than 300 ms.
* **Report.** When the game ended (or was suspended), `rec_stop` wrote the rest of the ring to the card and printed three lines:
  * `rec: run N done: F frames in S s, dropped D (ring full), C chunks (X failed), K KB`
  * `rec: rle565, B bytes a frame (mean), L largest, encode E ms a frame (mean), W ms worst, R MB/s at P fps.`
  * `rec: card M MB/s while writing, worst chunk T ms, game waited at most G ms in a pump, ring high H of S chunks.`

  Also `rec: chunk N (K KB) took T ms` for a chunk over 1 s, and `rec: chunk N not written: <FAT error>`. At the start: `rec: run N, WxH format at P fps, ring S chunks of K KB (M MB), files /bm/rec/rNNN0001.bmv...`.

### The code

| Place | What |
|---|---|
| `core/src/bm/rec.c` (434 lines), `core/src/bm/rec.h` | the whole recorder: `rec_config` (reads the keys), `rec_start`, `rec_stop`, `rec_active`, `rec_due`, `rec_frame`, `rec_pump`, `rec_note_game_io`, `rec_overlay_text`; the types `rec_cfg_t` (w, h, fps, format, ring_bytes) and `rec_stats_t` (captured, dropped_full, chunks, chunk_fail, bytes, write_us, write_worst_us, pump_worst_us, slots, slots_high, frame_bytes, frame_max, enc_us, enc_worst_us, run) |
| `core/src/bm/runtime.c` | `rec_config` and `rec_start` at the start of a run (`rec=on`), `rec_due` and `rec_frame` at the present point, `rec_pump(*deadline)` after the draw, `rec_note_game_io` after a screenshot, `rec_overlay_text` for the red text, `rec_stop` when the run ends |
| `core/src/bm/api_save.c` | `rec_note_game_io()` in `save()` |
| `core/src/bm/api_*.c` | each one included `rec.h` (a list that the files share) |
| `core/src/bm/api_video.c` | `REC_CLIP_DIR "/bm/rec/"`: `video_open("rec/...")` |
| `core/src/kernel/config.c`, `config.h` | `rec_mode()` and `rec_set_mode()` (the key `rec`) |
| `core/src/kernel/settings.c` | the row `R_REC` "Screen recorder" (a choice row, "On" or "Off") on the Settings page and on the System page, and its handler (`recorder: on (/bm/rec, RLE565 15 fps)`) |
| `core/src/kernel/monitor.c` | the keys `rec=`, `rec_fps=`, `rec_format=` in the list that `set` accepts |
| `docs/spec/config-keys.toml` | the keys `rec`, `rec_fps`, `rec_size`, `rec_format`, `rec_ring_mb` |
| `manuals/content/*/install-sd.md` | the rows `rec` and `rec_format` of the table of keys |
| `tests/host/stubs.c` | the stubs of the recorder for `bmhost` (it was off there) |
| `tests/qemu_test.py` | `test_video_rec`, `test_video_rec_rle`, `test_video_rec_drops` (below), and `keys("ss")` in the Settings walk that skipped the "Screen recorder" row |

### The tests

* `test_video_rec`: raw RGB565, 2 s of a cartridge at 15 fps; the chunks are read back; every frame is one colour and consecutive frames differ; the same colours through the cartridge player.
* `test_video_rec_rle`: the same with RLE565, and the encoder line in the report.
* `test_video_rec_drops`: raw RGB565, a game that saves every frame, a ring of 1 MB: the drops are counted and nothing blocks.
* `make test-vid` (the codecs and the RLE565 round trips, hostile streams) stays: it does not need the recorder.

### Measurements (Pi Zero W, 22 to 24 Oct 2026)

* The SD card wrote 1.82 to 2.50 MB/s, worst write 603 ms to 1060 ms. Raw 15 fps at 320x180 (1.7 MB/s) did not fit with margin.
* RLE565 encoder: a pixel-art frame 9253 bytes (8.0% of 115 KB) in 0.78 ms; a photo frame 70306 bytes (61.0%) in 1.75 ms. At 15 fps the recording needed 0.13 to 1.05 MB/s: it fits the card.
* JPEG decode (player, not recorder): 320x180 in 53 ms, 480x270 in 116 ms.
* Decision at that time: RLE565 (codec 4) at 15 fps is the recorder of the Pi Zero W.

### Ideas that were open

A palette format (8 bit, a palette for each frame or clip, a quantizer); the audio of the game in the recording; a delta against the previous frame if intra frames stay too big; a switch in the Dev tab; recording started by a call of the cartridge. The demo `bm/kratos` was made as a recording target.

## How to build it again

1. `git show eba4dcdb:core/src/bm/rec.c > core/src/bm/rec.c` and the same for `rec.h`.
2. Put back the calls in `runtime.c` and `api_save.c`, `rec_mode()` in `config.c`, the keys in `monitor.c` and `config-keys.toml`, the rows in `settings.c`, and the tests: take the lines of the commit `eba4dcdb`, and use `git log -S"rec_pump"` to find the change that took them away.
3. Keep the limit of files (`MAX_FILES`). Run `make test-vid` and the three `test_video_rec*` tests of `tests/qemu_test.py`.
