/*
 * Shell SDK: the animations (see shell_sdk_anim.h). Original code.
 */
#include "shell_sdk_anim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell_sdk_lists.h"
#include "shell_sdk_sheet.h"

void sdk_anim_new(sdk_anim_t *a, const char *name)
{
    memset(a, 0, sizeof *a);
    snprintf(a->name, sizeof a->name, "%s", name);
    a->wide = a->high = 2;
    a->frames = 4;
    a->speed = 8;
    for (int f = 0; f < SDK_ANIM_FRAMES; f++)
        for (int i = 0; i < SDK_ANIM_CELLS; i++)
            a->cell[f][i][1] = SDK_ANIM_EMPTY;
}

int sdk_anim_name_ok(const char *name)
{
    const size_t n = strlen(name);
    if (!n || n >= SDK_ANIM_NAME || !sdk_list_word_char((unsigned char)name[0]) ||
        (name[0] >= '0' && name[0] <= '9'))
        return 0;
    for (size_t i = 1; i < n; i++)
        if (!sdk_list_word_char((unsigned char)name[i]))
            return 0;
    return 1;
}

/* ---- the code ---- */

/* One value as a number 0 to hi. */
static int number(sdk_reader_t *r, const sdk_value_t *v, int lo, int hi, const char *what, int *out)
{
    if (v->word[0] || v->num < lo || v->num > hi)
        return sdk_list_wrong(r, v->line, "%s is a number %d to %d", what, lo, hi);
    *out = v->num;
    return 0;
}

static int read_one(sdk_reader_t *r, const sdk_item_t *it, sdk_anim_t *a)
{
    const sdk_value_t *v = r->value + it->first;
    int x[4];
    static const char *const head[4] = { "the width (tiles)", "the height (tiles)", "the frames", "the speed" };
    static const int most[4] = { SDK_ANIM_SIDE, SDK_ANIM_SIDE, SDK_ANIM_FRAMES, 60 };
    if (it->count < 4)
        return sdk_list_wrong(r, it->line, "%s starts with width, height, frames, speed", it->name);
    for (int i = 0; i < 4; i++)
        if (number(r, &v[i], 1, most[i], head[i], &x[i]) != 0)
            return -1;
    sdk_anim_new(a, it->name + 5);
    a->line = it->line;
    a->wide = (uint8_t)x[0];
    a->high = (uint8_t)x[1];
    a->frames = (uint8_t)x[2];
    a->speed = (uint8_t)x[3];
    const int cells = x[0] * x[1], want = 4 + x[2] * cells * 2;
    if (it->count != want)
        return sdk_list_wrong(r, it->line, "%s has %d values: %d frames of %dx%d tiles are %d", it->name, it->count,
                              x[2], x[0], x[1], want);
    v += 4;
    for (int f = 0; f < a->frames; f++)
        for (int i = 0; i < cells; i++, v += 2) {
            int tile = 0, attr = 0;
            if (number(r, &v[0], 0, 255, "a tile", &tile) != 0 ||
                number(r, &v[1], 0, 255, "an attribute", &attr) != 0)
                return -1;
            a->cell[f][i][0] = (uint8_t)tile;
            a->cell[f][i][1] = (uint8_t)attr;
        }
    return 0;
}

int sdk_anim_parse(const char *src, size_t len, int c, sdk_anims_t *out, char *err, size_t en)
{
    memset(out, 0, sizeof *out);
    sdk_reader_t *r = NULL;
    const int found = sdk_list_read(src, len, c, "anim", &r, err, en);
    if (found <= 0)
        return found;
    int rc = 0;
    for (int i = 0; i < r->items && rc == 0; i++) {
        const sdk_item_t *it = &r->item[i];
        if (strncmp(it->name, "anim_", 5) || !sdk_anim_name_ok(it->name + 5))
            rc = sdk_list_wrong(r, it->line, "'%s': an animation is named anim_NAME", it->name);
        else if (out->count == SDK_ANIM_MAX)
            rc = sdk_list_wrong(r, it->line, "%d animations at most", SDK_ANIM_MAX);
        else if ((rc = read_one(r, it, &out->anim[out->count])) == 0)
            out->count++;
    }
    if (rc != 0)
        memset(out, 0, sizeof *out);
    sdk_list_free(r);
    return rc;
}

