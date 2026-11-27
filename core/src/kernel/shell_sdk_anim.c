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
    static const int most[4] = { SDK_ANIM_BIG + SDK_ANIM_SIDE, SDK_ANIM_SIDE, SDK_ANIM_FRAMES, 60 };
    if (it->count < 4)
        return sdk_list_wrong(r, it->line, "%s starts with width, height, frames, speed", it->name);
    for (int i = 0; i < 4; i++)
        if (number(r, &v[i], 1, most[i], head[i], &x[i]) != 0)
            return -1;
    const int big = (x[0] & SDK_ANIM_BIG) != 0;
    x[0] &= ~SDK_ANIM_BIG;
    if (x[0] < 1 || x[0] > SDK_ANIM_SIDE || (big && (x[0] & 1 || x[1] & 1)))
        return sdk_list_wrong(r, it->line, big ? "16x16 sprites need 2 or 4 tiles across and down"
                                               : "the width is 1 to 4 tiles");
    sdk_anim_new(a, it->name + 5);
    a->big = (uint8_t)big;
    a->line = it->line;
    a->wide = (uint8_t)x[0];
    a->high = (uint8_t)x[1];
    a->frames = (uint8_t)x[2];
    a->speed = (uint8_t)x[3];
    const int cells = sdk_anim_across(a) * sdk_anim_down(a), want = 4 + x[2] * cells * 2;
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

int sdk_anim_across(const sdk_anim_t *a) { return a->big ? a->wide / 2 : a->wide; }
int sdk_anim_down(const sdk_anim_t *a) { return a->big ? a->high / 2 : a->high; }
int sdk_anim_tile(const uint8_t cell[2]) { return cell[0] | (cell[1] & SDK_ANIM_HIGH ? 256 : 0); }

