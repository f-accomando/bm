# SNES: Doom (Super FX 2) reverse-engineering notes

Status: 2026-11-12. Notes from the source of the original port and public documents. Nothing here is built in bm. Doom does not play in bm SNES yet: after the start and the sound load, the 65816 jumps into zero bytes at `$7E:5980` ([snes-superfx.md](snes-superfx.md) has the chip rules).

Marks: **S** = stated in the original source or its build files (read on 2026-11-12). **D** = deduced by us, not checked. Each **D** line is a lead to test, not a fact.

## Sources

| Source | What it holds | Licence and use |
|---|---|---|
| [RandalLinden/DOOM-FX](https://github.com/RandalLinden/DOOM-FX) (commit 90af514, 2022-06-19) | 65816 and Super FX assembly of Doom/FX, the section map `source/RL.SEC`, the tools that convert the WAD data, `docs/sfx.txt` and `docs/sfx.guide` | GPL-3.0. Read it to understand. Do not copy code into bm: bm keeps only original or permissive code |
| [Sunlitspace542/DOOM-FX](https://github.com/Sunlitspace542/DOOM-FX) | A fork of the same project that builds it | Same licence |
| Source backup of 2024 (archive.org, from floppy disks) | Reported to hold the sound driver and tools that the GitHub release lacks | Not read |
| [SNESdev wiki: GSU](https://snes.nesdev.org/wiki/GSU), [SnesLab GSU](https://sneslab.net/wiki/GSU) | Registers, opcode matrix, cycle times, flag rules of the Super FX | Public. Already used for the core |
| Official SNES Development Manual, Book II (Table 2-2-2) | The instruction set of the chip | Public scan |
| Ghidra module for the GSU | Not found: a search found no module, only Ghidra modules for the 65816 | Write one in SLEIGH if a disassembler is needed |

The release is partial: the repository has no sound driver source in `source/`, and one report says the GitHub release lacks tools. **D**: the bytes we run come from a retail ROM, so the tools are not needed.

## Layout of the program (S: `source/RL.SEC`, `rlinit.a`, `rl.a`)

- The engine is called REALITY_ENGINE (`RL`).
- Sections by size (hex): `ENGINE_GSUCODE` `$6C99` (the Super FX program, in ROM bank `$00` of the GSU map, load `$816B`), `ENGINE_RAMCODE` `$31AB` (65816 code that runs from WRAM), `ENGINE` `$0ADA`, `COLOUR_MAPS` `$2200`, `SINCOS` `$1B02`, `INVERSE` `$8000`, `RLSCNPIXSCALE` `$6669`, `RLSTATUS*` pictures, one pair of level sections `ExMy_A` and `ExMy_B` per level, and `MUSIC_C` with `MUSIC_DRIVER` `$2945`.
- Work RAM sections: `RAM0` (direct page, `$8B` bytes), `RAM1` and `RAM1GSUVECTORS` at `$0100` to `$0BBE`, `RAM2` at `$7E2000` (size `$E000` reserved), `RAM3` at `$7F0000` (`$D5E0` bytes), and `RAM7A` to `RAM7D` at `$700000` (cart RAM; the biggest, `RAM7D`, is `$7AD2` bytes at `$703080`).
- Tables made at build time by tools in `binaries/` (`MKSEC`, `MKTEX`, `MKRAY`, `MKTAN`, `MKARCTAN`, `MKFLOOR`, `MKRLPIXSCALE`, `RIPDOOM`) and by `convert/`: the WAD data is converted to the console formats.
- The ROM is bigger than 1 MB (sections reach `$5FFFFF` in the GSU map). **D**: it is a 2 MB cart, so the header and the ROM mapping at banks `$40` to `$5F` must follow the Super FX map exactly.

## The split of work between the 65816 and the Super FX (S unless marked)

- At start the 65816 runs `_RLInitRAMCode`: one `mvn` block move copies `ENGINE_RAMCODE` (`$31AB` bytes) from its ROM bank to WRAM at `_RLRAMCode`, which is the start of `RAM2` (`$7E2000`). So the code that the CPU runs most of the time is in WRAM, from `$7E2000` to about `$7E51AB`.
- `_RLInitGSU` fills a table of pointers (`PTRLDoTasks`, `PTRLPlayer`, `PTRLWeapons`, `PTRLDoSectors`, `PTRLBuildBSP`, `PTRLFireTarget`, `PTRLDamageObject` and more) with the low 16 bits of the entry points of the game logic when the build flag `usePATCH` is on. **D**: the CPU calls the game logic through these pointers, so one bad pointer sends the CPU to a wrong place.
- The Super FX draws the 3D view (walls, floors, sprites in the world; the BSP walk and the ray tables are its input). It sets the screen base with `sfxSCBR` from `_RLGSUChrDef`, so the picture is made in the cart RAM as character data and the CPU sends it to the PPU by DMA. **D**: this is the same split as in Star Fox, with a 2-bit colour plane count chosen for Doom.
- The 65816 runs the rules of the game (players, enemies, sectors, weapons, menus) and the sound: it talks to the sound program through `musicdrv.a` (`__SendDriverCommand`).

## What we know about the crash (`$7E:5980`)

- S: the engine RAM code ends at about `$7E51AB` (`$2000 + $31AB`), so `$7E:5980` is not inside it. Something that is not in the copied block runs there.
- D, to check in this order (each needs a trace, not a guess):
  1. The `mvn` block move from a ROM bank at `$5C0000` in the GSU map: check that the core reads the ROM bytes of banks `$40` to `$5F` for a 2 MB cart, and that `mvn` crosses from one bank to another with the right count and no early stop. A wrong copy leaves zeros in WRAM and the CPU then falls into zeros.
  2. The pointer table (`PTRL*`): the log of the 65816 at the start can show which pointer is read before it is written (bm may start the game logic before `_RLInitGSU` ends because of an interrupt or a wait on the GSU).
  3. The Super FX stop (`STOP`, `GO` flag and the interrupt of the GSU): the CPU waits for it, and a core that never raises the IRQ or the flag in time makes the CPU run on.
  4. A return from a call through the stack: the stack lives in WRAM bank 0 (`$0000` to `$1FFF` mirror of `$7E`), where `RAM1` has game data at `$0100` to `$0BBE`.
- Searched in the source on 2026-11-12: no text holds `$5980`, and no routine that builds code into WRAM was found (words searched: stub, generate, self-modify, build code). The pointer table at `$7F:4F72` is inside `RAM3` (`$7F0000`, `$D5E0` bytes). The next step is to find which routine reads that table in the ROM trace, then match it to a name in the source.
- The earlier note in [snes-superfx.md](snes-superfx.md) says the stubs are written by a DMA from the cart RAM. **D**: this fits item 1 only if the engine copies code another way too. Look for it in the trace.

## Differences from Doom for DOS (S for the data, D for the rest)

- The map data is converted: one pair of sections per level, and the textures, the floors and the sprites are converted by the tools into tables for the Super FX.
- The ray, tangent, arctangent and distance tables are made at build time (`MKRAY`, `MKTAN`, `MKARCTAN`, `RLSCNPIXSCALE`, `INVERSE`), because the chip has no divide and a small multiply.
- **D**: the view is smaller than the DOS view, the world is drawn without the lights of the DOS engine (it uses `COLOUR_MAPS`), and the number of enemies is lower. Not checked in the source.

## Open work

- Run the 4 checks above on the real ROM in the host build with a trace of the 65816 from the reset to the first jump to `$7E:5980`.
- Compare the section map of a retail ROM with `RL.SEC` (the retail build may differ from the source of 1995).
- If a disassembler is needed for the Super FX code of the ROM: write a SLEIGH module or a small script on the opcode table of [snes-superfx-isa.md](snes-superfx-isa.md).
- Read the sound driver in the archive.org backup if the sound or its timing is a suspect.
