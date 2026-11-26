/*
 * Shell SDK: the sheet of the Sprite tab (see shell_sdk_sheet.h). Original code.
 */
#include "shell_sdk_sheet.h"

#include <string.h>

#include "shell_sdk_tiles.h"

static sdk_tile_t sheet[SDK_SHEET_H][SDK_SHEET_W];
static sdk_tile_t clip[SDK_CLIP_TILES];
static int clip_w, clip_h;
static unsigned clip_stamp;

unsigned sdk_clip_stamp(void) { return clip_stamp; }

void sdk_sheet_reset(void)
{
    memset(sheet, 0, sizeof sheet);
    clip_w = clip_h = 0;
}

int sdk_sheet_empty(void)
{
    for (int r = 0; r < SDK_SHEET_H; r++)
        for (int c = 0; c < SDK_SHEET_W; c++)
            if (sheet[r][c].used)
                return 0;
    return 1;
}

const sdk_tile_t *sdk_sheet_tile(int r, int c) { return &sheet[r][c]; }

void sdk_sheet_set(int r, int c, const sdk_tile_t *t)
{
    if (r >= 0 && r < SDK_SHEET_H && c >= 0 && c < SDK_SHEET_W)
        sheet[r][c] = *t;
}

void sdk_clip_start(int w, int h)
{
    clip_w = w < 0 ? 0 : w > SDK_CLIP_TILES ? SDK_CLIP_TILES : w;
    clip_h = clip_w == 0 || h < 0 ? 0 : h > SDK_CLIP_TILES / clip_w ? SDK_CLIP_TILES / clip_w : h;
    memset(clip, 0, sizeof clip);
    clip_stamp++;
}

sdk_tile_t *sdk_clip_cell(int r, int c)
{
    return r >= 0 && r < clip_h && c >= 0 && c < clip_w ? &clip[r * clip_w + c] : NULL;
}

int sdk_clip_size(int *w, int *h)
{
    *w = clip_w;
    *h = clip_h;
    return clip_w > 0 && clip_h > 0;
}

int sdk_sheet_paste(int r, int c)
{
    int n = 0;
    for (int y = 0; y < clip_h; y++)
        for (int x = 0; x < clip_w; x++)
            if (r + y >= 0 && r + y < SDK_SHEET_H && c + x >= 0 && c + x < SDK_SHEET_W) {
                sheet[r + y][c + x] = clip[y * clip_w + x];
                n++;
            }
    return n;
}

void sdk_sheet_copy(int r1, int c1, int r2, int c2)
{
    sdk_clip_start(c2 - c1 + 1, r2 - r1 + 1);
    for (int y = 0; y < clip_h; y++)
        for (int x = 0; x < clip_w; x++)
            if (r1 + y >= 0 && r1 + y < SDK_SHEET_H && c1 + x >= 0 && c1 + x < SDK_SHEET_W)
                clip[y * clip_w + x] = sheet[r1 + y][c1 + x];
}

void sdk_sheet_clear(int r1, int c1, int r2, int c2)
{
    for (int r = r1; r <= r2; r++)
        for (int c = c1; c <= c2; c++)
            if (r >= 0 && r < SDK_SHEET_H && c >= 0 && c < SDK_SHEET_W)
                memset(&sheet[r][c], 0, sizeof sheet[r][c]);
}

size_t sdk_sheet_part(size_t size, size_t chr_from, size_t chr_len, size_t view, int tile, int *count)
{
    size_t from = view, end = size;
    if (chr_len && chr_len / (size_t)tile <= SDK_CLIP_TILES) {
        from = chr_from;
        end = chr_from + chr_len;
    } else if (chr_len && view >= chr_from && view < chr_from + chr_len)
        end = chr_from + chr_len;
    if (end > size)
        end = size;
    const size_t n = from < end ? (end - from) / (size_t)tile : 0;
    *count = n > SDK_CLIP_TILES ? SDK_CLIP_TILES : (int)n;
    return from;
}

void sdk_sheet_import(const uint8_t *data, size_t size, size_t from, int fmt, int count)
{
    memset(sheet, 0, sizeof sheet);
    const size_t tile = (size_t)sdk_tile_bytes(fmt);
    for (int i = 0; i < count && i < SDK_SHEET_W * SDK_SHEET_H; i++) {
        sdk_tile_t *t = &sheet[i / SDK_SHEET_W][i % SDK_SHEET_W];
        sdk_tile_decode(data, size, from + (size_t)i * tile, fmt, t->pix);
        for (int k = 0; k < 64; k++)
            t->used |= t->pix[k] != 0;
        t->fmt = (uint8_t)fmt;
    }
}
