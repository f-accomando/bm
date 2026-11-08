# Typed hot functions (prototype)

A typed subset of Lua for the hot functions of a game. One source gives plain Lua (the types taken out) and C. The goal: native speed on the Pi Zero W where the profile shows a hot function, with the same text running in the VM.

## How it is today (prototype)

- Compiler: `scripts/bmt.py` (`bmt.py hot.bmt --lua hot.lua --c hot.c`). The language is in its docstring: types `i32`, `f32`, `f64`, `bool` and arrays of them; `local`, `for`, `while`, `if`, `return`, `break`; `sqrt` and `abs`. A type error names the line of its statement.
- Source: `tests/typed/hot.bmt` has two functions of Dark Night: `cam_blocked` and `near_pairs` (the scan of the enemy separation).
- Test: `make test-typed` (on demand; it is not in `HOST_TESTS`). It checks eight programs with a type error, a good one, the loops of `tests/typed/loops.bmt` (`while`, `break` in a `while`: 1227 results) and 20000 random cases: C and Lua print the same results, byte for byte. A second host (`tests/typed/bindhost.c`) calls the C from Lua through the Lua C API, as the console would, and the results are the same again.
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

## Open work

- Data in C helps (3 to 6 times a frame), but the crossing between Lua and C is now the cost. Move the whole per-frame work over the arrays into C (the move of the enemies too, not only the scan), so that Lua sets parameters and reads results once a frame, not once for each enemy. That needs more of the language: `//`, `%`, calls between typed functions, structs.
- Measure on the Pi Zero W with `profile(true)`. The PC numbers say nothing about the ARM1176 (no out-of-order, a small cache).
- `f32` for the Pi (the VFP is faster on `float` than on `double`). The test uses `f64` because Lua numbers are `double`: with `f32` the results differ in the last digits, so the test needs a tolerance.
- Missing in the language: `//` and `%`, calls between typed functions, `min` and `max`, structs. Integer overflow differs: C wraps at 32 bits, Lua at 64.
- Not done: a loader for native code in a `.bm`. The prototype stays inside the kernel (a function the console registers), so the "one cartridge type" rule holds.

## Rules (do not break)

- Keep one source for both outputs. The Lua output is the source minus the types: do not edit it by hand.
- Refuse a narrowing (a float into an `i32`). Do not cut it silently.
- Compare the C with the Lua on random cases before you use a generated function. Run `make test-typed`.