static void write_block(sdk_text_t *t, const sdk_anims_t *all, int c)
{
    const char *com = c ? "//" : ";";
    sdk_list_add(t, "%s@anim  Sprite tab: wide, high, frames, speed; then tile, attribute\n", com);
    for (int k = 0; k < all->count; k++) {
        const sdk_anim_t *a = &all->anim[k];
        sdk_list_t l;
        char name[24], note[48], line[160];
        snprintf(name, sizeof name, "anim_%s", a->name);
        snprintf(note, sizeof note, "%dx%d, %d frames", a->wide * 8, a->high * 8, a->frames);
        sdk_list_list_open(&l, t, c, name, note);
        snprintf(line, sizeof line, "%d, %d, %d, %d", a->wide, a->high, a->frames, a->speed);
        sdk_list_list_line(&l, line, "wide, high, frames, speed");
        const int cells = a->wide * a->high;
        for (int f = 0; f < a->frames; f++) {
            size_t at = 0;
            for (int i = 0; i < cells; i++) {
                at = sdk_list_put(line, at, sizeof line, c ? "%s0x%02X,0x%02X" : "%s$%02X,$%02X",
                                  at ? ", " : "", a->cell[f][i][0], a->cell[f][i][1]);
                if (i % 4 == 3 || i == cells - 1) {
                    char frame[16] = "";
                    if (i < 4)
                        snprintf(frame, sizeof frame, "frame %d", f + 1);
                    sdk_list_list_line(&l, line, frame);
                    at = 0;
                }
            }
        }
        sdk_list_list_flush(&l, 1);
    }
    sdk_list_add(t, "%s@end\n", com);
}

long sdk_anim_text(const sdk_anims_t *a, int c, char *out, size_t max)
{
    sdk_text_t t = { out, max, 0, 0, 0 };
    if (max)
        out[0] = 0;
    write_block(&t, a, c);
    return t.full ? -1 : (long)t.len;
}

int sdk_anim_commit(const sdk_anims_t *a, int c, int run, char *err, size_t en)
{
    sdk_text_t t = { NULL, 0, 0, 0, 1 };
    write_block(&t, a, c);
    int rc = -1;
    if (t.full)
        snprintf(err, en, "not enough memory");
    else
        rc = sdk_list_commit("anim", c, t.s, t.len, run, err, en);
    free(t.s);
    return rc;
}

/* ---- the pixels ---- */

/* A tile turned as the attribute says. */
static void turn(const uint8_t in[64], int flip, uint8_t out[64])
{
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            out[y * 8 + x] = in[(flip & SDK_ANIM_FLIP_Y ? 7 - y : y) * 8 + (flip & SDK_ANIM_FLIP_X ? 7 - x : x)];
}

static const sdk_tile_t *sheet_tile(int n)
{
    return sdk_sheet_tile(n / SDK_SHEET_W, n % SDK_SHEET_W);
}

static void put_block(sdk_anim_pixels_t pix, int frame, int cx, int cy, const uint8_t tile[64])
{
    for (int y = 0; y < 8; y++)
        memcpy(&pix[frame][cy * 8 + y][cx * 8], tile + y * 8, 8);
}

static void get_block(sdk_anim_pixels_t pix, int frame, int cx, int cy, uint8_t tile[64])
{
    for (int y = 0; y < 8; y++)
        memcpy(tile + y * 8, &pix[frame][cy * 8 + y][cx * 8], 8);
}

void sdk_anim_load(const sdk_anim_t *a, sdk_anim_pixels_t pix)
{
    memset(pix, 0, sizeof(sdk_anim_pixels_t));
    for (int f = 0; f < a->frames; f++)
        for (int y = 0; y < a->high; y++)
            for (int x = 0; x < a->wide; x++) {
                const uint8_t *c = a->cell[f][y * a->wide + x];
                const sdk_tile_t *t = sheet_tile(c[0]);
                if (c[1] & SDK_ANIM_EMPTY || !t->used)
                    continue;
                uint8_t turned[64];
                turn(t->pix, c[1], turned);
                put_block(pix, f, x, y, turned);
            }
}

