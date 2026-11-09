# Typed hot functions (prototype)

The reasons, all the tests and the numbers, the benchmark of the C+Lua hybrid and the trial of Pallene are in [`docs/design/typed-native.md`](../design/typed-native.md). This sheet keeps the state and the rules.

A typed subset of Lua for the hot functions of a game. One source gives plain Lua (the types taken out) and C. The goal: native speed on the Pi Zero W where the profile shows a hot function, with the same text running in the VM.

## How it is today (prototype)

- Compiler: `scripts/bmt.py` (`bmt.py hot.bmt --lua hot.lua --c hot.c`). The language is in its docstring: types `i32`, `f32`, `f64`, `bool` and arrays of them; `local`, `for`, `while`, `if`, `return`, `break`; `sqrt` and `abs`. A type error names the line of its statement.
- Source: `tests/typed/hot.bmt` has two functions of Dark Night: `cam_blocked` and `near_pairs` (the scan of the enemy separation).
- Test: `make test-typed` (14 checks, on demand; it is not in `HOST_TESTS`). It checks eight programs with a type error, a good one, the loops of `tests/typed/loops.bmt` (`while`, `break` in a `while`: 1227 results) and 20000 random cases: C and Lua print the same results, byte for byte. A second host (`tests/typed/bindhost.c`) calls the C from Lua through the Lua C API, as the console would, and the results are the same again.
- `update_parts` of Yharnam (160 particles, `tests/typed/parts.bmt`, `parts_bench.lua`): Lua tables 17 - 24 us, typed C on arrays of C 0.96 - 1.2 us (16 - 22 times, the draw not included); Pallene on the tables of the game 1.2 - 1.9 times. Same count and sums.
- Measures on a PC (x86, not a Pi, three runs):

  | Way of calling | Time of a call | Against Lua |
  |---|---|---|
  | Lua (the VM) | 0.7 - 1.2 us | 1 |
  | C alone, flat arrays | 0.07 us | 10 - 17 times faster |
  | C called from Lua, with the game's tables | 0.5 - 0.9 us | 1.3 - 1.6 times faster |

- A whole frame of 17 enemies (`tests/typed/frame.lua`: Lua moves them, the pairs closer than 0.8 are found, Lua reads the hits; the sums are equal in all three ways). The data of the enemies are in C (one array for each field, `E.get`, `E.set`, `E.near`, `E.near_all`). Three runs on a PC:

  | Way | Time of a frame | Against tables |
  |---|---|---|
  | A: tables in Lua, as the game | 15 - 20 us | 1 |
  | B: data in C, one `E.near` call for each enemy | 3 - 4 us | 3.7 - 6 times faster |
  | C: data in C, one `E.near_all` call | 4 - 5.5 us | 3.2 - 4 times faster |

  The move of the enemies alone (34 calls of `E.get` and `E.set`) costs 2.4 - 3.3 us. With tables it costs 1.3 - 2.5 us. So in way C more than half of the frame is the crossing between Lua and C, not the scan. The numbers vary between runs (a PC shared with other work): read the ratios, not the digits.

## Pallene (read on 8 Nov 2026, not run)

Pallene is a typed subset of Lua that compiles to C (`github.com/pallene-lang/pallene`, MIT text, last commit 3 Nov 2026). What the code shows:

- It vendors Lua **5.4.7**, the version of `third_party/lua`.
- The generated C reads the array part of a Lua table in place (`arr->array[i - 1]`, after a check of the size) and tests the tag of the value. It copies nothing. This is the cost that the prototype shows as the crossing between Lua and C. It needs the internal headers of Lua, which bm has.
- The compiler is 11k lines of Lua: parser, type checker, an IR in basic blocks (`while` and `repeat` lower to jumps), checks of flow and of uninitialized values, constant propagation, a pass for the GC roots, and the C generator (`coder.lua`, 2000 lines).
- A record is a userdata, not a table. A module is a C library that Lua loads with `require`: the kernel has no dynamic loader, so a module must be linked into the kernel.
- To try: build the compiler with the Lua of `third_party/lua`, compile the two functions of `hot.bmt` as Pallene, link them into `bindhost.c`, and time the frame of `frame.lua` against way C.

## Pallene trial (8 Nov 2026)

