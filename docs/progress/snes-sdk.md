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
| `carts/shared/snescode.lua`, `carts/shared/asmbuf.lua` | the helpers of the Code tab: colours, F1 texts, errors in plain words, snippets, the text buffer (no bm API) |
| `tests/snes/code_test.lua` | the tests of the two modules above (`make test-snes-code`) |
| `tests/snes/asm_cli.lua` | a command line: `build/host/luahost tests/snes/asm_cli.lua in.s out.sfc [--title T] [--map hirom] [--sym f]` |

Test: `make test-snes-asm`, `make test-snes-code`.

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

## The app: bm SNES SDK (`carts/snes_sdk`)

- One SDK, many tabs: the SDK is ONE app with a tab bar (Code, Sprites, Map, Music, Run; F1 to F5). A new tool of the SDK is a new tab in `carts/snes_sdk/main.lua`, not a new app. The Code and Run tabs work. Map and Music say "coming soon".
- Code tab: the editor of the `.s` and `.inc` files of the project, on `snes_asm`. The files are in `/docs` (8.3 names; `doc_read`, `doc_write`). The project keeps the names of the open files and the main file in the line `--code main=... files=...` of the data between the marks. A new project opens `main.s` (from `/docs`, or new with the reset snippet).
  - Keys: Ctrl+B build, F1 help on the instruction (a short help shows in the status line when the cursor is on one), Ctrl+G go to a label (the label under the cursor, or a list; a double click on a label name goes to it), Ctrl+F find, F6 and F7 next and previous match, F8 next error, Ctrl+J snippets (reset, nmi, dma, joypad, colour; each line has its comment), Ctrl+E the files (open, new, save all, main file, close), Ctrl+W close the file, Ctrl+S save the sources (and the project when it has a name), Ctrl+Q start or stop a selection, Ctrl+C/X/V/K/D copy, cut, paste, cut line, copy line down. The mouse puts the cursor, drags a selection, scrolls with the wheel and clicks an error or a file tab. F1 in another tab opens the Code tab.
  - Build: the buffers are saved, then `snes_asm` assembles the main file (`.include` reads the open buffer, else `/docs`) and the ROM builder writes `<project name>.sfc` (or the stem of the main file) to `/docs`. The size of the ROM is 32 KB minimum. The title in the header is the name in upper case (21 characters at most).
  - Errors: `SC.plain` turns the message of the assembler into plain words with what to do (A 8 bit and a 16 bit value: `rep #$20`; X and Y: `rep #$10`; an unknown word: the nearest instruction; a label that does not exist; a branch that is too far: `brl`). The first error shows under the cursor line, all errors are in a list (file, line; click to go there).
  - The F1 texts come from `A.opcodes` (the modes, the opcodes and the lengths are read from it) and from the datasheet facts of the cycles and flags. A test checks that every opcode of the table has a text.
- Run tab: F5 (or Ctrl+B) builds the project of the Code tab and runs the ROM in the SNES core, with the picture on the left and the panel on the right. F5 in the Code tab builds and runs again. F5 in another tab shows the Run tab (a build when nothing runs). F5 when the CPU is stopped continues. F9 sets or clears a breakpoint at the cursor line (Code tab) or at the PC (Run tab, stopped) or at an address typed in (Run tab, running). A breakpoint shows as a red square in the gutter of the Code tab, and the line where the CPU stopped has a `>`. F10 steps one instruction (the first F10 on a running game pauses it). Space pauses. The machine runs only while the Run tab shows. With no breakpoint armed the loop is `snes_frame`'s: the C hot loop has no debug check.
  - Panel: A X Y S D DB PB PC P with the flags (N V M X D I Z C: capital letter set, small letter clear; emulation mode shows `-` and `B` for bits 5 and 4) and the set flags in words, the source line at the PC, the breakpoints, then a hex view with four spaces: RAM (24-bit CPU space, `snes_peek`), VRAM, CGRAM and OAM (`snes_mem`). Keys when stopped: `g` go to an address, `w` write `address=bytes` (`snes_poke`, CPU addresses), `b` toggle a breakpoint at an address, `c` clear all, Tab next memory, arrows and PgUp/PgDn scroll, Ctrl+R reset. The mouse: click a memory name, `go` or `poke`, the wheel scrolls.
  - The map of lines and addresses is `res.lines` of `A.assemble` (instruction lines only, not macro lines). A breakpoint keeps its source line: a build finds the new address. F9 builds first when a text is not the same as at the last build. Lines that move because of an edit keep the old marker line until the next build.
  - Tests: `make test-snes-run` (the helpers, with the assembler map) and `make test-studio` (the whole tab in the host SDK test, with a stand-in machine; the C core is in `make test-snes-debug`). No Pi or RGB30 run.
