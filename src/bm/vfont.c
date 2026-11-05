#include "vfont.h"
#include "vfont_default.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define STB_TRUETYPE_IMPLEMENTATION
#if __has_include("../../third_party/stb_truetype.h")
#include "../../third_party/stb_truetype.h"
#elif __has_include("third_party/stb_truetype.h")
#include "third_party/stb_truetype.h"
#else
#include "stb_truetype.h"
#endif

const uint8_t *g16_vfont_default_data(size_t *out_len)
{
    if (out_len) *out_len = vfont_default_ttf_len;
    return vfont_default_ttf;
}

int g16_vfont_init(g16_vfont_t *vf, const uint8_t *data, size_t len, int owns_data, float size)
{
    if (!vf || !data || len == 0) return -1;
    if (!vf->info) {
        vf->info = malloc(sizeof(stbtt_fontinfo));
        if (!vf->info) return -1;
    }
    if (vf->owns_data && vf->data && vf->data != data) {
        free((void *)vf->data);
    }
    for (int i = 0; i < 96; i++) {
        if (vf->glyphs[i].alpha) {
            stbtt_FreeBitmap(vf->glyphs[i].alpha, 0);
            vf->glyphs[i].alpha = NULL;
        }
        vf->glyphs[i].rendered = 0;
    }
    vf->data = data;
    vf->data_len = len;
    vf->owns_data = owns_data;
    if (!stbtt_InitFont((stbtt_fontinfo *)vf->info, data, 0)) {
        return -1;
    }
    vf->scale = 0.0f;
    g16_vfont_size(vf, size > 0.0f ? size : 16.0f);
    return 0;
}

void g16_vfont_free(g16_vfont_t *vf)
{
    if (!vf) return;
    for (int i = 0; i < 96; i++) {
        if (vf->glyphs[i].alpha) {
            stbtt_FreeBitmap(vf->glyphs[i].alpha, 0);
            vf->glyphs[i].alpha = NULL;
        }
        vf->glyphs[i].rendered = 0;
    }
    if (vf->owns_data && vf->data) {
        free((void *)vf->data);
        vf->data = NULL;
        vf->data_len = 0;
        vf->owns_data = 0;
    }
    if (vf->info) {
        free(vf->info);
        vf->info = NULL;
    }
}

void g16_vfont_size(g16_vfont_t *vf, float size)
{
    if (!vf || !vf->info) return;
    if (size <= 0.0f) size = 16.0f;
    if (vf->size == size && vf->scale > 0.0f) return;

    for (int i = 0; i < 96; i++) {
        if (vf->glyphs[i].alpha) {
            stbtt_FreeBitmap(vf->glyphs[i].alpha, 0);
            vf->glyphs[i].alpha = NULL;
        }
        vf->glyphs[i].rendered = 0;
    }
    vf->size = size;
    stbtt_fontinfo *info = (stbtt_fontinfo *)vf->info;
    vf->scale = stbtt_ScaleForPixelHeight(info, size);
    int raw_ascent = 0, raw_descent = 0, raw_gap = 0;
    stbtt_GetFontVMetrics(info, &raw_ascent, &raw_descent, &raw_gap);
    vf->ascent = (int)roundf(raw_ascent * vf->scale);
    vf->descent = (int)roundf(raw_descent * vf->scale);
    vf->line_gap = (int)roundf(raw_gap * vf->scale);
    vf->line_height = vf->ascent - vf->descent + vf->line_gap;
    if (vf->line_height < 1) vf->line_height = (int)size;
}

static g16_vglyph_t *get_glyph(g16_vfont_t *vf, int ch)
{
    if (ch < 32 || ch >= 128) return NULL;
    int idx = ch - 32;
    g16_vglyph_t *g = &vf->glyphs[idx];
    if (g->rendered) return g;

    stbtt_fontinfo *info = (stbtt_fontinfo *)vf->info;
    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(info, ch, &advance, &lsb);
    g->advance_x = (int)roundf(advance * vf->scale);

    int gw = 0, gh = 0, xoff = 0, yoff = 0;
    g->alpha = stbtt_GetCodepointBitmap(info, 0, vf->scale, ch, &gw, &gh, &xoff, &yoff);
    g->w = gw;
    g->h = gh;
    g->xoff = xoff;
    g->yoff = yoff;
    g->rendered = 1;
    return g;
}

