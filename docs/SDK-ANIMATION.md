# Shell SDK: animations

The Sprite tab of a NES or SNES project has an animation mode. Press `a` to open it and `a` to close it. An
animation is 1 to 8 frames. A frame is a block of 8x8 tiles: 8x8, 8x16, 16x8, 16x16, 16x24, 24x24, 16x32, 32x16 or
32x32 pixels.

## Make an animation

1. Press `n`. The new animation has 4 empty frames of 16x16.
2. Set the name, size, frames, speed, palette and reuse in the first row. Left and Right choose a setting. `-` and
   `=` change it. Enter types the name.
3. Press Down. Paint the frames. The preview on the right moves while you work.
4. Press `w`. Choose the first tile in the sheet. Press Enter.

The save writes the tiles into the sheet and the `anim` block into the code. One undo step removes both.

- `w` takes empty tiles, from the tile that you choose, and the tiles that this animation had. It does not take a
  tile that another animation shows.
- **Reuse mirrors**: cells that are the same, also mirrored or upside down, take one tile. A coin that you draw the
  same in its four quarters takes one tile. **Reuse off**: each cell takes its own tile.
- An empty cell takes no tile and shows no sprite.
- `i` takes the frames from the sheet. The frames are side by side from the tile that you choose.
- Ctrl+C on a whole frame also copies its tiles. Ctrl+V on the sheet pastes them.
- Changes that are not saved stay while the SDK is open on the project.

Hold F12 to see the keys.

## The code

The block is between `;@anim` and `;@end` (assembly) or `//@anim` and `//@end` (bm C). The tab reads it and writes
it. You can edit it by hand.

```c
//@anim  Sprite tab: wide, high, frames, speed; then tile, attribute
const byte anim_spin[] = {          // 16x16, 2 frames
    2, 2, 2, 8,                              // wide, high, frames, speed
    0x02,0x00, 0x02,0x40, 0x02,0x80, 0x02,0xC0,  // frame 1
    0x03,0x00, 0x03,0x40, 0x03,0x80, 0x03,0xC0 }; // frame 2
//@end
```

| Value | Meaning |
|---|---|
| wide, high | tiles across and down, 1 to 4 |
| frames | 1 to 8 |
| speed | screen frames for each animation frame, 1 to 60 |
| tile | 0 to 255 |
| attribute | palette 0 to 3; `$40` mirrored; `$80` upside down; `$10` empty cell |

## Show it in the program

Each cell is one hardware sprite. A 16x16 animation takes 4 sprites. The NES shows 8 sprites on a line at most.

**bm C** (NES and SNES):

```c
void update(void) {
    animate(4, 100, 80, anim_spin);   // first sprite, x, y, animation
}
```

`animate` shows the frame of the moment. Call it at each frame. It uses the sprites from the first one, one a cell.

**Assembly, NES**:

```asm
    lda #4
    sta anim_n                    ; the first sprite
    lda #100
    sta anim_x
    lda #80
    sta anim_y
    lda #<anim_spin
    ldy #>anim_spin
    jsr anim_draw
    jsr anim_tick                 ; once a frame
```

- The build adds the routines when the program calls them. They use the zero page `$FA-$FB`, the RAM `$0780-$078F`
  and the sprite table at `$0200`.
- **Assembly, SNES**: the block is there. The routines are not.

## Code

`core/src/kernel/shell_sdk_anim.c` (the model and the routines), `shell_sdk_animui.c` (the screen),
`shell_sdk_lists.c` (the reader of the blocks). Tests: `make test-sdk-anim`, and
`tests/qemu_test.py -k shell_sdk_animation`.
