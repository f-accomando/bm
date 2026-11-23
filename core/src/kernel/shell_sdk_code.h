/*
 * Shell SDK: the Code tab. A text editor on the left. When a ROM (.nes, .sfc, .smc) is open, the tab splits:
 * the editor stays on the left and the ROM (its header and a hex dump) is on the right. Ctrl+Tab moves the
 * focus from one side to the other. The text is not in the project file yet.
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

/* Handles a key of the Code tab. Returns 1 when the tab used it. ctrl is 1 while Ctrl is held. */
int sdk_code_key(int key, int ctrl);

#endif /* SHELL_SDK_CODE_H */
