# .bmv: the video clip format

A `.bmv` file is a short clip for a cartridge. It holds baseline JPEG frames, a frame index and raw PCM audio. Study and numbers: [video-feasibility-2026-10.md](bench/video-feasibility-2026-10.md), [video-spikes-2026-10.md](bench/video-spikes-2026-10.md). Area sheet: [video.md](progress/video.md).

All numbers are little-endian.

## Layout

| Part | Where | Size |
|---|---|---|
| Header | 0 | 32 bytes |
| Frames | 32 | the JPEG files, one after the other |
| Index | `index_off` | 8 bytes a frame |
| Audio | `audio_off` | `audio_len` bytes |

## Header

| Offset | Type | Field | Rule |
|---|---|---|---|
| 0 | 4 bytes | magic | `BMV1` |
| 4 | u8 | version | 1 |
| 5 | u8 | codec | 1 = baseline JPEG (MJPEG); 2 = raw RGB565, little-endian (the recorder); 3 = raw RGB332 (`rrrgggbb`, the recorder) |
| 6 | u8 | audio channels | 0 = no audio, 1 or 2 |
| 7 | u8 | audio bits | 8 (unsigned) or 16 (signed); 0 with no audio |
| 8 | u16 | width | 1 to 1920 |
| 10 | u16 | height | 1 to 1080 |
| 12 | u16 | fps | 1 to 240 |
| 14 | u16 | audio rate (Hz) | 4000 to 48000; 0 with no audio |
| 16 | u32 | frame count | 1 to 1,000,000 |
| 20 | u32 | `index_off` | at least 32 |
| 24 | u32 | `audio_off` | at least 32; 0 with no audio |
| 28 | u32 | `audio_len` | a whole number of sample frames |

## Index entry

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | offset of the frame in the file (at least 32) |
| 4 | u32 | size of the frame in bytes (at least 4) |

Every frame has the size of the clip. A raw frame is exactly width x height x 2 (codec 2) or x 1 (codec 3) bytes; the reader refuses another size. Every frame is a key frame, so a seek is one index read.

## Rules

* The reader checks the header and every index entry when it opens the clip. A frame, the index or the audio outside the file is refused.
* The encoder writes baseline JPEG. The decoder in `src/bm/jpeg.c` refuses progressive files.
* The default audio is 22.05 kHz mono 16-bit. 48 kHz stereo is 4.4 times larger (see the container table in the spikes sheet).

## Tool

```
python scripts/mkvid.py -o clip.bmv --clip movie.mp4 --size 320x180 --fps 15 --quality 6 --seconds 10
python scripts/mkvid.py -o clip.bmv --jpegs frames/ --fps 15 --pcm audio.raw --rate 22050 --channels 1 --bits 16
```

`--clip` needs ffmpeg. `--jpegs` needs only Python.

## Player API (C, `src/bm/vid.h`)

| Call | What it does |
|---|---|
| `bmv_open` | Checks a clip in memory. Fills a `bmv_t`. |
| `bmv_frame` | Gives the JPEG bytes of frame i. |
| `bmv_frame_at` | Gives the frame to show at a time in ms. |
| `bmv_decode` | Decodes frame i to RGB565 into a given buffer. |
| `bmv_duration_ms` | Gives the length of the clip. |
| `bmv_audio_span` | Gives the PCM bytes for a time span. |

`bmv_decode` also copies a raw RGB565 frame and expands a raw RGB332 frame (r 3 bits, g 3 bits, b 2 bits, the top bits repeated). `jpeg_decode565` (`src/bm/jpeg.h`) writes RGB565 into a given buffer. It does not allocate a 24-bit picture.

The Lua API for cartridges is in [api/video.md](api/video.md). The screen recorder writes codec 2 and 3 chunks: see [video.md](progress/video.md).