void sdk_anim_plan(const sdk_anim_t *a, sdk_anim_pixels_t pix, int reuse, sdk_anim_plan_t *p)
{
    static const uint8_t flips[4] = { 0, SDK_ANIM_FLIP_X, SDK_ANIM_FLIP_Y, SDK_ANIM_FLIP_X | SDK_ANIM_FLIP_Y };
    p->count = 0;
    for (int f = 0; f < SDK_ANIM_FRAMES; f++)
        for (int i = 0; i < SDK_ANIM_CELLS; i++) {
            p->use[f][i] = -1;
            p->flip[f][i] = 0;
        }
    for (int f = 0; f < a->frames; f++)
        for (int y = 0; y < a->high; y++)
            for (int x = 0; x < a->wide; x++) {
                const int i = y * a->wide + x;
                uint8_t block[64], turned[64];
                get_block(pix, f, x, y, block);
                int any = 0;
                for (int k = 0; k < 64; k++)
                    any |= block[k];
                if (!any)
                    continue;                   /* nothing to show: no tile */
                for (int u = 0; reuse && u < p->count && p->use[f][i] < 0; u++)
                    for (int o = 0; o < 4; o++) {
                        turn(p->tile[u], flips[o], turned);
                        if (!memcmp(turned, block, 64)) {
                            p->use[f][i] = (int16_t)u;
                            p->flip[f][i] = flips[o];
                            break;
                        }
                    }
                if (p->use[f][i] < 0) {
                    memcpy(p->tile[p->count], block, 64);
                    p->use[f][i] = (int16_t)p->count++;
                }
            }
}

int sdk_anim_spots(const sdk_anim_plan_t *p, int start, const uint8_t *own, int *where)
{
    int n = 0;
    for (int t = start < 0 ? 0 : start; t < SDK_ANIM_TILES && n < p->count; t++)
        if (!sheet_tile(t)->used || (own && own[t]))
            where[n++] = t;
    return n;
}

void sdk_anim_own(const sdk_anims_t *all, int which, uint8_t own[SDK_ANIM_TILES])
{
    memset(own, 0, SDK_ANIM_TILES);
    for (int pass = 0; pass < 2; pass++)        /* its tiles, then not those that another one names */
        for (int k = 0; k < all->count; k++) {
            const sdk_anim_t *a = &all->anim[k];
            if ((k == which) != (pass == 0))
                continue;
            for (int f = 0; f < a->frames; f++)
                for (int i = 0; i < a->wide * a->high; i++)
                    if (!(a->cell[f][i][1] & SDK_ANIM_EMPTY))
                        own[a->cell[f][i][0]] = pass == 0;
        }
}

void sdk_anim_place(sdk_anim_t *a, const sdk_anim_plan_t *p, const int *where, int format, int palette)
{
    for (int u = 0; u < p->count; u++) {
        sdk_tile_t t = { .used = 1, .fmt = (uint8_t)format };
        memcpy(t.pix, p->tile[u], 64);
        sdk_sheet_set(where[u] / SDK_SHEET_W, where[u] % SDK_SHEET_W, &t);
    }
    for (int f = 0; f < SDK_ANIM_FRAMES; f++)
        for (int i = 0; i < SDK_ANIM_CELLS; i++) {
            const int u = f < a->frames && i < a->wide * a->high ? p->use[f][i] : -1;
            a->cell[f][i][0] = (uint8_t)(u < 0 ? 0 : where[u]);
            a->cell[f][i][1] = (uint8_t)(u < 0 ? SDK_ANIM_EMPTY : p->flip[f][i] | (palette & 3));
        }
}