int g16_vprint(g16_t *g, g16_vfont_t *vf, const char *text, int x, int y, uint16_t col)
{
    if (!vf || !vf->info || !text) return x;
    int cur_x = x - g->cam_x;
    int cur_y = y - g->cam_y;
    int start_x = cur_x;
    int baseline = cur_y + vf->ascent;

    stbtt_fontinfo *info = (stbtt_fontinfo *)vf->info;
    char prev_ch = 0;

    for (const char *p = text; *p; p++) {
        char ch = *p;
        if (ch == '\n') {
            cur_x = start_x;
            baseline += vf->line_height;
            prev_ch = 0;
            continue;
        }
        if (ch == '\r') continue;
        if (ch == '\t') {
            g16_vglyph_t *sp = get_glyph(vf, ' ');
            cur_x += (sp && sp->advance_x ? sp->advance_x : (int)(vf->size * 0.5f)) * 4;
            prev_ch = 0;
            continue;
        }

        if (prev_ch && (unsigned char)ch >= 32 && (unsigned char)ch < 128) {
            int kern = stbtt_GetCodepointKernAdvance(info, prev_ch, ch);
            cur_x += (int)roundf(kern * vf->scale);
        }

        g16_vglyph_t *glyph = get_glyph(vf, (unsigned char)ch);
        if (glyph) {
            if (glyph->alpha && glyph->w > 0 && glyph->h > 0) {
                int gx = cur_x + glyph->xoff;
                int gy = baseline + glyph->yoff;

                int x0 = gx, y0 = gy;
                int x1 = gx + glyph->w, y1 = gy + glyph->h;
                int src_x0 = 0, src_y0 = 0;

                if (x0 < g->cx0) { src_x0 = g->cx0 - x0; x0 = g->cx0; }
                if (y0 < g->cy0) { src_y0 = g->cy0 - y0; y0 = g->cy0; }
                if (x1 > g->cx1) x1 = g->cx1;
                if (y1 > g->cy1) y1 = g->cy1;

                if (x0 < x1 && y0 < y1) {
                    for (int r = 0; r < y1 - y0; r++) {
                        int dst_y = y0 + r;
                        const uint8_t *src_row = glyph->alpha + (src_y0 + r) * glyph->w + src_x0;
                        uint16_t *dst_row = g->px + (uint32_t)dst_y * g->stride + x0;
                        int row_w = x1 - x0;
                        for (int c = 0; c < row_w; c++) {
                            uint8_t a = src_row[c];
                            if (!a) continue;
                            if (a == 255) {
                                dst_row[c] = col;
                            } else {
                                dst_row[c] = blend_rgb565(col, dst_row[c], a);
                            }
                        }
                    }
                }
            }
            cur_x += glyph->advance_x;
        }
        prev_ch = ch;
    }
    return cur_x + g->cam_x;
}

void g16_vmeasure(g16_vfont_t *vf, const char *text, int *out_w, int *out_h)
{
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (!vf || !vf->info || !text) return;

    int max_w = 0;
    int cur_w = 0;
    int lines = 1;
    char prev_ch = 0;
    stbtt_fontinfo *info = (stbtt_fontinfo *)vf->info;

    for (const char *p = text; *p; p++) {
        char ch = *p;
        if (ch == '\n') {
            if (cur_w > max_w) max_w = cur_w;
            cur_w = 0;
            lines++;
            prev_ch = 0;
            continue;
        }
        if (ch == '\r') continue;
        if (ch == '\t') {
            g16_vglyph_t *sp = get_glyph(vf, ' ');
            cur_w += (sp && sp->advance_x ? sp->advance_x : (int)(vf->size * 0.5f)) * 4;
            prev_ch = 0;
            continue;
        }

        if (prev_ch && (unsigned char)ch >= 32 && (unsigned char)ch < 128) {
            int kern = stbtt_GetCodepointKernAdvance(info, prev_ch, ch);
            cur_w += (int)roundf(kern * vf->scale);
        }

        g16_vglyph_t *glyph = get_glyph(vf, (unsigned char)ch);
        if (glyph) {
            cur_w += glyph->advance_x;
        }
        prev_ch = ch;
    }
    if (cur_w > max_w) max_w = cur_w;

    if (out_w) *out_w = max_w;
    if (out_h) *out_h = lines * vf->line_height;
}
