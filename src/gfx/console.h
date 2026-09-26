#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

#include "drivers/fb.h"
#include "gfx/font.h"

/* ANSI colour indices (same order as SGR 30-37 / 90-97). */
enum {
    COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_BROWN,
    COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_LIGHT_GREY,
    COLOR_DARK_GREY, COLOR_LIGHT_RED, COLOR_LIGHT_GREEN, COLOR_YELLOW,
    COLOR_LIGHT_BLUE, COLOR_LIGHT_MAGENTA, COLOR_LIGHT_CYAN, COLOR_WHITE,
};

/*
 * Text console on the framebuffer. Row 0 is a status bar; the rest scrolls.
 * Output understands \n \r \b \t and a subset of ANSI escape sequences:
 *   ESC[...m (SGR: 0, 1, 7, 30-37, 39, 40-47, 49, 90-97, 100-107)
 *   ESC[2J (clear), ESC[H / ESC[r;cH (cursor position), ESC[K (clear to EOL)
 * Bytes >= 0x80 are drawn as code page 437 glyphs.
 */
void console_init(framebuffer_t *fb, const font_t *font);
int  console_active(void);
void console_putc(char c);
void console_write(const char *s);
void console_clear(void);
void console_set_color(uint8_t fg, uint8_t bg);
void console_size(uint32_t *cols, uint32_t *rows);
framebuffer_t *console_framebuffer(void);

/* Status bar text, left and right aligned (either may be NULL to keep it). */
void console_set_status(const char *left, const char *right);

/* While suspended the console keeps its text but does not touch the
 * framebuffer; resuming redraws the whole screen. */
void console_suspend(int suspend);

/* White on red, cleared: used by the exception handler before its dump. */
void console_panic(void);

#endif
