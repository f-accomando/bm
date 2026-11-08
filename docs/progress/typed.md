# Typed hot functions (prototype)

A typed subset of Lua for the hot functions of a game. One source gives plain Lua (the types taken out) and C. The goal: native speed on the Pi Zero W where the profile shows a hot function, with the same text running in the VM.

## How it is today (prototype)

- Compiler: `scripts/bmt.py` (`bmt.py hot.bmt --lua hot.lua --c hot.c`). The language is in its docstring: types `i32`, `f32`, `f64`, `bool` and arrays of them; `local`, `for`, `if`, `return`, `break`; `sqrt` and `abs`. A type error names the line of its statement.
- Source: `tests/typed/hot.bmt` has two functions of Dark Night: `cam_blocked` and `near_pairs` (the scan of the enemy separation).
- Test: `make test-typed` (on demand; it is not in `HOST_TESTS`). It checks seven programs with a type error, a good one, and 20000 random cases: C and Lua print the same results, byte for byte. A second host (`tests/typed/bindhost.c`) calls the C from Lua through the Lua C API, as the console would, and the results are the same again.
- Measures on a PC (x86, not a Pi, three runs):

  | Way of calling | Time of a call | Against Lua |
  |---|---|---|
  | Lua (the VM) | 0.7 - 1.2 us | 1 |
  | C alone, flat arrays | 0.07 us | 10 - 17 times faster |
  | C called from Lua, with the game's tables | 0.5 - 0.9 us | 1.3 - 1.6 times faster |

## Open work

- The call through the Lua C API copies the numbers out of the game's tables, and that costs about as much as the work of a function this small. Test a design where C owns the data (one array for each field: `x`, `z`, `alive`) and Lua reads it through short accessors. Or compile the whole loop (`update_enemies`), not its pieces.
- Measure on the Pi Zero W with `profile(true)`. The PC numbers say nothing about the ARM1176 (no out-of-order, a small cache).
- `f32` for the Pi (the VFP is faster on `float` than on `double`). The test uses `f64` because Lua numbers are `double`: with `f32` the results differ in the last digits, so the test needs a tolerance.
- Missing in the language: `//` and `%`, `while`, calls between typed functions, `min` and `max`, structs. Integer overflow differs: C wraps at 32 bits, Lua at 64.
- Not done: a loader for native code in a `.bm`. The prototype stays inside the kernel (a function the console registers), so the "one cartridge type" rule holds.

## Rules (do not break)

- Keep one source for both outputs. The Lua output is the source minus the types: do not edit it by hand.
- Refuse a narrowing (a float into an `i32`). Do not cut it silently.
- Compare the C with the Lua on random cases before you use a generated function. Run `make test-typed`.
