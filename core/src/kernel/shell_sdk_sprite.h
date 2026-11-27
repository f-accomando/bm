/*
 * Shell SDK: the tiles on the screen. The view of the tiles of the open ROM, with a cursor of one tile that
 * selects and copies (the Code tab shows it with the key t, the Sprite tab on its left), and the Sprite tab:
 * the sprite sheet on the right, where the copied tiles are pasted, and on the left the canvas that paints the
 * pixels of a tile of the sheet, or the ROM.
 */
#ifndef SHELL_SDK_SPRITE_H
#define SHELL_SDK_SPRITE_H

#include <stddef.h>
#include <stdint.h>

#include "shell_sdk_proj.h"
#include "shell_sdk_rom.h"

/* The SDK starts: an empty sheet, canvas and clipboard. */
void sdk_sprite_reset(void);
/* The code the sheet is written in: SDK_SPRITE_NES or SDK_SPRITE_SNES, or SDK_SPRITE_NONE (a bm project: the
 * sheet lives only while the SDK is open). Call sdk_sprite_sync before the tab is drawn: when the code changed
 * since the last time, the sheet is read again from its tiles block (shell_sdk_chr.h) and the palettes from the
 * source (shell_sdk_pal.h). A change of the sheet is written into the code at once. */
enum { SDK_SPRITE_NONE, SDK_SPRITE_NES, SDK_SPRITE_SNES, SDK_SPRITE_NES_C, SDK_SPRITE_SNES_C };
void sdk_sprite_sync(const sdk_proj_t *p);
/* The ROM that opened, or NULL when it closed. The bytes stay with the caller. A new ROM takes the left of the
 * Sprite tab, with the keys. */
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
/* A key of the Sprite tab. Everywhere: Ctrl+T or Ctrl+Tab move between the left and the sheet, Ctrl+E shows the
 * ROM or the canvas on the left. Sheet: the arrows move (Shift selects), Enter opens the tile in the canvas,
 * Ctrl+C copy, Ctrl+X cut, Ctrl+V paste, Del empties, i imports the sprite sheet of the ROM (sdk_sheet_part;
 * twice when the sheet has tiles). Canvas: the arrows move (Shift selects), Space paints, Backspace makes
 * transparent (both also while held and moving: SDK_MOD_PAINT, SDK_MOD_ERASE), [ ] the colour, Ctrl+C, X, V,
 * Enter back to the sheet. Sheet and canvas: p the palette, s 8x8 or 16x16. ROM: sdk_tiles_key, and i. Everywhere
 * in a NES or SNES project: v hides or shows the code under the panes. */
int sdk_sprite_key(int key, int mods);
/* The animation mode (a): 1 while its preview moves (call sdk_sprite_tick between the keys), 1 while a name is
 * typed there (Esc is the tab's), and its end when another project comes. */
int sdk_sprite_animating(void);
void sdk_sprite_tick(void);
int sdk_sprite_typing(void);
void sdk_sprite_new_project(void);
/* The legend of what has the keys now (shell_sdk_keys.h). */
int sdk_sprite_keys(void);

#endif /* SHELL_SDK_SPRITE_H */