int sdk_anim_import(const sdk_anim_t *a, int start, sdk_anim_pixels_t pix)
{
    const int column = start % SDK_SHEET_W, row = start / SDK_SHEET_W;
    if (column + a->frames * a->wide > SDK_SHEET_W || row + a->high > SDK_SHEET_H)
        return -1;
    memset(pix, 0, sizeof(sdk_anim_pixels_t));
    for (int f = 0; f < a->frames; f++)
        for (int y = 0; y < a->high; y++)
            for (int x = 0; x < a->wide; x++) {
                const sdk_tile_t *t = sdk_sheet_tile(row + y, column + f * a->wide + x);
                if (t->used)
                    put_block(pix, f, x, y, t->pix);
            }
    return 0;
}

void sdk_anim_flip(sdk_anim_pixels_t pix, int frame, int x0, int y0, int x1, int y1, int x)
{
    if (x)
        for (int r = y0; r <= y1; r++)
            for (int l = x0, h = x1; l < h; l++, h--) {
                const uint8_t v = pix[frame][r][l];
                pix[frame][r][l] = pix[frame][r][h];
                pix[frame][r][h] = v;
            }
    else
        for (int c = x0; c <= x1; c++)
            for (int l = y0, h = y1; l < h; l++, h--) {
                const uint8_t v = pix[frame][l][c];
                pix[frame][l][c] = pix[frame][h][c];
                pix[frame][h][c] = v;
            }
}

/* ---- the recall from the program ---- */

static const char routine[] =
    "; ---- the animations of the Sprite tab: anim_tick once a frame, anim_draw ------\n"
    "; anim_draw: A and Y the address (low, high) of anim_NAME; anim_n the first\n"
    "; sprite, anim_x and anim_y where it goes. It writes the sprites at $0200.\n"
    "; It takes the zero page $FA-$FB and the RAM $0780-$078F.\n"
    "__an_p    = $FA\n"
    "anim_x    = $0780\n"
    "anim_y    = $0781\n"
    "anim_n    = $0782\n"
    "__an_t    = $0783          ; the frames counted, 2 bytes\n"
    "__an_by   = $0785\n"
    "__an_q    = $0786          ; 2 bytes\n"
    "__an_size = $0788\n"
    "__an_w    = $0789\n"
    "__an_h    = $078A\n"
    "__an_row  = $078B\n"
    "__an_col  = $078C\n"
    "__an_o    = $078D\n"
    "anim_tick:\n"
    "    inc __an_t\n"
    "    bne @done\n"
    "    inc __an_t+1\n"
    "@done:\n"
    "    rts\n"
    "anim_draw:\n"
    "    sta __an_p\n"
    "    sty __an_p+1\n"
    "    txa\n"
    "    pha\n"
    "    lda __an_t\n"
    "    sta __an_q\n"
    "    lda __an_t+1\n"
    "    sta __an_q+1\n"
    "    ldy #3\n"
    "    lda (__an_p),y         ; speed\n"
    "    jsr __an_div\n"
    "    ldy #2\n"
    "    lda (__an_p),y         ; frames: the rest is the frame to show\n"
    "    jsr __an_div\n"
    "    tax\n"
    "    ldy #0\n"
    "    lda (__an_p),y\n"
    "    sta __an_w\n"
    "    iny\n"
    "    lda (__an_p),y\n"
    "    sta __an_h\n"
    "    lda __an_p\n"
    "    clc\n"
    "    adc #4\n"
    "    sta __an_p\n"
    "    bcc @sized\n"
    "    inc __an_p+1\n"
    "@sized:\n"
    "    lda #0\n"
    "    ldy __an_h\n"
    "@times:\n"
    "    clc\n"
    "    adc __an_w\n"
    "    dey\n"
    "    bne @times\n"
    "    asl a                  ; the bytes of a frame\n"
    "    sta __an_size\n"
    "    cpx #0\n"
    "    beq @draw\n"
    "@skip:\n"
    "    lda __an_p\n"
    "    clc\n"
    "    adc __an_size\n"
    "    sta __an_p\n"
    "    lda __an_p+1\n"
    "    adc #0\n"
    "    sta __an_p+1\n"
    "    dex\n"
    "    bne @skip\n"
    "@draw:\n"
    "    lda anim_n\n"
    "    asl a\n"
    "    asl a\n"
    "    sta __an_o\n"
    "    lda #0\n"
    "    sta __an_row\n"
    "@line:\n"
    "    lda #0\n"
    "    sta __an_col\n"
    "@cell:\n"
    "    ldx __an_o\n"
    "    ldy #1\n"
    "    lda (__an_p),y\n"
    "    and #$10               ; an empty cell: the sprite off the screen\n"
    "    beq @shown\n"
    "    lda #$FF\n"
    "    sta $0200,x\n"
    "    jmp @next\n"
    "@shown:\n"
    "    lda __an_row\n"
    "    asl a\n"
    "    asl a\n"
    "    asl a\n"
    "    clc\n"
    "    adc anim_y\n"
    "    sec\n"
    "    sbc #1                 ; a sprite shows one line lower\n"
    "    sta $0200,x\n"
    "    ldy #0\n"
    "    lda (__an_p),y\n"
    "    sta $0201,x\n"
    "    iny\n"
    "    lda (__an_p),y\n"
    "    and #$C3\n"
    "    sta $0202,x\n"
    "    lda __an_col\n"
    "    asl a\n"
    "    asl a\n"
    "    asl a\n"
    "    clc\n"
    "    adc anim_x\n"
    "    sta $0203,x\n"
    "@next:\n"
    "    lda __an_o\n"
    "    clc\n"
    "    adc #4\n"
    "    sta __an_o\n"
    "    lda __an_p\n"
    "    clc\n"
    "    adc #2\n"
    "    sta __an_p\n"
    "    bcc @same\n"
    "    inc __an_p+1\n"
    "@same:\n"
    "    inc __an_col\n"
    "    lda __an_col\n"
    "    cmp __an_w\n"
    "    bne @cell\n"
    "    inc __an_row\n"
    "    lda __an_row\n"
    "    cmp __an_h\n"
    "    bne @line\n"
    "    pla\n"
    "    tax\n"
    "    rts\n"
    "; __an_q / A (1 to 60): the quotient in __an_q, the rest in A\n"
    "__an_div:\n"
    "    sta __an_by\n"
    "    lda #0\n"
    "    ldx #16\n"
    "@bit:\n"
    "    asl __an_q\n"
    "    rol __an_q+1\n"
    "    rol a\n"
    "    cmp __an_by\n"
    "    bcc @under\n"
    "    sbc __an_by\n"
    "    inc __an_q\n"
    "@under:\n"
    "    dex\n"
    "    bne @bit\n"
    "    rts\n"
    ;

