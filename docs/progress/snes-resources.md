# bm SNES: resources to learn 65C816 assembly and to make SNES games

Added 2026-11-11 from the user's list. Read them to learn. Do not copy their code or data into the repository: the rule is original code or a permissive license (see [snes.md](snes.md)). Check the license of each project before you use anything from it.

| Resource | Use |
|---|---|
| `alekmaul/pvsneslib` (GitHub) | PVSnesLib: a C and assembly library to make SNES games. Use it to see how a game sets up the PPU, DMA, HDMA and sound, and as a source of small test programs for the emulator. |
| `ersanio.gitbook.io` | A written course on 65C816 assembly and on Super Mario World hacking. Use it to learn the opcodes, the addressing modes and the SMW memory map. |
| `smwdisxx` (GitHub; the owner was not given) | A disassembler of Super Mario World. Use it to read how SMW uses the NMI, the DMA and the sound. Look for the right repository before you use it. |
| `snes-ide` (GitHub; the owner was not given) | An IDE for SNES development. Look for the right repository before you use it. |

## How they help bm SNES

- **Test programs:** a small ROM built from PVSnesLib exercises one part of the machine (a mode, HDMA, mode 7). It is a better test than a commercial game, and it is legal to keep a ROM you build. A built ROM goes in `tests/snes/` only when its license allows it. The test ROM today is made by `tests/snes/make_testrom.py`.
- **Open work in [snes.md](snes.md):** the missing features (hires modes 5 and 6, mosaic, offset-per-tile, the sound echo, exact cycle costs) each need a ROM that uses them. These projects are the way to get one.
- **Speed work ([snes-cpu.md](snes-cpu.md), [snes-gpu.md](snes-gpu.md)):** the disassembly of SMW shows what the CPU does in a frame, which helps to find the hot loops for a JIT (C4) and the register writes to queue (C6).
- **Learning:** the course explains the 65C816 flags M and X, the direct page and the bank registers. These facts decide how the converter in [snes-convert.md](snes-convert.md) must handle a block.
