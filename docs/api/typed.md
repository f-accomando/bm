# API: typed native functions (experiment)

Part of the game API. Index and basics: [API.md](../API.md).

These functions are C code that the typed subset of Lua would give ([design](../design/typed-native.md), [state](../progress/typed.md)). They keep their data in C arrays. They exist to measure Lua against C on the Pi Zero W. The functions made for one game (`typed_set`, `typed_blocked`, `typed_ysync`, `typed_yblocked` and the others) were removed from the kernel on 9 Nov 2026: `collmap_*` replaced them. A kernel that does not have them has no such name: test with `if typed_blocked then`. They may change or go away.

## A collision map for games seen from above (experiment)

The game gives the geometry once, and the C that tests a body is made by `scripts/bmt.py` from `tests/typed/collmap.bmt`. Test with `if collmap_new then`. Yharnam uses it (53.0 fps on the Pi Zero W against 50.5 with the Lua). A call from Lua to a C function costs about 1 us on the Pi Zero W, so call it once for each test of a body, not for each number. It may change or go away.

| Function | Description |
|---|---|
| `collmap_new(opts)` | `opts = {map={w,h}, chunk=n, tile=px, cols=m, foot={x0,y0,x1,y1}, margin=px, pad={px,py}}`: a map of `w` x `h` chunks of `n` x `n` tiles of `px` pixels; `cols` obstacles at most in a chunk. `foot` gives the four corners of the body from its place; obstacles are ellipses grown by `pad`, and tested for the chunk and its neighbours within `margin` pixels |
| `collmap_chunk(cm, cx, cy, solid, cols)` | copies a chunk in: `solid` is a string with one byte (0 or 1) for each tile, row by row; `cols` is a flat table `x, y, rx, ry, off` for each obstacle. Gives `false` if it does not fit |
| `collmap_blocked(cm, x, y)` | `true` or `false`, or `nil` when a chunk it needs is not copied in (the game runs its own Lua) |

## The bmvm machine (experiment)

A cartridge carries typed hot functions as data, a blob that `scripts/bmtvm.py` makes from a `.bmt` source, and the kernel runs them. No native code goes into the cartridge. The code is checked when it is loaded (registers, jumps, constants, calls) and while it runs (array bounds, call depth, stack, a budget of steps), and an array is a number in a table of the call, never a pointer. Test with `if vm_load then`. It may change or go away.

| Function | Description |
|---|---|
| `vm_load(blob)` | a module, or `nil` and the reason |
| `vm_fn(m, name)` | the number of a function, or `nil` |
| `vm_buf(kind, n)` | an array of `n` elements, all 0; `kind` is `"i32"` or `"f64"` |
| `vm_buf_fill(b, t [, at])` | copies `t[1], t[2] ...` into `b`, from element `at` (1 if missing) |
| `vm_buf_get(b, i)` | element `i` of `b` |
| `vm_bind(m, f, buffers...)` | binds the array parameters of function `f`, in order, once |
| `vm_run(m, f, args...)` | runs `f`; a number for a number parameter, a buffer for an array parameter (none if `f` is bound); gives the result |
