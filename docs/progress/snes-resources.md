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

## NES games on a SNES (idea, not started)

Added 2026-11-11 from the user's notes. The descriptions below are the user's, not checked by reading the projects. Nothing is built. The idea is to run NES games on a SNES (a real console, or the bm SNES core) by translating the 6502 code of a NES game to 65C816 code, and by converting its graphics and sound to the SNES hardware. The same rules apply: read, do not copy, check the license.

| Resource | Use |
|---|---|
| [Project NESted](https://github.com/Myself086/Project-Nested) | Runs NES games on a SNES with no change to the hardware. It uses JIT and AOT recompilation from 6502 to 65C816 and converts the NES graphics and audio. It is the main example. |
| [upernes](https://github.com/mandraga/upernes) | A NES to SNES recompiler. It shows the 6502 to 65C816 translation and the conversion of NES tiles to the SNES format. It is a second approach, to compare with Project NESted. |
| [WDC W65C816S datasheet](https://www.westerndesigncenter.com/wdc/documentation/w65c816s.pdf) | The official sheet of the 65C816: native and emulation mode, registers, instructions, addressing, 8-bit and 16-bit operation. |
| [6502.org](https://www.6502.org/) | The 6502: opcodes, undocumented instructions, particular behaviours. Use it to compare the 6502 of the NES with the 65C816. |
| [NESdev Wiki](https://www.nesdev.org/wiki/) | The NES hardware: the 2A03 CPU, the 2C02 PPU, the APU, cartridges and mappers, timing, memory, tiles, sprites, palettes. |
| [SNESdev Wiki](https://snes.nesdev.org/wiki/) | The SNES hardware: PPU, DMA, HDMA, graphics modes, memory, peripherals. It tells which NES functions the SNES hardware can reproduce. |

Order of reading: Project NESted and upernes for the practical examples, then the WDC sheet and the two wikis for the differences between the machines.

How it connects to bm:
- **The converter of [snes-convert.md](snes-convert.md)** goes from 65C816 to ARM. A NES-on-SNES translator goes from 6502 to 65C816: the problems are the same (a block of code, flags, timing), in the other direction. Read the two projects for how they find the blocks and how they keep the cycle counts.
- **The bm NES core** (`core/src/bm/nes.c`, the cart in `bm/nes/`) already runs the same games on the Pi: it is the reference to compare a translated game against, frame by frame.
- **Open questions** before any work: what the target is (a real SNES, the bm SNES core, or both), the license of each project, and what part of NES hardware (the APU, the mappers) a SNES cannot reproduce.