int sdk_anim_expand(const char *src, size_t len, sdk_anim_src_t *s, char *err, size_t en)
{
    memset(s, 0, sizeof *s);
    if (!sdk_anim_called(src, len, 0))
        return 0;                               /* the program draws no animation: the source as it is */
    size_t from, to;
    if (sdk_list_find(src, len, 0, "anim", &from, &to) <= 0)
        to = len;
    const size_t n = sizeof routine - 1, gap = to == len && len && src[len - 1] != '\n';
    s->text = malloc(len + n + 2);
    if (!s->text) {
        snprintf(err, en, "main.s:1: not enough memory for the animations");
        return -1;
    }
    memcpy(s->text, src, to);
    if (gap)
        s->text[to] = '\n';
    memcpy(s->text + to + gap, routine, n);
    memcpy(s->text + to + gap + n, src + to, len - to);
    s->len = to + gap + n + (len - to);
    s->text[s->len] = 0;
    for (size_t i = 0; i < to + gap; i++)
        s->first += s->text[i] == '\n';
    s->first++;
    for (size_t i = 0; i < n; i++)
        s->lines += routine[i] == '\n';
    return 0;
}

void sdk_anim_error_line(const sdk_anim_src_t *s, char *err, size_t en)
{
    const char *colon = strchr(err, ':');
    const int at = colon ? atoi(colon + 1) : 0;
    const char *why = colon ? strchr(colon + 1, ':') : NULL;
    if (!s->text || at < s->first || !why)
        return;
    char msg[160];
    if (at < s->first + s->lines)
        snprintf(msg, sizeof msg, "main.s:%d: in the animation routine:%s", s->first, why + 1);
    else
        snprintf(msg, sizeof msg, "main.s:%d:%s", at - s->lines, why + 1);
    snprintf(err, en, "%s", msg);
}

