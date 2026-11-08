# API: typed native functions (experiment)

Part of the game API. Index and basics: [API.md](../API.md).

These functions are C code that the typed subset of Lua would give ([design](../design/typed-native.md), [state](../progress/typed.md)). They keep their data in C arrays. They exist to measure Lua against C on the Pi Zero W. A kernel that does not have them has no such name: test with `if typed_blocked then`. They may change or go away.

The data is one chunk of 32 x 32 tiles of 16 pixels and up to 64 columns (the shape of Yharnam's chunks).

| Function | Description |
|---|---|
| `typed_set(i, solid)` | sets tile `i` (0 to 1023, row by row) as solid or not |
| `typed_col(i, x, y, rx, ry, off)` | sets column `i` (0 to 63): its centre, its two radii, and `off` (true: the column does not block) |
| `typed_fdiv(a, b)` | `floor(a) // b` as an integer, for an integer `b` |
| `typed_solid_at(x, y)` | `true` if the pixel is in a solid tile or outside the chunk |
| `typed_blocked(x, y)` | `true` if a box of 12 x 5 pixels around `(x, y)` touches a solid tile or a column |
| `typed_blocked_many(n, x0, y0, dx, dy)` | the number of blocked points among the `n` points `(x0 + i*dx, y0 + i*dy)`; one call for the whole loop |
| `typed_ysync(cx, cy, solid, cols)` | copies one chunk of Yharnam to C: `solid` is a string of 256 bytes (0 or 1, row by row), `cols` a flat table `x, y, rx, ry, off` for each column. Gives `false` if it does not fit |
| `typed_yblocked(x, y)` | the game's `blocked`: `true` or `false`, or `nil` when a chunk it needs is not in C (the Lua then decides). Chunks are 16 x 16 tiles of 16 pixels, 8 x 8 chunks |

A call from Lua to a C function costs about 1 us on the Pi Zero W, so give a C function a loop, not one number at a time.