Built with `tests/typed/pallene/setup.sh` and run with `tests/typed/pallene/run.py` (the build needs the Git mirrors of Pallene, Lua 5.4.7 and LPeg). The 20000 random cases give the same result as Lua. On the frame of 17 enemies (six runs): arrays of numbers 1.3 - 2.6 times faster than Lua tables, tables as the game 0.9 - 1.5 times, move and scan in Pallene 0.9 - 1.6 times. The bmt way with data owned by C gives 3.2 - 6 times. Pallene shows a small gain on this scene when the data stay in the tables of the game.

## Pi Zero W measures (8 Nov 2026)

Clean kernel `v0.2.5-2137-g4ff425ca`, the V3D on (`3d=gpu`). Reports in the branch `reports`, folder `claude-darknight-pi-perf`. Details: section 3 of the design document. The first runs of that day had `gpu3d=0` in `bm/config.txt` and are not valid.

- Yharnam, autoplay, 7199 frames: 49.7 fps, 17.2 ms a frame. The Lua is a large part: `draw_scene` 3.9 ms (13.4 with its calls), `fdiv` 2.2, `blocked` 1.1 (3.2). `update_parts` is not in the first ten rows.
- Dark Night, a walk with the camera turning, 3595 frames: 59.0 fps, 7.0 ms a frame. The Lua functions are 0.25 to 0.65 ms each.
- Result: road B has a case in Yharnam (small functions called thousands of times) and none in Dark Night. The 16 to 22 times of `update_parts` on a PC says little: that function is not the cost.
- The machine is fuzzed (`tests/typed/vmfuzz.c`): the first version had a loop without the budget (a conditional jump backward); the loader now refuses it.
- Two ways without an API for each game, measured on the Pi (design document, section 3): `collmap` (the general C: 5.1 us against 29.4 us of Lua, 1.3 us more than the C made for Yharnam) and the `bmvm` machine (14.9 us, 2 times the Lua, data in the cartridge, safe by construction).
- Yharnam with `blocked` in C (`yharnam_typed`, default on): 53.0 fps against 50.5 with the Lua, 0 differences in 60000 calls. It uses the general `collmap` since 9 Nov 2026; the C made for Yharnam (`typed_y*`, 53.9 fps) and the six fixed `typed_*` functions are removed from the kernel.
- Typed C against Lua on the Pi (8 Nov 2026, with the fixed functions that are now removed): `blocked` 13.3 times, 29.5 times with the loop in C; `solid_at` 4.5 times; `fdiv` 1.7 times. A call from Lua to C costs about 1 us. Same results as Lua.

## A language of bm (learning project, started 9 Nov 2026)

Full manual and specification: [`docs/design/bml-manual.md`](../design/bml-manual.md). The target is native code for the ARMv6 (ARM1176, 32 bit, A32) of the Pi Zero W. The RGB30 (AArch64) is out of this project: the `targets` field of the `.bm` header says "Pi" for a cartridge that carries this code.

Decisions of the user:
- The syntax is the syntax of Lua (the typed subset of `bmt.py`). One source runs in the Lua VM and in the new compiler. The Lua result is the reference of every test.
- The first back end is a bytecode and a virtual machine. The ARM back end follows and stays the goal.
- The compiler is `tools/bml/bml.py` (Python), separate from `scripts/bmt.py`. It does not replace `bmt.py`.

