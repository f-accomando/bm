# API: the NES machine

Part of the game API. Index and basics: [API.md](../API.md). The machine is in C (`src/bm/nes.c`, entry
points in `src/bm/api_nes.c`); the cartridge `carts/nes` is its user interface. See
[progress/nes.md](../progress/nes.md).

| Function | Description |
|---|---|
| `nes_load(bytes)` | loads a `.nes` ROM (iNES header) and resets the machine: `true`, or `nil` and the reason (an unsupported mapper, a short file) |
| `nes_reset()` | a reset of the machine |
| `nes_frame(pad1, pad2, [render])` | runs one frame with two pads. A pad is a number of bits: A 1, B 2, Select 4, Start 8, Up 16, Down 32, Left 64, Right 128. `render` false: the picture is not drawn (a frame that is skipped; the sprite 0 hit is still computed) |
| `nes_draw(target, [w, h])` | copies the picture (256 pixels wide, 240 lines) to the top left of a render target; with `w` and `h` it is scaled to that size (nearest neighbour). Returns the lines (240) |
| `nes_to_sheet([x, y])` | copies the picture into the cartridge's sprite sheet at `(x, y)` (multiples of 8; the sheet must hold 256 x 240 pixels there), so that `sspr` draws it with a zoom |
| `nes_info()` | `{mapper, prg, chr, battery, mirroring, sram}` of the loaded ROM (sizes in bytes; `chr` 0 means picture RAM; `mirroring` is `"horizontal"`, `"vertical"`, `"one screen"` or `"four screens"`) |
| `nes_sram([bytes])` | without an argument: the battery RAM (8 KB) as a string if it changed since the last call, else `nil`; with `bytes`: puts them in it |
| `nes_debug()` | `{pc, a, x, y, s, p, ctrl, mask, status, frames, cycles, jammed, under, over, us, audio}`: the registers of the 6502 and of the picture unit, the averaged microseconds (`us`) and the sound counts (`audio`, `under`, `over`) |

The ROMs are read with `doc_list("nes")` and `doc_read(name, "nes")` (folder `/nes` at the root of the SD card;
see [system.md](system.md)). The picture is RGB565. The sound goes out through the console's output (mono, 48 kHz):
`nes_load` connects it.