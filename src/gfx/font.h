#ifndef FONT_H
#define FONT_H

#include <stdint.h>

/* Monospace bitmap font: ceil(width / 8) bytes per glyph row, MSB = left.
 * 256 glyphs in code page 437 order. */
typedef struct {
    uint8_t width;
    uint8_t height;
    const uint8_t *glyphs;
} font_t;

extern const font_t font_console_8x16;
extern const font_t font_console_16x32;     /* 8x16 scaled 2x (menu 720p) */
extern const font_t font_console_24x48;     /* 8x16 scaled 3x (menu 1080p) */
extern const font_t font_console_8x14;     /* .bm print(): font("8x14") */
extern const font_t font_console_6x12;     /* font("6x12"): 106 x 30 at 640x360 */

#endif
