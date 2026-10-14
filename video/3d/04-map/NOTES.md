# Video 4 (3D map): what the recording taught (2026-10-14)

- **Keys proven with bmhost screenshots** (`proto.py`, `record.py`): hold 8 frames then wait 20; a capital letter is typed for Shift+letter; the cursor moves 2 tiles per arrow press with the big brush; each step of `'.'` on height/walls changes the tile by 2.
- **`z` toggles the brush size**: after `T` the brush is 16x16 already; pressing `z` (as the first draft did) went back to 8x8 and the corridor came out one tile wide.
- **Pick the grass with `Tab`, 3 times Down, Enter.** The 16x16 brush stamps 2x2 tiles from the chosen one; with tile 0 (nothing) the top-left tile of every block is empty and `map3d` reads only that one: the whole floor is a hole (`map3d_height` returns nil everywhere). `verify.py` checks it.
- **The tile chooser stops at 990 (the sheet): the markers 1024 (start) and 1030 (goal) cannot be chosen.** New keys `Shift+S` / `Shift+G` (and `Shift+A/C/O` for apples, crate, lamp) in `carts/editor/main.lua`; apples must not start on the start cell (the row overwrites it).
- **Shift+arrows cannot be typed** over the serial/typed queue: Shift has to be held. The 3D tilt is not shown in the video.
- **`lamp3d` slots are 1..4**: the preview called `lamp3d(0, ...)` and the editor stopped with an error as soon as a lamp was on the map. Fixed in the editor. **The kernel embeds the editor** (`build/editor.bm` inside `kernel.img`): rebuild the kernel after any change of `carts/editor` or a QEMU recording still runs the old editor (it cost two 70-minute recordings).
- `verify.py`: four layers; a corridor of 3 cells (rows alternate 3 and 6 tiles); a hole = a row thinner than the rows two above and two below; heights, walls; apples 1026, crate 1028, lamp 1032, start 1024, goal 1030; the parts of video 3 kept; `bm_forge check`.
- The saved map is 547 KB (256x256, 4 layers); bmhost and QEMU give the same file.
