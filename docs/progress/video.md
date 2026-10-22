# Video: clips for cartridges and screen recording

Study: [video-feasibility-2026-10.md](../bench/video-feasibility-2026-10.md). Overview: [progress.md](../progress.md).

## Rules (do not break)
* The Pi Zero W (RGB565, no 32-bit colour) is the benchmark.
* Mali paths on the RGB30 stay untouched.
* The first codec is MJPEG with `src/bm/jpeg.c` (baseline only). Then RLE+delta palette. MPEG-1 comes last.

## Open work
* VID-S3, VID-S4: PC spikes (compression ratio, ffmpeg recipe).
* VID-S1, VID-S2: hardware benchmarks (SD write speed, decode time).
* VID-FMT: container and player API.
* VID-REC: chunked screen recording. It depends on VID-S1.