static void write_block(sdk_text_t *t, const sdk_anims_t *all, int c)
{
    const char *com = c ? "//" : ";";
    sdk_list_add(t, "%s@anim  Sprite tab: wide, high, frames, speed; then tile, attribute\n", com);
    for (int k = 0; k < all->count; k++) {
        const sdk_anim_t *a = &all->anim[k];
        sdk_list_t l;
        char name[24], note[48], line[160];
        snprintf(name, sizeof name, "anim_%s", a->name);
        snprintf(note, sizeof note, "%dx%d, %d frames%s", a->wide * 8, a->high * 8, a->frames,
                 a->big ? ", 16x16 sprites" : "");
        sdk_list_list_open(&l, t, c, name, note);
        char wide[8];
        snprintf(wide, sizeof wide, !a->big ? "%d" : c ? "0x%02X" : "$%02X", a->wide | (a->big ? SDK_ANIM_BIG : 0));
        snprintf(line, sizeof line, "%s, %d, %d, %d", wide, a->high, a->frames, a->speed);
        sdk_list_list_line(&l, line, a->big ? "$80+wide, high, frames, speed" : "wide, high, frames, speed");
        const int cells = sdk_anim_across(a) * sdk_anim_down(a);
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

/* A block of side x side pixels turned as the attribute says. */
static void turn(const uint8_t *in, int flip, uint8_t *out, int side)
{
    for (int y = 0; y < side; y++)
        for (int x = 0; x < side; x++)
            out[y * side + x] = in[(flip & SDK_ANIM_FLIP_Y ? side - 1 - y : y) * side +
                                   (flip & SDK_ANIM_FLIP_X ? side - 1 - x : x)];
}

static const sdk_tile_t *sheet_tile(int n)
{
    return sdk_sheet_tile(n / SDK_SHEET_W, n % SDK_SHEET_W);
}

/* The block of the cell (cx, cy) of a frame. */
static void put_block(sdk_anim_pixels_t pix, int frame, int cx, int cy, const uint8_t *block, int side)
{
    for (int y = 0; y < side; y++)
        memcpy(&pix[frame][cy * side + y][cx * side], block + y * side, (size_t)side);
}

static void get_block(sdk_anim_pixels_t pix, int frame, int cx, int cy, uint8_t *block, int side)
{
    for (int y = 0; y < side; y++)
        memcpy(block + y * side, &pix[frame][cy * side + y][cx * side], (size_t)side);
}

/* The tiles of a block: 1, or the 4 of a 16x16 one, and which tile the n-th is. */
static int block_tiles(int side) { return side == 16 ? 4 : 1; }
static int block_tile(int first, int n) { return first + (n & 1) + (n >> 1) * SDK_SHEET_W; }

void sdk_anim_load(const sdk_anim_t *a, sdk_anim_pixels_t pix)
{
    const int side = a->big ? 16 : 8, across = sdk_anim_across(a);
    memset(pix, 0, sizeof(sdk_anim_pixels_t));
    for (int f = 0; f < a->frames; f++)
        for (int y = 0; y < sdk_anim_down(a); y++)
            for (int x = 0; x < across; x++) {
                const uint8_t *c = a->cell[f][y * across + x];
                if (c[1] & SDK_ANIM_EMPTY)
                    continue;
                uint8_t block[256] = { 0 }, turned[256];
                for (int n = 0; n < block_tiles(side); n++) {
                    const int at = block_tile(sdk_anim_tile(c), n);
                    if (at >= SDK_ANIM_TILES || !sheet_tile(at)->used)
                        continue;
                    for (int i = 0; i < 64; i++)
                        block[(n >> 1) * 8 * side + i / 8 * side + (n & 1) * 8 + i % 8] = sheet_tile(at)->pix[i];
                }
                turn(block, c[1], turned, side);
                put_block(pix, f, x, y, turned, side);
            }
}

void sdk_anim_plan(const sdk_anim_t *a, sdk_anim_pixels_t pix, int reuse, sdk_anim_plan_t *p)
{
    static const uint8_t flips[4] = { 0, SDK_ANIM_FLIP_X, SDK_ANIM_FLIP_Y, SDK_ANIM_FLIP_X | SDK_ANIM_FLIP_Y };
    const int side = a->big ? 16 : 8, across = sdk_anim_across(a), size = side * side;
    p->count = 0;
    p->side = side;
    for (int f = 0; f < SDK_ANIM_FRAMES; f++)
        for (int i = 0; i < SDK_ANIM_CELLS; i++) {
            p->use[f][i] = -1;
            p->flip[f][i] = 0;
        }
    for (int f = 0; f < a->frames; f++)
        for (int y = 0; y < sdk_anim_down(a); y++)
            for (int x = 0; x < across; x++) {
                const int i = y * across + x;
                uint8_t block[256], turned[256];
                get_block(pix, f, x, y, block, side);
                int any = 0;
                for (int k = 0; k < size; k++)
                    any |= block[k];
                if (!any)
                    continue;                   /* nothing to show: no tile */
                for (int u = 0; reuse && u < p->count && p->use[f][i] < 0; u++)
                    for (int o = 0; o < 4; o++) {
                        turn(p->tile[u], flips[o], turned, side);
                        if (!memcmp(turned, block, (size_t)size)) {
                            p->use[f][i] = (int16_t)u;
                            p->flip[f][i] = flips[o];
                            break;
                        }
                    }
                if (p->use[f][i] < 0) {
                    memcpy(p->tile[p->count], block, (size_t)size);
                    p->use[f][i] = (int16_t)p->count++;
                }
            }
}

int sdk_anim_spots(const sdk_anim_plan_t *p, int start, int limit, const uint8_t *own, int *where)
{
    static uint8_t taken[SDK_ANIM_TILES];
    const int tiles = block_tiles(p->side);
    int n = 0;
    memset(taken, 0, sizeof taken);
    for (int t = start < 0 ? 0 : start; t < limit && n < p->count; t++) {
        if (tiles == 4 && (t % SDK_SHEET_W == SDK_SHEET_W - 1 || t + SDK_SHEET_W + 1 >= limit))
            continue;
        int free_here = 1;
        for (int k = 0; k < tiles; k++) {
            const int at = block_tile(t, k);
            free_here &= !taken[at] && (!sheet_tile(at)->used || (own && own[at]));
        }
        if (!free_here)
            continue;
        for (int k = 0; k < tiles; k++)
            taken[block_tile(t, k)] = 1;
        where[n++] = t;
    }
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
                for (int i = 0; i < sdk_anim_across(a) * sdk_anim_down(a); i++)
                    for (int n = 0; !(a->cell[f][i][1] & SDK_ANIM_EMPTY) && n < (a->big ? 4 : 1); n++) {
                        const int at = block_tile(sdk_anim_tile(a->cell[f][i]), n);
                        if (at < SDK_ANIM_TILES)
                            own[at] = pass == 0;
                    }
        }
}

void sdk_anim_place(sdk_anim_t *a, const sdk_anim_plan_t *p, const int *where, int format, int palette)
{
    for (int u = 0; u < p->count; u++)
        for (int n = 0; n < block_tiles(p->side); n++) {
            sdk_tile_t t = { .used = 1, .fmt = (uint8_t)format };
            for (int i = 0; i < 64; i++)
                t.pix[i] = p->tile[u][(n >> 1) * 8 * p->side + i / 8 * p->side + (n & 1) * 8 + i % 8];
            const int at = block_tile(where[u], n);
            sdk_sheet_set(at / SDK_SHEET_W, at % SDK_SHEET_W, &t);
        }
    const int cells = sdk_anim_across(a) * sdk_anim_down(a);
    for (int f = 0; f < SDK_ANIM_FRAMES; f++)
        for (int i = 0; i < SDK_ANIM_CELLS; i++) {
            const int u = f < a->frames && i < cells ? p->use[f][i] : -1;
            a->cell[f][i][0] = (uint8_t)(u < 0 ? 0 : where[u] & 0xFF);
            a->cell[f][i][1] = (uint8_t)(u < 0 ? SDK_ANIM_EMPTY : p->flip[f][i] | (palette & 7) |
                                                  (where[u] > 255 ? SDK_ANIM_HIGH : 0));
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
                    put_block(pix, f, x, y, t->pix, 8);
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

static const char snes_routine[] =
    "; ---- the animations of the Sprite tab: anim_tick once a frame, anim_draw ------\n"
    "; anim_draw: X (16 bits) the address of anim_NAME; anim_n the first sprite,\n"
    "; anim_x (2 bytes) and anim_y where it goes. It writes the 544 bytes of the\n"
    "; sprites at $0200, and takes the RAM $0780-$0793 and the divider $4204-$4206.\n"
    "anim_x    = $0780\n"
    "anim_y    = $0782\n"
    "anim_n    = $0783\n"
    "__an_t    = $0784          ; the frames counted, 2 bytes\n"
    "__an_cols = $0786\n"
    "__an_rows = $0787\n"
    "__an_step = $0788\n"
    "__an_big  = $0789\n"
    "__an_size = $078A          ; 2 bytes\n"
    "__an_r    = $078C\n"
    "__an_k    = $078D\n"
    "__an_s    = $078E          ; the sprite written\n"
    "__an_px   = $078F          ; 2 bytes: x of the cell\n"
    "__an_hi   = $0791\n"
    "__an_attr = $0792\n"
    "__an_mask = $0793\n"
    "__an_oam  = $0200\n"
    "    .a8\n"
    "    .i16\n"
    "anim_tick:\n"
    "    php\n"
    "    rep #$20\n"
    "    .a16\n"
    "    inc __an_t\n"
    "    plp\n"
    "    .a8\n"
    "    rts\n"
    "anim_draw:\n"
    "    php\n"
    "    rep #$10\n"
    "    sep #$20\n"
    "    .a8\n"
    "    .i16\n"
    "    phx\n"
    "    phy\n"
    "    ldy __an_t             ; the frame: ticks / speed, then the rest of / frames\n"
    "    sty $4204\n"
    "    lda $0003,x\n"
    "    sta $4206\n"
    "    jsr __an_wait\n"
    "    ldy $4214\n"
    "    sty $4204\n"
    "    lda $0002,x\n"
    "    sta $4206\n"
    "    jsr __an_wait\n"
    "    lda $4216\n"
    "    sta __an_k\n"
    "    lda $0000,x\n"
    "    and #$80\n"
    "    sta __an_big\n"
    "    lda $0000,x\n"
    "    and #$7F\n"
    "    sta __an_cols\n"
    "    lda $0001,x\n"
    "    sta __an_rows\n"
    "    lda #8\n"
    "    sta __an_step\n"
    "    lda __an_big\n"
    "    beq @sized\n"
    "    lsr __an_cols          ; 16x16 sprites: half the cells, twice as far apart\n"
    "    lsr __an_rows\n"
    "    lda #16\n"
    "    sta __an_step\n"
    "@sized:\n"
    "    lda __an_rows\n"
    "    sta __an_r\n"
    "    lda #0\n"
    "@times:\n"
    "    clc\n"
    "    adc __an_cols\n"
    "    dec __an_r\n"
    "    bne @times\n"
    "    asl a                  ; the bytes of a frame\n"
    "    sta __an_size\n"
    "    stz __an_size+1\n"
    "    rep #$20\n"
    "    .a16\n"
    "    txa\n"
    "    clc\n"
    "    adc #4\n"
    "    tax\n"
    "@frame:\n"
    "    sep #$20\n"
    "    .a8\n"
    "    lda __an_k\n"
    "    beq @draw\n"
    "    dec __an_k\n"
    "    rep #$20\n"
    "    .a16\n"
    "    txa\n"
    "    clc\n"
    "    adc __an_size\n"
    "    tax\n"
    "    bra @frame\n"
    "@draw:\n"
    "    .a8\n"
    "    stz __an_r\n"
    "    lda anim_n\n"
    "    sta __an_s\n"
    "@line:\n"
    "    stz __an_k\n"
    "    lda anim_x\n"
    "    sta __an_px\n"
    "    lda anim_x+1\n"
    "    sta __an_px+1\n"
    "@cell:\n"
    "    jsr __an_index         ; Y = the 4 bytes of the sprite\n"
    "    lda $0001,x\n"
    "    and #$10               ; an empty cell: the sprite below the screen\n"
    "    beq @shown\n"
    "    lda #$F0\n"
    "    sta __an_oam+1,y\n"
    "    jmp @next\n"
    "@shown:\n"
    "    lda __an_px\n"
    "    sta __an_oam,y\n"
    "    lda __an_r\n"
    "    asl a\n"
    "    asl a\n"
    "    asl a\n"
    "    sta __an_attr\n"
    "    lda __an_big\n"
    "    beq @low_y\n"
    "    asl __an_attr\n"
    "@low_y:\n"
    "    lda __an_attr\n"
    "    clc\n"
    "    adc anim_y\n"
    "    sec\n"
    "    sbc #1                 ; a sprite shows one line lower\n"
    "    sta __an_oam+1,y\n"
    "    lda $0000,x\n"
    "    sta __an_oam+2,y\n"
    "    lda $0001,x            ; the attribute: upside down, mirrored, in front, palette, tile bit 8\n"
    "    and #$07\n"
    "    asl a\n"
    "    sta __an_attr\n"
    "    lda $0001,x\n"
    "    and #$C0\n"
    "    ora __an_attr\n"
    "    ora #$30\n"
    "    sta __an_attr\n"
    "    lda $0001,x\n"
    "    and #$08\n"
    "    beq @low_tile\n"
    "    lda #1\n"
    "@low_tile:\n"
    "    ora __an_attr\n"
    "    sta __an_oam+3,y\n"
    "    lda __an_px+1          ; the 2 bits of the sprite in the high table: x bit 8, size\n"
    "    and #$01\n"
    "    sta __an_hi\n"
    "    lda __an_big\n"
    "    beq @small\n"
    "    lda __an_hi\n"
    "    ora #$02\n"
    "    sta __an_hi\n"
    "@small:\n"
    "    jsr __an_high\n"
    "@next:\n"
    "    inx\n"
    "    inx\n"
    "    inc __an_s\n"
    "    lda __an_px\n"
    "    clc\n"
    "    adc __an_step\n"
    "    sta __an_px\n"
    "    lda __an_px+1\n"
    "    adc #0\n"
    "    sta __an_px+1\n"
    "    inc __an_k\n"
    "    lda __an_k\n"
    "    cmp __an_cols\n"
    "    beq @row_done\n"
    "    jmp @cell\n"
    "@row_done:\n"
    "    inc __an_r\n"
    "    lda __an_r\n"
    "    cmp __an_rows\n"
    "    beq @done\n"
    "    jmp @line\n"
    "@done:\n"
    "    ply\n"
    "    plx\n"
    "    plp\n"
    "    rts\n"
    "__an_wait:                 ; the divider takes 16 cycles\n"
    "    nop\n"
    "    nop\n"
    "    nop\n"
    "    rts\n"
    "__an_index:\n"
    "    rep #$20\n"
    "    .a16\n"
    "    lda __an_s\n"
    "    and #$00FF\n"
    "    asl a\n"
    "    asl a\n"
    "    tay\n"
    "    sep #$20\n"
    "    .a8\n"
    "    rts\n"
    "__an_high:\n"
    "    lda __an_s\n"
    "    and #$03\n"
    "    asl a\n"
    "    sta __an_attr\n"
    "    lda #$03\n"
    "    sta __an_mask\n"
    "@shift:\n"
    "    lda __an_attr\n"
    "    beq @ready\n"
    "    asl __an_mask\n"
    "    asl __an_hi\n"
    "    dec __an_attr\n"
    "    bra @shift\n"
    "@ready:\n"
    "    rep #$20\n"
    "    .a16\n"
    "    lda __an_s\n"
    "    and #$00FF\n"
    "    lsr a\n"
    "    lsr a\n"
    "    tay\n"
    "    sep #$20\n"
    "    .a8\n"
    "    lda __an_mask\n"
    "    eor #$FF\n"
    "    and __an_oam+512,y\n"
    "    ora __an_hi\n"
    "    sta __an_oam+512,y\n"
    "    rts\n"
    ;

int sdk_anim_expand(const char *src, size_t len, int snes, sdk_anim_src_t *s, char *err, size_t en)
{
    memset(s, 0, sizeof *s);
    if (!sdk_anim_called(src, len, 0))
        return 0;                               /* the program draws no animation: the source as it is */
    size_t from, to;
    if (snes || sdk_list_find(src, len, 0, "anim", &from, &to) <= 0)
        to = len;
    const char *code = snes ? snes_routine : routine;
    const size_t n = strlen(code), gap = to == len && len && src[len - 1] != '\n';
    s->text = malloc(len + n + 2);
    if (!s->text) {
        snprintf(err, en, "main.s:1: not enough memory for the animations");
        return -1;
    }
    memcpy(s->text, src, to);
    if (gap)
        s->text[to] = '\n';
    memcpy(s->text + to + gap, code, n);
    memcpy(s->text + to + gap + n, src + to, len - to);
    s->len = to + gap + n + (len - to);
    s->text[s->len] = 0;
    for (size_t i = 0; i < to + gap; i++)
        s->first += s->text[i] == '\n';
    s->first++;
    for (size_t i = 0; i < n; i++)
        s->lines += code[i] == '\n';
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
    "    byte __big = __a[0] & 0x80;\n"
    "    byte __w = __a[0] & 0x7F;\n"
    "    byte __h = __a[1];\n"
    "    byte __step = 8;\n"
    "    if (__big) { __w >>= 1; __h >>= 1; __step = 16; }\n"
    "    byte __f = __anim_t / __a[3] % __a[2];\n"
    "    word __skip = 4 + __f * (word)(__w * __h * 2);\n"
    "    const byte *__c = __a + __skip;\n"
    "    for (byte __r = 0; __r < __h; __r++)\n"
    "        for (byte __k = 0; __k < __w; __k++) {\n"
    "            if (__c[1] & 0x10) hide_sprite(__n);\n"
    "            else {\n"
    "                word __t = __c[0];\n"
    "                if (__c[1] & 0x08) __t += 256;\n"
    "                sprite(__n, __x + __k * __step, __y + __r * __step, __t);\n"
    "                sprite_style(__n, __c[1] & 0xC7);\n"
    "                sprite_big(__n, __big);\n"
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
