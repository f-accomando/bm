# Typed hot functions and a language of bm: considerations and development

This document keeps the reasons, the tests and the numbers of the work on native code for the games of bm (8 Nov 2026). It has two jobs. It is the **benchmark** of the C+Lua hybrid that bm has today. It is the **record** of the road to a typed language that compiles to C. The status sheet is [`docs/progress/typed.md`](../progress/typed.md). Both roads stay open and run in parallel.

## 1. Why

1. **Performance.** The Raspberry Pi Zero W is slow. bm already left Linux and Python for C and Lua. Native code for the hot parts of a game is the next step.
2. **A reason to choose bm.** bm is bare metal on purpose. Arduino and Pico users write C or assembly. A readable language that compiles to native code on a bare-metal platform, with a game API, is something that others do not offer. This reason is about the product, not about speed. It needs a language that is complete.

## 2. Two roads, in parallel

| Road | What it is | State |
|---|---|---|
| **A. C+Lua hybrid** | Games in Lua. The hot parts are C functions of the kernel (`spr`, `map`, `hit3d`...). Lua fixes come first, a new C function comes after the profile asks for it. | Works today. Its results are in section 3 and are the baseline. |
| **B. Typed language** | A typed Lua-like source that gives plain Lua and C. Own compiler (`scripts/bmt.py`), or Pallene, or a mix. | Prototype. Sections 5 to 7. |

Rule: road B is judged against road A with the same scene and the same sums. A new way must beat the hybrid, or give something that the hybrid cannot give.

## 3. Benchmark: the C+Lua hybrid

**Method.** `bmhost` on a PC (x86, not a Pi). `BMHOST_LUAPROF=1` counts the Lua VM instructions by function and by line. A script of keys plays the game (`tests/darknight/check.py` for Dark Night; the autoplay of Yharnam with `BMHOST_CONFIG=yharnam_autoplay=1`). The profile does not see the time in the C functions (`rectfill`, `spr`, the 3D). The ms of a PC do not carry to a Pi: compare the samples. Not measured here: any number on a Pi or on the RGB30.

**Yharnam** (60 s, 3600 frames): 1.457 ms a frame, worst 8.238 ms. 102,518 samples.

| Share | Function | What it does |
|---|---|---|
| 38.7% | `draw_scene` | a loop over all objects: it filters them by view, then calls `spr` and `sspr` one by one |
| 12.2% | `update_parts` | the particles, in Lua tables |
| 7.4% | `by_depth` | the comparator of `table.sort` |
| ~20% | `solid_at`, `blocked`, `chunk`, `fdiv`, `inside`, `ensure`, `BRK.near` | small functions called thousands of times a frame |

**Dark Night** (the key script, 3650 frames): 5.252 ms a frame before the fixes, worst 12.264 ms. 80,608 samples.

| Share | Function | Cause |
|---|---|---|
| 27.3% | `update_enemies` | each enemy loops over all the enemies (17: 272 pairs a frame) |
| 22.6% | `cam_blocked` | a scan over all 46 objects for the doors |
| 19.7% | `saw_icon` | one `rectfill` for each pixel of the HUD icon |

**The fixes** (all in `carts/darknight/main.lua`, pixel-identical or event-identical, `make test-darknight` passes with 97 events):

| Step | Commit in `bm-core` | Samples |
|---|---|---|
| Start | | 80,608 |
| `saw_icon`: one `rectfill` for each run of pixels (160 and 214 calls become 32 and 36) | `0a7630a` | 66,809 |
| `cam_blocked`: list `camstops`; enemy scan: reject on x first, `sqrt` only when near | `a06cc5f` | 44,934 |
| Enemy scan only within `FAR` (22) of the Hunter | `d1c72fb` | **39,783** |

The total is 51% fewer Lua instructions with no change of language. Frames 100 to 900 of the key script are identical to the original. Later frames differ between runs of the same build (the cause is not found), so they cannot be compared.

**The icon three ways** (micro test in bmhost, the same pixels each time):