/* animate() of bm C, written in bm C and put after the program that calls it. */
static const char animate_nes[] =
    "\n"
    "word __anim_t;\n"
    "byte __anim_seen;\n"
    "void animate(byte __n, byte __x, byte __y, const byte *__a)\n"
    "{\n"
    "    __anim_t += (byte)(frame - __anim_seen);\n"
    "    __anim_seen = frame;\n"
    "    byte __w = __a[0];\n"
    "    byte __h = __a[1];\n"
    "    byte __f = __anim_t / __a[3] % __a[2];\n"
    "    word __skip = 4 + __f * (word)(__w * __h * 2);\n"
    "    const byte *__c = __a + __skip;\n"
    "    for (byte __r = 0; __r < __h; __r++)\n"
    "        for (byte __k = 0; __k < __w; __k++) {\n"
    "            if (__c[1] & 0x10) hide_sprite(__n);\n"
    "            else {\n"
    "                sprite(__n, __x + __k * 8, __y + __r * 8, __c[0]);\n"
    "                sprite_style(__n, __c[1]);\n"
    "            }\n"
    "            __n++;\n"
    "            __c += 2;\n"
    "        }\n"
    "}\n"
    ;
static const char animate_snes[] =
    "\n"
    "word __anim_t;\n"
    "byte __anim_seen;\n"
    "void animate(byte __n, int __x, byte __y, const byte *__a)\n"
    "{\n"
    "    __anim_t += (byte)(frame - __anim_seen);\n"
    "    __anim_seen = frame;\n"
    "    byte __w = __a[0];\n"
    "    byte __h = __a[1];\n"
    "    byte __f = __anim_t / __a[3] % __a[2];\n"
    "    word __skip = 4 + __f * (word)(__w * __h * 2);\n"
    "    const byte *__c = __a + __skip;\n"
    "    for (byte __r = 0; __r < __h; __r++)\n"
    "        for (byte __k = 0; __k < __w; __k++) {\n"
    "            if (__c[1] & 0x10) hide_sprite(__n);\n"
    "            else {\n"
    "                sprite(__n, __x + __k * 8, __y + __r * 8, __c[0]);\n"
    "                sprite_style(__n, __c[1]);\n"
    "            }\n"
    "            __n++;\n"
    "            __c += 2;\n"
    "        }\n"
    "}\n"
    ;

const char *sdk_anim_prelude(int snes) { return snes ? animate_snes : animate_nes; }

int sdk_anim_called(const char *src, size_t len, int c)
{
    static const char *const calls[] = { "anim_draw", "anim_tick" };
    int called = 0;
    for (size_t i = 0; i < len;) {
        if ((!c && src[i] == ';') || (c && src[i] == '/' && i + 1 < len && src[i + 1] == '/')) {
            while (i < len && src[i] != '\n')
                i++;
        } else if (c && src[i] == '/' && i + 1 < len && src[i + 1] == '*') {
            for (i += 2; i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'); i++)
                ;
            i += 2;
        } else if (sdk_list_word_char((unsigned char)src[i])) {
            size_t e = i;
            while (e < len && sdk_list_word_char((unsigned char)src[e]))
                e++;
            if (!c) {
                for (int k = 0; k < 2; k++)
                    if (strlen(calls[k]) == e - i && !memcmp(calls[k], src + i, e - i))
                        return 1;
            } else if (e - i == 7 && !memcmp("animate", src + i, 7)) {
                size_t b = i;                   /* "void animate(": the program has its own */
                while (b > 0 && (src[b - 1] == ' ' || src[b - 1] == '\t' || src[b - 1] == '\n'))
                    b--;
                if (b >= 4 && !memcmp("void", src + b - 4, 4))
                    return 0;
                size_t p = e;
                while (p < len && src[p] == ' ')
                    p++;
                called |= p < len && src[p] == '(';
            }
            i = e;
        } else
            i++;
    }
    return called;
}
