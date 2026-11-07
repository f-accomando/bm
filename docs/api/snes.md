# API: the SNES machine

Part of the game API. Index and basics: [API.md](../API.md). The machine is in C (`src/bm/snes_*.c`, entry
points in `src/bm/api_snes.c`); the cartridge `carts/snes` is its user interface. See
[progress/snes.md](../progress/snes.md).

| Function | Description |
|---|---|
| `snes_load(bytes)` | loads a ROM (`.sfc` or `.smc`, a copier header is skipped) and resets the machine: `true`, or `nil` and the reason |
| `snes_reset()` | a reset of the machine |
| `snes_frame(pad1, pad2, [render])` | runs one frame of the machine with two pads (`render` false: the picture is not drawn, for a frame that is skipped); a pad is bits: `0x8000` B, `0x4000` Y, `0x2000` Select, `0x1000` Start, `0x0800` up, `0x0400` down, `0x0200` left, `0x0100` right, `0x0080` A, `0x0040` X, `0x0020` L, `0x0010` R |
| `snes_draw(target, [w, h])` | copies the picture (256 pixels wide, 224 or 239 lines) to the top left of a render target; with `w` and `h` it scales it to that size (nearest pixel, much faster than `target_blit` with a scale); returns the number of lines |
| `snes_info()` | `{title, mapping, size, sram}` of the loaded ROM (`mapping` is `"LoROM"` or `"HiROM"`) |
| `snes_sram([bytes])` | without an argument: the battery RAM as a string if it changed since the last call, else `nil`; with `bytes`: puts them in the battery RAM |
| `snes_debug()` | what a stuck game waits for: `{pc, wai, stp, io, spc, spc_pc, inidisp, tm, nmi, frames}` (`io` the last six I/O registers read; `spc` is `"boot"`, `"run"` or `"halt"`) |
