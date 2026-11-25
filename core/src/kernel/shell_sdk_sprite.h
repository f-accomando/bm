/*
 * Shell SDK: the tiles on the screen. The view of the tiles of the open ROM, with a cursor of one tile that
 * selects and copies (the Code tab shows it with the key t, the Sprite tab on its right), and the Sprite tab:
 * the canvas on the left, where the copied tiles are pasted.
 */
#ifndef SHELL_SDK_SPRITE_H
#define SHELL_SDK_SPRITE_H

#include <stddef.h>
#include <stdint.h>

#include "shell_sdk_rom.h"

/* The SDK starts: an empty canvas and clipboard. */
void sdk_sprite_reset(void);
/* The ROM that opened, or NULL when it closed. The bytes stay with the caller. A new ROM shows the split
 * screen again, with the keys on the ROM side of the Sprite tab. */
void sdk_sprite_rom(const uint8_t *data, size_t size, const sdk_rom_t *info, const char *name);

/* The view of the tiles in a pane: its text; sdk_tiles_paint draws the pixels after all the text of the screen.
 * code: in the Code tab, where t and a change the view. active: the pane has the keys. */
void sdk_tiles_draw(int col, int width, int rows, int code, int active);
void sdk_tiles_paint(void);
/* A key of the view: the arrows move the cursor (Shift selects), Ctrl+C copies the tiles, Ctrl+A selects all
 * that shows; Ctrl+Left and Right move the view by a tile, ',' and '.' by a byte; PgUp, PgDn, Home, End scroll;
 * + and - the tiles in a row, o the 8x16 order, b the bits a pixel. Returns 1 when it used the key. */
int sdk_tiles_key(int key, int mods);

/* The Sprite tab; msg (or NULL) goes on the last row. */
void sdk_sprite_draw(int cols, int rows, const char *msg);
/* A key of the Sprite tab: Ctrl+T or Ctrl+Tab move between the canvas and the ROM, Ctrl+E hides or shows the
 * ROM; on the canvas the arrows move (Shift selects), Ctrl+C copy, Ctrl+X cut, Ctrl+V paste, Del empties. */
int sdk_sprite_key(int key, int mods);

#endif /* SHELL_SDK_SPRITE_H */
