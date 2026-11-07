/*
 * font_bmf.c: Dynamic BareMetal Font (.bmf) loader and scanner.
 */
#include "font_bmf.h"
#include "fs/fat.h"
#include "lib/printf.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>

static font_t dynamic_font;
static uint8_t *dynamic_buf;
static size_t dynamic_buf_len;
static char dynamic_name[16];

int font_bmf_scan(char list[][16], int max_fonts)
{
    int n = 0;
    static const char *const builtins[] = { "terminus", "inter", "6x12", "8x14", "bm" };
    for (int i = 0; i < 5 && n < max_fonts; i++) {
        strncpy(list[n++], builtins[i], 15);
        list[n - 1][15] = 0;
    }

    fat_dir_t d;
    fat_entry_t e;
    if (fat_opendir(&d, BMF_DIR) == 0) {
        while (fat_readdir(&d, &e) == 1 && n < max_fonts) {
            if (e.is_dir)
                continue;
            size_t l = strlen(e.name);
            if (l > 4 && strcasecmp(e.name + l - 4, ".bmf") == 0) {
                char stem[16];
                size_t stem_len = l - 4 < 15 ? l - 4 : 15;
                memcpy(stem, e.name, stem_len);
                stem[stem_len] = 0;

                int dup = 0;
                for (int j = 0; j < n; j++) {
                    if (strcasecmp(list[j], stem) == 0) {
                        dup = 1;
                        break;
                    }
                }
                if (!dup) {
                    strncpy(list[n++], stem, 15);
                    list[n - 1][15] = 0;
                }
            }
        }
    }
    return n;
}

const font_t *font_find(const char *name)
{
    if (!name || !name[0])
        return &font_terminus_8x16;

    if (strcasecmp(name, "terminus") == 0)
        return &font_terminus_8x16;
    if (strcasecmp(name, "inter") == 0)
        return &font_inter_8x16;
    if (strcasecmp(name, "6x12") == 0)
        return &font_console_6x12;
    if (strcasecmp(name, "8x14") == 0)
        return &font_console_8x14;
    if (strcasecmp(name, "bm") == 0)
        return &font_bm_12x32;

    if (dynamic_buf && strcasecmp(dynamic_name, name) == 0)
        return &dynamic_font;

    char path[32];
    size_t nl = strlen(name);
    if (nl > 4 && strcasecmp(name + nl - 4, ".bmf") == 0)
        ksnprintf(path, sizeof path, "%s/%s", BMF_DIR, name);
    else
        ksnprintf(path, sizeof path, "%s/%s.bmf", BMF_DIR, name);

    fat_entry_t e;
    uint8_t *data = NULL;
    size_t len = 0;
    if (fat_find(path, &e) == 0 && !e.is_dir && fat_load(&e, &data, &len) == 0) {
        if (len >= BMF_HEADER_SIZE && memcmp(data, BMF_MAGIC, 6) == 0) {
            uint8_t w = data[8];
            uint8_t h = data[9];
            uint16_t count = (uint16_t)(data[10] | (data[11] << 8));
            size_t bytes_per_glyph = (size_t)h * ((w + 7) / 8);
            if (len >= BMF_HEADER_SIZE + count * bytes_per_glyph && w > 0 && h > 0) {
                if (dynamic_buf)
                    free(dynamic_buf);
                dynamic_buf = data;
                dynamic_buf_len = len;
                strncpy(dynamic_name, name, 15);
                dynamic_name[15] = 0;
                dynamic_font.width = w;
                dynamic_font.height = h;
                dynamic_font.glyphs = dynamic_buf + BMF_HEADER_SIZE;
                return &dynamic_font;
            }
        }
        free(data);
    }

    return &font_terminus_8x16;
}
