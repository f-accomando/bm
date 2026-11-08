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
- Yharnam with `blocked` in C (`yharnam_typed`, default on): 53.9 fps against 50.5 with the Lua, 0 differences in 80000 calls. Frame 16.8 to 15.6 ms.
- Typed C against Lua on the Pi (`tests/typed/pi_bench`, `src/bm/api_typed.c`): `blocked` 13.3 times, 29.5 times with the loop in C; `solid_at` 4.5 times; `fdiv` 1.7 times. A call from Lua to C costs about 1 us. Same results as Lua.

## Open work

Roadmap of a language of bm, a small start. The Lua output stays the reference, and a test compares it with the C.

1. Done: `blocked` of Yharnam in C (hand-written, in `src/bm/api_typed.c`), 50.5 to 53.9 fps on the Pi. Next: generate that C with `bmt.py` (it needs `//`, `%` and calls between typed functions), so that the C comes from the same source as the Lua.
2. Look at the next hot spot of Yharnam: `draw_scene` (3.7 ms, 13 with its calls). It draws the objects one by one; the loop may go to C with `spr` and `sspr` called from C.
3. Measure the RGB30 with the same runs.
4. Complete the subset: `//`, `%`, `min`, `max`, calls between typed functions, structs. Decide the integer overflow first (C wraps at 32 bits, Lua at 64).
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
