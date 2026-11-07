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
extern const font_t font_terminus_8x16;    /* CP437 Terminus 8x16 */
extern const font_t font_inter_8x16;       /* CP437 Inter 8x16 */
extern const font_t font_console_16x32;     /* 8x16 scaled 2x (menu 720p) */
extern const font_t font_console_24x48;     /* 8x16 scaled 3x (menu 1080p) */
extern const font_t font_console_8x14;     /* .bm print(): font("8x14") */
extern const font_t font_console_6x12;     /* font("6x12"): 106 x 30 at 640x360 */
extern const font_t font_bm_12x32;          /* the bm font family (scripts/bmfont.py): 720p, ASCII only for now */
extern const font_t font_bm_18x48;          /* the same at 1080p */

/* Dynamic .bmf font loader (/bmf/<name>.bmf or built-in fallback) */
const font_t *font_find(const char *name);
int font_bmf_scan(char list[][16], int max_fonts);

#endif
