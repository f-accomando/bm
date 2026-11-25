/*
 * Shell SDK: the canvas of the Sprite tab (see shell_sdk_canvas.h). Original code.
 */
#include "shell_sdk_canvas.h"

#include <string.h>

static sdk_tile_t canvas[SDK_CANVAS_H][SDK_CANVAS_W];
static sdk_tile_t clip[SDK_CLIP_TILES];
static int clip_w, clip_h;

void sdk_canvas_reset(void)
{
    memset(canvas, 0, sizeof canvas);
    clip_w = clip_h = 0;
}

int sdk_canvas_empty(void)
{
    for (int r = 0; r < SDK_CANVAS_H; r++)
        for (int c = 0; c < SDK_CANVAS_W; c++)
            if (canvas[r][c].used)
                return 0;
    return 1;
}

const sdk_tile_t *sdk_canvas_tile(int r, int c) { return &canvas[r][c]; }

void sdk_clip_start(int w, int h)
{
    clip_w = w < 0 ? 0 : w > SDK_CLIP_TILES ? SDK_CLIP_TILES : w;
    clip_h = clip_w == 0 || h < 0 ? 0 : h > SDK_CLIP_TILES / clip_w ? SDK_CLIP_TILES / clip_w : h;
    memset(clip, 0, sizeof clip);
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

int sdk_canvas_paste(int r, int c)
{
    int n = 0;
    for (int y = 0; y < clip_h; y++)
        for (int x = 0; x < clip_w; x++)
            if (r + y >= 0 && r + y < SDK_CANVAS_H && c + x >= 0 && c + x < SDK_CANVAS_W) {
                canvas[r + y][c + x] = clip[y * clip_w + x];
                n++;
            }
    return n;
}

void sdk_canvas_copy(int r1, int c1, int r2, int c2)
{
    sdk_clip_start(c2 - c1 + 1, r2 - r1 + 1);
    for (int y = 0; y < clip_h; y++)
        for (int x = 0; x < clip_w; x++)
            if (r1 + y >= 0 && r1 + y < SDK_CANVAS_H && c1 + x >= 0 && c1 + x < SDK_CANVAS_W)
                clip[y * clip_w + x] = canvas[r1 + y][c1 + x];
}

void sdk_canvas_clear(int r1, int c1, int r2, int c2)
{
    for (int r = r1; r <= r2; r++)
        for (int c = c1; c <= c2; c++)
            if (r >= 0 && r < SDK_CANVAS_H && c >= 0 && c < SDK_CANVAS_W)
                memset(&canvas[r][c], 0, sizeof canvas[r][c]);
}