| Way | Time of an icon |
|---|---|
| One `rectfill` for each pixel | 374 - 427 us |
| One `rectfill` for each run of pixels (kept) | 68 - 74 us |
| Drawn once, then `sspr` with a zoom, or `target_blit` | 4 - 9 us |

The last way is not in the game. On the sprite sheet it breaks the 3D textures (they are on the sheet). In a render target it showed a black box and lost the shadow in the game, while the micro test was right. The cause is open (`docs/progress/darknight.md`).

**Gap.** The earlier history of bm (Linux and Python, then C and Lua) is not measured in this work. Add its numbers here when they exist, so that this section stays the baseline.

## 4. Similar projects

| Project | What it is | Note for bm |
|---|---|---|
| [Pallene](https://github.com/pallene-lang/pallene) | typed Lua that compiles to C, works on Lua data in place | tried in section 6 |
| [Nelua](https://raw.githubusercontent.com/edubart/nelua-lang/master/README.md) | static language with Lua syntax, generates C, alpha | not tried |
| [Ravi](https://the-ravi-programming-language.readthedocs.io/en/latest/ravi-overview.html) | Lua with optional types, a JIT (MIR) and AOT | not tried |
| [Terra](https://terralang.org/) | low-level language made from Lua, needs LLVM | not tried |
| Teal | typed Lua that gives plain Lua | safety, not speed |
| MicroPython `@native`, `viper` | a typed subset of Python in machine code | the same idea on the Pico |

## 5. Prototype of the typed subset (`scripts/bmt.py`)

One source gives plain Lua (the types cut out) and C. Types `i32`, `f32`, `f64`, `bool` and arrays. `local`, `for`, `while`, `if`, `return`, `break`, `sqrt`, `abs`. A type error names the line of its statement; a float is never cut into an `i32`. The language is in the docstring of the script.

`make test-typed` (13 checks, on demand):

| Check | Result |
|---|---|
| Programs with a type error (8) | all refused, each with the right line |
| A good program (i32 widens to f64) | accepted |
| 20,000 random cases, `cam_blocked` and the enemy scan (`tests/typed/hot.bmt`), C against Lua | identical, byte for byte |
| Native functions called from Lua through the Lua C API, against Lua | identical |
| 1,227 results of `while` loops (`tests/typed/loops.bmt`), C against Lua | identical |
| Frame of 17 enemies, three ways | equal sums |

A found defect: the compiler pointed at the line after a type error. The test found it and it is fixed.

**Time of one call** (PC, three runs): Lua 0.7 - 1.2 us. C alone on flat arrays 0.07 us (10 to 17 times). C called from Lua with the tables of the game 0.5 - 0.9 us (**1.3 to 1.6 times**): the copy of the numbers out of the tables costs about as much as the function.

**Frame of 17 enemies** (`tests/typed/frame.lua`: Lua moves them, the pairs closer than 0.8 are found, Lua reads the hits):

| Way | Time of a frame | Against tables |
|---|---|---|
| A: tables in Lua, as the game | 13 - 22 us | 1 |
| B: data in C, one call for each enemy | 3 - 4 us | 3.7 - 6 times |
| C: data in C, one call for the frame | 3.4 - 5.5 us | 3.2 - 4.6 times |

The move of the enemies alone, through `E.get` and `E.set`, costs 2.4 - 3.3 us: more than half of way C. With tables it costs 1.3 - 2.5 us. The crossing between Lua and C is the cost now.

## 6. Trial of Pallene

**Build** (`tests/typed/pallene/setup.sh WORKDIR`, 9 seconds). Pallene needs a Lua 5.4.7 with a patch (`expose-internal-apis.patch`) and LPeg. The site lua.org is not reachable from the session, so the script takes Lua from the Git mirror of the tag v5.4.7 and LPeg from its Git mirror. The Makefile parts of the patch do not apply to the mirror; the script makes `luacore.h` itself. Pallene's text is MIT; its last commit is 3 Nov 2026.

**What the code does.** The generated C reads the array part of a Lua table in place (`arr->array[i - 1]`) and tests the tag of the value. It copies nothing. Of all the files of `third_party/lua`, only `ldo.c` and `lvm.c` differ from Lua 5.4.7 (the profile hooks): the structures of the tables are the same.

**Run** (`tests/typed/pallene/run.py PREFIX build/typed`, 2 checks): the 20,000 random cases give the same result from Pallene and from Lua. The frame sums are equal in all four ways.

| Way (Pallene's own Lua, `plua`) | Time of a frame (6 runs) | Against tables |
|---|---|---|
| A: Lua tables, the scan in Lua | 13.3 - 22.5 us | 1 |
| P1: arrays of numbers, Lua moves, Pallene scans | 7.2 - 11.0 us | 1.3 - 2.6 times |
| P2: tables as the game, Lua moves, Pallene scans | 10.5 - 23.0 us | 0.9 - 1.5 times |
| P3: tables as the game, Pallene moves and scans | 9.4 - 17.1 us | 0.9 - 1.6 times |

One call of `cam_blocked` or the scan on tables of Lua: Pallene 0.43 - 0.61 us, Lua 0.56 - 1.02 us (1.0 to 1.7 times).

## 7. All the ways on one frame

| Way | Needs changing the data of the game? | Time of a frame | Against tables |
|---|---|---|---|
| Lua tables (road A) | no | 13 - 22 us | 1 |
| Pallene, tables as the game (P2, P3) | no | 9.4 - 23 us | 0.9 - 1.6 times |
| Pallene, arrays of numbers (P1) | yes (arrays) | 7.2 - 11 us | 1.3 - 2.6 times |
| bmt C, data owned by C (B, C) | yes (C arrays and calls) | 3 - 5.5 us | 3.2 - 6 times |

Reading: on this scene Pallene gives little when the data stay in the tables of the game. It gives more on arrays of numbers. Data owned by C give the most, but then Lua reaches the data through short calls, and those calls are now the larger part of the cost.

The numbers vary between runs (a shared PC): read the ratios, not the digits. The scene is synthetic (17 enemies in a room of 5 x 5). Dark Night has much more logic for each enemy.

## 8. State and next steps

**Decisions.**
- Road A stays the baseline and the default way to make a game faster: profile, fix the structure in Lua, then add a C function.
- Road B stays open. It is not yet shown that it beats road A on a Pi.

**Open questions.**
1. **The Pi.** Every number is from a PC. The ARM1176 has no out-of-order execution and a small cache; the ratios may change a lot. There is no ARM toolchain in the build session. Measure on the Pi Zero W with `profile(true)` and read the session report.
2. **A language of bm.** The prototype lacks `//`, `%`, calls between typed functions, `min`, `max` and structs. The movement of the enemies cannot be written in it yet. Add them, rewrite the movement in the language, and measure the frame again.
3. **Code in a cartridge.** Native code needs a loader and one version for each architecture, or it must be linked into the kernel. The rule of one cartridge type must hold.
4. **A gain to see.** Pallene shows a small gain on this scene. Try it on a heavier loop (the particles of Yharnam, `update_parts`) before a decision.
5. **Dark Night in the language.** A real game is the test that counts.

**Rules for new results.** Run the same scene. Check equal sums or equal results before the time. Give a range of at least three runs. Say what is a PC and what is a Pi.

## 9. Reproduce

```
make test-typed                                                  # the prototype: 13 checks and the numbers of section 5
sh tests/typed/pallene/setup.sh /path/to/work                    # builds Pallene
python3 tests/typed/pallene/run.py /path/to/work/prefix build/typed   # the trial of section 6
BMHOST_LUAPROF=1 build/host/bmhost-bin build/carts/darknight.bm --input keys.txt --seconds 62   # the profile of section 3
```

The keys of Dark Night: take `SCRIPT` from `tests/darknight/check.py` (one line `<frame> keys <usages>`, and `<frame> quit`).
