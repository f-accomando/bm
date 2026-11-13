# bm SNES SDK

Area sheet for the SNES SDK of bm: tools to make SNES games inside bm, in 65C816 assembly, with the keyboard and the mouse.
Back to [snes.md](snes.md) (the emulator that runs the ROMs). Phase 1 of the plan is the assembler, the ROM builder, the code tab, the debug API, the sprite tab and the templates. This sheet covers the assembler and the ROM builder. Other sessions do the debug API and the sprite tab.

## What is in it

| File | Use |
|---|---|
| `src/script/snes_asm.lua` | the assembler and the ROM builder (`require "snes_asm"`; built into the kernels, the host tools and the web player) |
| `tests/snes/asm_test.lua` | unit tests: all 256 opcodes, the addressing modes, width tracking, expressions, directives, macros, errors, the builder |
| `tests/snes/testrom.s`, `tests/snes/asm_rom_test.py` | the program of `make_testrom.py` in assembly; the test compares the bytes and runs the ROM in the core |
| `tests/snes/asm_run.c` | the host harness: it runs a ROM for N frames and checks the NMI counter and the backdrop colour |
| `tests/snes/asm_cli.lua` | a command line: `build/host/luahost tests/snes/asm_cli.lua in.s out.sfc [--title T] [--map hirom] [--sym f]` |

Test: `make test-snes-asm`.

## The API

```lua
local A = require("snes_asm")
local res = A.assemble(source, { name = "main.s", read = function(path) return text or nil end })
if not res.ok then print(A.format_errors(res)) end        -- "main.s:12: message", one per line
local rom, info = A.build_rom(res, { title = "MY GAME" }) -- a string; give it to snes_load(rom)
local text = A.symbol_table(res)                          -- "$00:8000 reset", "= $10 counter"
local rom, res = A.build(source, opts, header)            -- both steps; on an error: nil, text, res
```

- `res.segments`: the code blocks `{ addr, data }`. `res.symbols`: name to value. `res.symlist`: the sorted list. `res.errors`: `{ file, line, msg }`.
- `read(path)` gives the text of `.include` and the bytes of `.incbin`. The default reads with `io.open` (the host tools only). A cart gives its own reader (for example `doc_read`).
- `A.opcodes` and `A.lengths` are the tables of the assembler (read only). A disassembler can use them.
- Header options (`build_rom`): `title`, `map` (`lorom` or `hirom`), `fast`, `sram_kb`, `battery`, `region`, `dev_id`, `version`, `chipset`, `fill`, `min_kb`, `vectors`.
- Vectors: `reset`, `nmi`, `irq`, `cop`, `brk`, `abort`. The builder reads the label of the same name (also in upper case). `vectors = { nmi = "my_nmi" }` or a number sets one. `emu_nmi`, `emu_irq`, `emu_cop`, `emu_abort` set the emulation vectors. `reset` is required. A missing vector points to an RTI that the builder writes at `$00:FFE0`.
- The ROM size is a power of two (32 KB minimum for LoROM, 64 KB for HiROM, 8 MB maximum). The builder writes the header at `$FFC0-$FFDF` of bank 0 and the vectors at `$FFE4-$FFFF`. It writes the complement and the checksum last.

## The language

- Case: mnemonics and directives ignore the case. Labels keep it. Comments start with `;`.
- Labels: `name:`. A local label is `@name:`; it belongs to the last global label. Many labels can stand on one line. An equate is `name = expression` (it can refer to a label defined later).
- Numbers: `$FF`, `0xFF`, `%1010`, `123`, `'A'`. `*` is the address of the instruction.
- Operators: `+ - * / %`, `<< >> & | ~` (`~` is xor between two values and NOT before one), `== != < > <= >=`, `&& ||`, `!`. Prefix operators: `<` low byte, `>` high byte, `^` bank byte. They take the whole expression after them: `#<table+1` is the low byte of `table+1`.
- Directives: `.org`, `.byte .word .long .dword` (also `.db .dw .dl .dd`), `.res n[,fill]` (`.ds`), `.align n[,fill]`, `.incbin "file"[,offset[,length]]`, `.include "file"`, `.a8 .a16 .i8 .i16`, `.lorom .hirom`, `.macro name p1, p2` ... `.endmacro`, `.rept n` ... `.endr`, `.if expr`, `.ifdef`, `.ifndef`, `.else`, `.endif`, `.error "text"`.
- Macros: the parameter names are replaced as whole words in the body. `\@` gives a number that is new for each call: `loop\@:`. A macro can call a macro (16 levels).
- Addresses are 24 bits. `.org $8000` is bank 0. `.org $018000` is bank 1. A `.res` leaves a gap that the builder fills with the padding byte.
- Width tracking: the assembler tracks M and X in the order of the source. `.a8 .a16 .i8 .i16` set them. `REP #const` and `SEP #const` set them too (the operand must be a constant). The state at the start is 8 bits for both. The width decides the size of `lda #`, `adc #`, `and #`, `bit #`, `cmp #`, `eor #`, `ora #`, `sbc #` (M) and of `ldx #`, `ldy #`, `cpx #`, `cpy #` (X). `rep`, `sep` and `pea` do not depend on the state. `xce`, `plp` and `rti` do not change the state in the assembler: use `.a16` after them.
- Operand size: a value known in pass 1 picks the smallest form (direct page, absolute, long). A symbol that is not defined yet is absolute. Force a size with `z:` (direct page), `a:` (absolute, keeps the low 16 bits), `f:` (long) before the operand, or with `.b .w .l` after the mnemonic (`lda.l label`). `jmp` and `jsr` to an address in the bank of the instruction use the absolute form; another bank uses the long form. `jml` and `jsl` are always long.
- Special: `brk`, `cop` and `wdm` are two bytes (`brk` gives signature 0; `brk #n` sets it). `mvn src,dst` and `mvp src,dst` are written source first. `pea` takes `$1234` or `#$1234`. Aliases: `bge` `blt` `ina` `dea` `tad` `tda` `tas` `tsa` `swa`.
- Errors: each has the file and the line. An error in a macro also names the macro and the line of the call. The assembler reports up to 200 errors.