Stages. Each stage ends with a test that compares the result with the Lua result (`make test-bml`, `tests/bml/run.py`):
1. **Front end (done).** Lexer, parser, syntax tree, type check in `tools/bml/bml.py`. Types `i32` and `bool`, `local`, `if`/`elseif`/`else`, `while`, `break`, numeric `for`, `return`, `abs`/`min`/`max`, functions that call functions. Error messages name the line (23 negative tests + `tests/bml/sum.b` and `tests/bml/features.b`).
2. **Bytecode and VM on the host (done).** `BytecodeCompiler` and `StackVM` in `tools/bml/bml.py`. `sum(10)` gives 55; `sum(-5..100)`, `tests/bml/features.b`, and 200 random programs (1000 calls) give the same result as Lua.
3. **VM of the kernel (done).** `BMVMCompiler` (`--bmvm`) lowers the checked IR to a `BMV1` register module for `src/bm/bmvm.c` (`vm_load`, `vm_run`). Tested on `sum`, `features`, and 200 random programs in C against Lua.
4. **Back end ARMv6 (done).** `ARMv6Assembler` and `ARMv6Compiler` (`--armv6`, `--dump-armv6`) emit 32-bit A32 words (AAPCS: `r0`-`r3` args, `r0` result, `r4`-`r10` register allocator + `[fp, #-off]` spills, `__bml_idivmod` helper, and the exact 9-word leaf encoding for `sum`). Tested on `qemu-system-arm -cpu arm1176`: all 1,559 calls (`sum`, `features`, 200 random programs) match Lua.
5. **Load on the Pi (done).** `compile_native_blob` (`--native`) packs the `BMN1` module (`0x314E4D42`). `src/bm/bmnative.c` and `src/bm/api_vm.c` expose `native_load`, `native_fn`, and `native_run`: `bmnative_load` validates the A32 words, copies them into 32-byte-aligned executable RAM, cleans the D-cache (`dcache_clean_range`), and invalidates the I-cache (`mcr p15, 0, a, c7, c5, 1`) and branch target cache (`c7, c5, 6`) + `DSB`/`ISB`. Verified in Lua (`native_load`/`native_run`) and on bare-metal ARM1176 QEMU (including self-modifying cache flush test).
6. **Types and memory (next).** Arrays, then structs. Decide the overflow of `i32` first: C wraps at 32 bit, Lua at 64 bit (`StackVM` supports `--wrap32`). Then `f32` with the VFP of the ARM1176.
7. **Standard functions and bm.** A table of kernel functions (`spr`, `rectfill`, `btn`, math) that the native code calls. Strings and tables stay out until this works.
8. **Games.** Write the movement of the enemies of Dark Night in the language. Measure on the Pi.
9. **On the console.** Run the compiler on the Pi itself (a large project, last).

The first program (`tests/bml/sum.b`), tested in stages 1 and 2:

```
function sum(n: i32): i32
  local s = 0
  for i = 1, n do s = s + i end
  return s
end
```

The ARMv6 words that stage 4 must write for it (verified with `arm-none-eabi-objdump` and `run_armv6_words` on `-5..100`): `E3A01000 E3A02001 E1520000 CA000002 E0811002 E2822001 EAFFFFFA E1A00001 E12FFF1E`. Result for `sum(10)`: 55.

## Open work

Stages 1 through 5 of `tools/bml/bml.py` are complete (`make test-bml`); stage 6 (arrays, structs, `i32` overflow rule, `f32` VFP) and stage 7 (kernel function table) are next. For `scripts/bmt.py`, the Lua output stays the reference and `make test-typed` compares it with the C.

1. Done: `blocked` of Yharnam is made by `bmt.py` (`tests/typed/yblocked.bmt`): 50.5 to 53.5 fps on the Pi, the same as the hand-written C. The kernel now has the general form (`tests/typed/collmap.bmt`, `src/bm/typed_collmap_gen.h`). The subset has `//`, `%`, `floor`, `min`, `max` and calls between typed functions. Next: structs, and a table of the cols that the game fills once (today `typed_ysync` copies a chunk each time).
2. `draw_scene` of Yharnam: the cull in C gave nothing (53.1 against 53.5 fps), and its cost is spread over the sort comparator, the cull, the building tiles and the draw calls (design document, section 3). Do not port a part of it. Options: the whole draw of the objects in C, or a cheaper sort (the comparator is 7.3% of the Lua instructions).
3. Measure the RGB30 with the same runs.
4. Complete the subset: structs. Decide the integer overflow first (C wraps at 32 bits, Lua at 64).
5. Keep the data in C and call once a frame. Move the draw of the particles into C too, or the accessors take the gain back.
6. Link the C into the kernel (a new API file in `src/bm/`, the table in `runtime.c`, the API in `docs/api/`). Write a test cartridge that runs the Lua and the C on the same scene and logs both times.
7. Write the movement of the enemies of Dark Night in the language. Measure the frame on the Pi and on the RGB30. Decide road B with these numbers.
8. Later and optional: `f32` for the VFP of the Pi (the test needs a tolerance) and a compiler on the console (a large project).

Other open points:

- Try Pallene on a heavier loop and keep both roads open: the C+Lua hybrid is the baseline (section 3 of the design document).
- Not done: a loader for native code in a `.bm`. The prototype stays inside the kernel (a function the console registers), so the "one cartridge type" rule holds.

## Rules (do not break)

- Keep one source for both outputs. The Lua output is the source minus the types: do not edit it by hand.
- Refuse a narrowing (a float into an `i32`). Do not cut it silently.
- Compare the C with the Lua on random cases before you use a generated function. Run `make test-typed`.
- The language of bm (`tools/bml/`) uses the syntax of Lua. Test every stage against the Lua result before the next stage.
