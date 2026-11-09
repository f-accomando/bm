# API: the PlayStation 1 machine

Part of the game API. Index and basics: [API.md](../API.md). The machine is in C (`src/bm/psx_*.c`, entry
points in `src/bm/api_psx.c`); the cartridge `carts/psx` is its user interface. See
[progress/psx.md](../progress/psx.md). The files are read in C from the `/psx` folder of the SD card (the app needs
the documents permission). A disc image is never loaded whole.

| Function | Description |
|---|---|
| `psx_list()` | the files of `/psx` as `{ {name, size, kind}, ... }`; `kind` is `"bios"` (a 512 KB file), `"exe"`, `"cue"`, `"disc"` (`.bin` `.img` `.iso`) or `"other"`; `nil` and the reason if the app is not allowed |
| `psx_boot(name)` | loads the BIOS (the first 512 KB file of `/psx`, a name like `scph1001.bin` first) and the game `name`, resets the machine: `true`, or `nil` and the reason. A PS-EXE runs without a BIOS. A `.cue` uses the file of its first `FILE` line |
| `psx_reset()` | a reset of the machine with the same game |
| `psx_frame(pad1, pad2, [render])` | runs one frame (1/60 s) with two pads (`render` false: the picture is not converted, for a frame that is skipped); a pad is bits: `0x0001` Select, `0x0008` Start, `0x0010` up, `0x0020` right, `0x0040` down, `0x0080` left, `0x0100` L2, `0x0200` R2, `0x0400` L1, `0x0800` R1, `0x1000` Triangle, `0x2000` Circle, `0x4000` Cross, `0x8000` Square |
| `psx_draw(target, [w, h])` | copies the picture (256 to 640 pixels wide, 240 or 480 lines) to the top left of a render target; with `w` and `h` it scales it to that size (nearest pixel); returns the size of the picture |
| `psx_info()` | `{bios, game, kind, disc_sectors}` |
| `psx_debug()` | the state of the machine: `{pc, ra, sp, sr, cause, istat, imask, frames, gpu_cmds, dma_runs, us_frame, us_cpu, unhandled, last_unhandled, audio}` |
| `psx_tty()` | the text a program printed with the BIOS calls (kept without a BIOS; the last 2 KB) |