## Sprite page (bm Pixel, F7)

- Converters: `carts/pixel/src/snesgfx.lua` (no UI, no bm API). Test: `make test-snes-gfx` (`tests/snes/snesgfx_test.lua`: known tile bytes for 2, 4 and 8 bpp, round trips, BGR555 edge values, OAM bytes, quantiser).
- The build adds the module after `main.lua` as the global `SNESGFX` (`build/pixel/main.lua`, rule in the Makefile; a cart has no other Lua file). The page calls `SNESGFX or require "snesgfx"`; the host test uses `require`.
- The page: depth 2/4/8 bpp, size pairs 8/16, 8/32, 8/64, 16/32, 16/64, 32/64 (small or large), 8 sub-palettes of BGR555 (colour 0 clear), 5-bit channel bars with a live 8-bit and 5-bit preview, the sub-palette of each tile on the grid, an OAM preview (flip X, flip Y, priority, palette bits), PNG import with the quantiser (15 colours + clear), and export of `.chr` and `.pal` to `/docs`.
- `.chr`: planar tiles in the SNES order, tile rows padded to 16 tiles (the OAM name table is 16 tiles wide): use a sheet 128 pixels wide. `.pal`: all sub-palettes, 2 bytes a colour, low byte first.
- The tab shows only while the page is open (the bar has no room for a seventh tab); the key is F7 and the menu has a row.

## Rules (do not break)

- The assembler is original code. The references (PVSnesLib, the ersanio course, the SMW disassembly, libSFX, the WDC datasheet) are read only: no code and no data from them enters the repository.
- The ROM goes to a buffer that the cart gives to `snes_load`. A cart writes a ROM file to `/docs` (`doc_write`). `/sfc` is read only for carts: the SNES cart reads `.sfc` from `/sfc` and, for a ROM of the SDK, from `/docs`.
- One module `snes_asm` holds the assembler and the builder. Register a new built-in library in the same places as `snes_asm` (`src/bm/require.c`, `src/script/embed.S`, `src/rgb30/bm_embed.S`, `tests/host/libs.S`, `rgb30.mk`, `Makefile`, `scripts/mkweblibs.py`).
- The opcode test of `asm_test.lua` lists the 256 opcodes in the order of the opcode map, apart from the table of the assembler. Keep it so: the two lists check each other.
- Pass 1 decides the size of each instruction and pass 2 only writes bytes. A change to the size rules must keep the two passes equal.
- Keep the SNES converters in `snesgfx.lua` and the page in the table `snp` of `carts/pixel/main.lua` (the main chunk has 200 locals at most).
- Write the text of docs and comments in ASD-STE100.

## Open work

- Sprite page: sizes 16x32 and 32x64 (OBSEL 6 and 7), recolour of the OAM preview by sub-palette, `.pal` and tile sub-palettes saved in the project (now: export and re-read with shift L), `.chr` import. A `.bme` is one file: `.chr` and `.pal` go to `/docs` until the project layout has a `gfx/` folder.

- Code tab on `carts/code`: a 65C816 language mode with the errors of `A.assemble` (Phase 1, step 3).
- A project layout: `src/*.s`, `gfx/*.png`, `snes.bme`, with `.include` read from the project (Phase 1, steps 2 and 6).
- The code tab: another session. The sprite page is done (see above). The debug API is done (`snes_peek`, `snes_poke`, `snes_regs`, `snes_step`, `snes_break`, `snes_run`; see [snes.md](snes.md) rules).
- The assembler has no listing file, no `.struct`, no `.enum`, no anonymous labels (`+`, `-`) and no linker. Add them when a game needs them.
- A disassembler that follows M and X (Phase 2), built on `A.opcodes`.
- Measure the speed of `A.assemble` on the Pi Zero W with a large source. The tests run on the PC only.
- Super FX opcodes (GSU) and the DSP, SA-1 modes (Phase 4).