- Frame: the project is a `.bme` in `/bme` (Ctrl+N new, Ctrl+O open, Ctrl+S save, Esc menu). The sheet is its SHEET8. The data of the Sprites tab is text between `-- snes-sdk:begin` and `-- snes-sdk:end` in its Lua section: depth, sizes, the sub-palettes (BGR555 words) and the sub-palette of each tile. Other tabs keep their own data outside these marks.
- Sprites tab: depth 2/4/8 bpp, the size pairs 8/16, 8/32, 8/64, 16/32, 16/64, 32/64 (small or large), 8 sub-palettes of BGR555 with colour 0 clear, 5-bit channel bars with a live preview, the sub-palette of each tile on the grid, a sprite editor (pencil, fill, mouse or keys), an OAM preview (flip X, flip Y, priority, palette bits), PNG import with the quantiser (15 colours + clear), export of `.chr` (planar tiles, rows padded to 16 tiles: use a sheet 128 wide) and `.pal` (all sub-palettes, low byte first) to `/docs`.
- The pixels are indices in the sub-palette of their tile (the data); the colours of the runtime sheet come from the palette. A change of a colour repaints the sheet.
- Converters: `carts/shared/snesgfx.lua` (no UI, no bm API). Test: `make test-snes-gfx`. Host test of the app: `tests/studio/snes_sdk_host.lua` (in `make test-studio`).
- Build: the Makefile adds the converter after `main.lua` as the global `SNESGFX` (`build/snes_sdk/main.lua`; a cart has no other Lua file). The cart reads it lazily (`G`), because the global exists only after the main chunk ran.
- bm Pixel keeps its own F7 page (the first version of the same tool, kept as it is). The SDK is the place for new SNES work: do not extend bm Pixel for it.

## Rules (do not break)

- The assembler is original code. The references (PVSnesLib, the ersanio course, the SMW disassembly, libSFX, the WDC datasheet) are read only: no code and no data from them enters the repository.
- The ROM goes to a buffer that the cart gives to `snes_load`. A cart writes a ROM file to `/docs` (`doc_write`). `/sfc` is read only for carts: the SNES cart reads `.sfc` from `/sfc` and, for a ROM of the SDK, from `/docs`.
- One module `snes_asm` holds the assembler and the builder. Register a new built-in library in the same places as `snes_asm` (`src/bm/require.c`, `src/script/embed.S`, `src/rgb30/bm_embed.S`, `tests/host/libs.S`, `rgb30.mk`, `Makefile`, `scripts/mkweblibs.py`).
- The opcode test of `asm_test.lua` lists the 256 opcodes in the order of the opcode map, apart from the table of the assembler. Keep it so: the two lists check each other.
- Pass 1 decides the size of each instruction and pass 2 only writes bytes. A change to the size rules must keep the two passes equal.
- The Code tab has no Lua macros, no `.lua` blocks and no Lisp syntax (the user decided to leave them out). Do not add them to `snes_asm`.
- Each snippet of `snescode.lua` assembles by itself (with a `reset` label) and has a comment on every line. Add a snippet to the list and the test checks it.
- A new instruction or mode in `snes_asm` needs its F1 text in `snescode.lua` (the test fails without it).
- One SDK, many tabs: new SNES tools are tabs of `carts/snes_sdk`. Shared code goes in a module (`carts/shared/`), never in the interface of another app.
- Keep the converters free of any bm API, so the PC tests run them.
- The Run tab runs the machine only while it shows and never adds a check to the C hot loop; the debug panel reads through `snes_peek`, `snes_mem`, `snes_regs`, `snes_step`, `snes_break` and `snes_run`.
- Register a new shared module of the SDK (`snesrun.lua`) in the build of `$(BUILD)/snes_sdk/main.lua` as a global (`SNESRUN`), as for `SNESCODE`.
- Write the text of docs and comments in ASD-STE100.

## Open work

First, the Sprites tab (done; the rest of it is here). Then the tabs below, in this order. Do not start a tab before the one above it works.

- Sprites tab: sizes 16x32 and 32x64 (OBSEL 6 and 7), recolour of the OAM preview by sub-palette, `.pal` and tile sub-palettes as files of the project, `.chr` import, a rectangle select and animation. A `.bme` is one file: `.chr` and `.pal` go to `/docs` until the project layout has a `gfx/` folder. Remove the duplicate F7 page of bm Pixel when the user agrees.
1. Code tab (done; the rest of it is here): register `snes_sdk` in the console's app list and the Market. Typing with the pad (an on-screen keyboard) is not done: the Pi and RGB30 need a keyboard for now. A list of the local labels (`@name`) in Go to. The speed of the colours and the build on the Pi Zero W is not measured.
2. Run tab (done; the rest of it is here): the speed of the picture every frame (`snes_draw` scaled) and of `snes_run` on the Pi Zero W and the RGB30 is not measured. The DSP firmware of `snes_info().dsp` is not loaded for the SDK ROM. The save RAM is not kept. No pad keys while stopped. Only the S-CPU is debugged (SA-1 and Super FX are not).
3. Learn tab ("Impara"): original lessons in Italian and English, kept as data files in the project, with projects that work. Each lesson can link to an outside source (SNESdev Wiki, ersanio, PVSnesLib, the SMW disassembly). Use links only. Never copy their text or code.
4. Step-by-step run as a teaching tool: the registers have colours, and the one that changed shows.
5. A disassembler that follows M and X: the true flags from `snes_regs` when the CPU is stopped, and `REP` and `SEP` when it is not. It uses the opcode table of the assembler (`A.opcodes`).
6. OAM view: the table of the 128 sprites (from `snes_debug` or `snes_peek`) and boxes on the screen; a click on a box selects the sprite.
7. A watch on the writes to one address. Measure the cost on the Pi Zero W before you keep it.
8. Map and Music tabs (Phase 2 and 3).

Other open work:

- A project layout: `src/*.s`, `gfx/*.png`, `snes.bme`, with `.include` read from the project (Phase 1, steps 2 and 6).
- The assembler has no listing file, no `.struct`, no `.enum`, no anonymous labels (`+`, `-`) and no linker. Add them when a game needs them.
- Measure the speed of `A.assemble` on the Pi Zero W with a large source. The tests run on the PC only.
- Super FX opcodes (GSU) and the DSP, SA-1 modes (Phase 4).
