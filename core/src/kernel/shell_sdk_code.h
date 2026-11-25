/*
 * Shell SDK: the Code tab. A text editor on the left. When a ROM (.nes, .sfc, .smc) is open, the tab splits:
 * the editor stays on the left and the ROM (its header and a hex dump) is on the right. Ctrl+T or Ctrl+Tab
 * moves the focus from one side to the other; Ctrl+W closes the ROM. The dump and the listing have the cursor
 * and the moves of the editor, Shift selects and Ctrl+C copies into its clipboard; they cannot be changed.
 * On the ROM side the key a
 * changes between the dump and the ASM listing (read only), and
 * the key t between the dump and the bytes drawn as 8x8 tiles. The Sprite tab shows the tiles of the same
 * ROM: the open ROM and the view of its tiles are one for the two tabs.
 */
#ifndef SHELL_SDK_CODE_H
#define SHELL_SDK_CODE_H

#include <stddef.h>

/* An empty editor and no ROM (the SDK starts, or a new project). */
void sdk_code_reset(void);

/* Opens a ROM from the SD card. Returns 0, or -1 with the reason in err. The focus goes to the editor. */
int sdk_code_open_rom(const char *path, char *err, size_t en);
int sdk_code_has_rom(void);
/* "Super Mario World.sfc": the file name of the open ROM ("" when there is none). */
const char *sdk_code_rom_name(void);

/* Draws the tab on the rows 1 to rows of the console; msg (or NULL) goes on the last row. */
void sdk_code_draw(int cols, int rows, const char *msg);

/* The modifier keys held while a key came (hid_usage_held). */
#define SDK_MOD_CTRL  1
#define SDK_MOD_SHIFT 2
#define SDK_MOD_ALT   4

/* Handles a key of the Code tab. Returns 1 when the tab used it. The editor keys are those of bm Code:
 * Shift with a move selects; Ctrl+Left and Ctrl+Right skip a word; Ctrl+Alt+Left and Right go to the line
 * start and end; Ctrl+Alt+Up and Down to the start and the end of the text; PgUp, PgDn and Alt+Up, Alt+Down
 * move a page; Ctrl+C copy, Ctrl+X cut, Ctrl+V paste (a line when nothing is selected), Ctrl+D duplicate the
 * line, Ctrl+A select all. */
int sdk_code_key(int key, int mods);

/* The Sprite tab with a ROM open: the keys on the left, the tiles of the ROM on the right; msg (or NULL) goes
 * on the last row. In the tiles + and - change the tiles in a row, o the order (8x8 or 8x16), Ctrl+Left and
 * Ctrl+Right move one tile. sdk_sprite_key returns 1 when it used the key; Ctrl+W closes the ROM. */
void sdk_sprite_draw(int cols, int rows, const char *msg);
int sdk_sprite_key(int key, int mods);

#endif /* SHELL_SDK_CODE_H */
