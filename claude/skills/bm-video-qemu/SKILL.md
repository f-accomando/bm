---
name: bm-video-qemu
description: Records a bm tutorial episode on the REAL kernel in QEMU (-M raspi0) at exactly 60 fps of guest time, from the files an episode's record.py writes (out/input.txt, out/overlay.json, out/sd/), then builds the 1920x1080 page with the key overlay and subtitles. Use it whenever a video of the console must be recorded in the cloud, a recording looks wrong or fails verify.py, a cloud container killed a recording, or a new episode must be added to video/3d/rec_qemu.py.
---

# bm-video-qemu: record an episode in QEMU

Tools: `video/lib/qemurec.py` (the recorder) and `video/3d/rec_qemu.py NN` (an episode). Needs `qemu-system-arm`, `Xvfb`, `ffmpeg`,
`dosfstools`, `mtools`, `arm-none-eabi-gcc` (`apt-get update && apt-get install -y qemu-system-arm gcc-arm-none-eabi dosfstools mtools`).
Build first: `make build/kernel.img` (the kernel holds the editors; after a `git pull` delete `build/{editor,studio,...}.bm` and rebuild).

## Run

1. `python3 video/3d/NN-name/record.py --no-video` (bmhost, seconds): writes `out/input.txt`, `out/overlay.json`, `out/sd/carts/*`, the answer of `verify.py`.
2. `python3 video/3d/rec_qemu.py NN --shift 1` -> `out/qemu/{console.mkv, overlay.ass, subs.srt, epNN_qemu.mp4, sd/}`.
   Tests: `--limit-s 40` (first 40 s of the script), `--part 2 --start-file X.bme` (one tool of a several-tool episode, `PARTS` in rec_qemu.py).
3. `python3 video/3d/NN-name/verify.py out/qemu/sd/carts/TROPIC.BME` (episode 5: a folder with `sd/`, `sound.wav`, `events.json`).
4. Chain: copy the saved project to `video/3d/chain/NN-name.bme` and export the `.bm` as the episode's record.py does.

## What is true (measured; do not rediscover)

- **Always `--shift 1`** (icount, 2 ns per instruction: the guest makes 60 fps, loses ~2%). `shift 2` loses ~25% of the frames even in the menu.
  Cost on the 4-core cloud host: ~15x real time in editors (3.5 min of video = ~55 min). Two episodes in parallel are fine (`REC_DISPLAY` = one X display each).
- **Keys go over the serial line** (`serial_bytes` in qemurec.py: the kernel's typed-key queue, the same as bmhost's `type`). The emulated
  USB keyboard loses presses at random and merges a release and the next press into one. `--usb` keeps it (Ctrl+Shift chords and Ctrl+Esc use it).
- Uppercase letters (Shift+P) are just the character `P` on the serial line: no Shift chord needed.
- QEMU == bmhost: the same `input.txt` gives the byte-identical saved file. If `verify.py` fails in QEMU, run it on the bmhost output first:
  a failure there too is the script's (or the start project's), not the recorder's. Scripts must be made on the REAL chain, not a stand-in.
- The guest clock is the BCM2835 system timer read by the monitor (`xp`); frames are stamped with it, so the video is exact whatever the host speed.
- QEMU's Pi has no audio out: the sound is bmhost's `out/sound.wav` (same synth, same keys), muxed with the intro delay by `rec_qemu.py`.
- Menu walk: Tab = Dev tab; icons (Right presses from the first): SDK 0, Sound 1, Studio 2, Animator 3, Mesh 4, Pixel 5, Code 6, Assistant 7.
  Between tools: Ctrl+Esc = home (Dev tab, last icon selected), Left/Right, Enter.
- The kernel reads `bm/config.txt` on the SD: the recorder writes `layout=us`.
- Stats (`N/300 frames`, `dropped`) print on the serial line when a tool is left to the monitor (Ctrl+Shift+Esc).

## Cloud container rules (they cost two lost recordings)

- The container is recycled after ~30 min without session activity and kills background jobs: keep the turn alive with foreground waits
  (`for i in $(seq 29); do sleep 20; grep -q 'video:' log && break; done`, timeout 590000) until the recording ends.
- Never `kill $(pgrep qemu-system-arm)` or `pkill -f name` (it kills other recordings or your own shell): kill by the PIDs you started.
- `out/` is not in git: a recycled container loses finished mp4s. Send finished videos with SendUserFile.
- Write a `HEARTBEAT` row in SYNC.md every 30 min (rules of 2026-10-14).

## Checks before trusting a recording

`late_keys: 0` in the `recorded:` line; `ffprobe` = 1920x1080 at 60 fps and frames = script + intro; a frame at a shot time against
`out/shots/NN.png` of bmhost (compare the 16x16 cells, not pixels: RGB565 differs a little); `verify.py` PASS.
