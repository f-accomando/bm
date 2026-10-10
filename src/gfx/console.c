#include "console.h"

#define MAX_COLS    240     /* 1920 / 8 */
#define MAX_ROWS    68      /* 1080 / 16 */
#define STATUS_ROW  0
#define TOP_ROW     1
#define TAB_WIDTH   8

#define ATTR(fg, bg)    ((uint8_t)(((bg) << 4) | ((fg) & 0xF)))
#define ATTR_FG(a)      ((a) & 0xF)
#define ATTR_BG(a)      ((a) >> 4)
#define ATTR_DEFAULT    ATTR(COLOR_LIGHT_GREY, COLOR_BLACK)
#define ATTR_STATUS     ATTR(COLOR_BLACK, COLOR_CYAN)

typedef struct {
    uint8_t ch;
    uint8_t attr;
} cell_t;

static const uint8_t ansi_rgb[16][3] = {
    {0, 0, 0},       {170, 0, 0},     {0, 170, 0},     {170, 85, 0},
    {0, 0, 170},     {170, 0, 170},   {0, 170, 170},   {170, 170, 170},
    {85, 85, 85},    {255, 85, 85},   {85, 255, 85},   {255, 255, 85},
    {85, 85, 255},   {255, 85, 255},  {85, 255, 255},  {255, 255, 255},
};

static struct {
    framebuffer_t *fb;
    const font_t *font;
    uint32_t cols, rows;
    uint32_t cx, cy;
    uint8_t attr;
    int active;
    int suspended;
    int cursor_drawn;
    uint32_t pal[16];
    cell_t cells[MAX_ROWS][MAX_COLS];

    int esc;                /* 0 none, 1 after ESC, 2 inside CSI */
    int params[8];
    int nparams;
} con;

static void draw_cell(uint32_t col, uint32_t row, int cursor)
{
    if (con.suspended)
        return;

    const cell_t *c = &con.cells[row][col];
    const uint32_t h = con.font->height;
    const uint8_t *g = con.font->glyphs + c->ch * h;
    uint32_t fg = con.pal[ATTR_FG(c->attr)];
    uint32_t bg = con.pal[ATTR_BG(c->attr)];
    uint8_t *line = con.fb->base + row * h * con.fb->pitch + col * 8 * 4;

    for (uint32_t y = 0; y < h; y++, line += con.fb->pitch) {
        uint32_t bits = g[y];
        if (cursor && y >= h - 2)
            bits = 0xFF;
        uint32_t *p = (uint32_t *)line;
        p[0] = bits & 0x80 ? fg : bg;
        p[1] = bits & 0x40 ? fg : bg;
        p[2] = bits & 0x20 ? fg : bg;
        p[3] = bits & 0x10 ? fg : bg;
        p[4] = bits & 0x08 ? fg : bg;
        p[5] = bits & 0x04 ? fg : bg;
        p[6] = bits & 0x02 ? fg : bg;
        p[7] = bits & 0x01 ? fg : bg;
    }
}

static void redraw_all(void)
{
    if (con.suspended)
        return;
    /* Clear the margins the cell grid does not cover. */
    uint32_t bg = con.pal[ATTR_BG(con.attr)];
    uint32_t gw = con.cols * 8, gh = con.rows * con.font->height;
    fb_fill_rect(con.fb, gw, 0, con.fb->width - gw, con.fb->height, bg);
    fb_fill_rect(con.fb, 0, gh, gw, con.fb->height - gh, bg);

    for (uint32_t r = 0; r < con.rows; r++)
        for (uint32_t c = 0; c < con.cols; c++)
            draw_cell(c, r, 0);
    con.cursor_drawn = 0;
}

static void cursor_hide(void)
{
    if (con.cursor_drawn) {
        draw_cell(con.cx, con.cy, 0);
        con.cursor_drawn = 0;
    }
}

static void cursor_show(void)
{
    if (!con.suspended && con.cx < con.cols) {
        draw_cell(con.cx, con.cy, 1);
        con.cursor_drawn = 1;
    }
}

static void clear_row(uint32_t row, uint32_t from)
{
    for (uint32_t c = from; c < con.cols; c++) {
        con.cells[row][c].ch = ' ';
        con.cells[row][c].attr = con.attr;
        draw_cell(c, row, 0);
    }
}

static void scroll_up(void)
{
    for (uint32_t r = TOP_ROW; r + 1 < con.rows; r++)
        for (uint32_t c = 0; c < con.cols; c++)
            con.cells[r][c] = con.cells[r + 1][c];
    for (uint32_t c = 0; c < con.cols; c++) {
        con.cells[con.rows - 1][c].ch = ' ';
        con.cells[con.rows - 1][c].attr = con.attr;
    }
    for (uint32_t r = TOP_ROW; r < con.rows; r++)
        for (uint32_t c = 0; c < con.cols; c++)
            draw_cell(c, r, 0);
}

static void newline(void)
{
    con.cx = 0;
    if (++con.cy >= con.rows) {
        con.cy = con.rows - 1;
        scroll_up();
    }
}

static void sgr(void)
{
    if (con.nparams == 0)
        con.params[con.nparams++] = 0;

    for (int i = 0; i < con.nparams; i++) {
        int p = con.params[i];
        uint8_t fg = ATTR_FG(con.attr), bg = ATTR_BG(con.attr);

        if (p == 0)                     { fg = ATTR_FG(ATTR_DEFAULT); bg = ATTR_BG(ATTR_DEFAULT); }
        else if (p == 1)                { fg |= 8; }
        else if (p == 7)                { uint8_t t = fg; fg = bg; bg = t; }
        else if (p >= 30 && p <= 37)    { fg = (uint8_t)(p - 30) | (fg & 8); }
        else if (p == 39)               { fg = ATTR_FG(ATTR_DEFAULT); }
        else if (p >= 40 && p <= 47)    { bg = (uint8_t)(p - 40); }
        else if (p == 49)               { bg = ATTR_BG(ATTR_DEFAULT); }
        else if (p >= 90 && p <= 97)    { fg = (uint8_t)(p - 90 + 8); }
        else if (p >= 100 && p <= 107)  { bg = (uint8_t)(p - 100 + 8); }
        con.attr = ATTR(fg, bg);
    }
}

static void csi_final(char f)
{
    int p0 = con.nparams > 0 ? con.params[0] : 0;
    int p1 = con.nparams > 1 ? con.params[1] : 0;

    switch (f) {
    case 'm':
        sgr();
        break;
    case 'J':
        if (p0 == 2)
            console_clear();
        break;
    case 'H': case 'f': {
        uint32_t row = p0 > 0 ? (uint32_t)p0 : 1;     /* 1-based, below the status bar */
        uint32_t col = p1 > 0 ? (uint32_t)p1 : 1;
        row = TOP_ROW + row - 1;
        con.cy = row < con.rows ? row : con.rows - 1;
        con.cx = col - 1 < con.cols ? col - 1 : con.cols - 1;
        break;
    }
    case 'K':
        clear_row(con.cy, con.cx);
        break;
    }
}

static void put_raw(char ch)
{
    uint8_t c = (uint8_t)ch;

    if (con.esc == 1) {
        if (c == '[') {
            con.esc = 2;
            con.nparams = 0;
            con.params[0] = 0;
        } else {
            con.esc = 0;
        }
        return;
    }
    if (con.esc == 2) {
        if (c >= '0' && c <= '9') {
            if (con.nparams == 0)
                con.nparams = 1;
            con.params[con.nparams - 1] = con.params[con.nparams - 1] * 10 + (c - '0');
        } else if (c == ';') {
            if (con.nparams == 0)
                con.nparams = 1;
            if (con.nparams < 8)
                con.params[con.nparams++] = 0;
        } else {
            con.esc = 0;
            csi_final((char)c);
        }
        return;
    }

    switch (c) {
    case 0x1B: con.esc = 1; break;
    case '\n': newline(); break;
    case '\r': con.cx = 0; break;
    case '\b': if (con.cx > 0) con.cx--; break;
    case '\t':
        do {
            put_raw(' ');
        } while (con.cx % TAB_WIDTH);
        break;
    default:
        if (c < 0x20)
            break;
        if (con.cx >= con.cols)
            newline();
        con.cells[con.cy][con.cx].ch = c;
        con.cells[con.cy][con.cx].attr = con.attr;
        draw_cell(con.cx, con.cy, 0);
        con.cx++;
    }
}

void console_init(framebuffer_t *fb, const font_t *font)
{
    con.fb = fb;
    con.font = font;
    con.cols = fb->width / 8;
    con.rows = fb->height / font->height;
    if (con.cols > MAX_COLS) con.cols = MAX_COLS;
    if (con.rows > MAX_ROWS) con.rows = MAX_ROWS;
    for (int i = 0; i < 16; i++)
        con.pal[i] = fb_color(fb, ansi_rgb[i][0], ansi_rgb[i][1], ansi_rgb[i][2]);
    con.attr = ATTR_DEFAULT;
    con.suspended = 0;
    con.esc = 0;
    con.active = 1;

    for (uint32_t c = 0; c < con.cols; c++) {
        con.cells[STATUS_ROW][c].ch = ' ';
        con.cells[STATUS_ROW][c].attr = ATTR_STATUS;
    }
    console_clear();
}

int console_active(void)
{
    return con.active;
}

void console_putc(char c)
{
    if (!con.active)
        return;
    cursor_hide();
    put_raw(c);
    cursor_show();
}

void console_write(const char *s)
{
    if (!con.active)
        return;
    cursor_hide();
    while (*s)
        put_raw(*s++);
    cursor_show();
}

void console_clear(void)
{
    for (uint32_t r = TOP_ROW; r < con.rows; r++)
        for (uint32_t c = 0; c < con.cols; c++) {
            con.cells[r][c].ch = ' ';
            con.cells[r][c].attr = con.attr;
        }
    con.cx = 0;
    con.cy = TOP_ROW;
    redraw_all();
}

void console_set_color(uint8_t fg, uint8_t bg)
{
    con.attr = ATTR(fg, bg);
}

void console_size(uint32_t *cols, uint32_t *rows)
{
    *cols = con.cols;
    *rows = con.rows - TOP_ROW;
}

framebuffer_t *console_framebuffer(void)
{
    return con.fb;
}

static void status_put(uint32_t col, const char *s)
{
    for (; *s && col < con.cols; s++, col++) {
        con.cells[STATUS_ROW][col].ch = (uint8_t)*s;
        draw_cell(col, STATUS_ROW, 0);
    }
}

void console_set_status(const char *left, const char *right)
{
    if (!con.active)
        return;
    if (left) {
        uint32_t c;
        for (c = 0; c < con.cols / 2; c++) {
            con.cells[STATUS_ROW][c].ch = ' ';
            draw_cell(c, STATUS_ROW, 0);
        }
        status_put(1, left);
    }
    if (right) {
        uint32_t n = 0;
        while (right[n])
            n++;
        for (uint32_t c = con.cols / 2; c < con.cols; c++) {
            con.cells[STATUS_ROW][c].ch = ' ';
            draw_cell(c, STATUS_ROW, 0);
        }
        if (n + 1 < con.cols)
            status_put(con.cols - 1 - n, right);
    }
}

void console_suspend(int suspend)
{
    if (!con.active || con.suspended == suspend)
        return;
    con.suspended = suspend;
    if (!suspend) {
        redraw_all();
        cursor_show();
    }
}

int console_suspended(void)
{
    return con.active && con.suspended;
}

void console_panic(void)
{
    if (!con.active)
        return;
    con.suspended = 0;
    con.esc = 0;
    con.attr = ATTR(COLOR_WHITE, COLOR_RED);
    for (uint32_t c = 0; c < con.cols; c++)
        con.cells[STATUS_ROW][c].attr = ATTR(COLOR_RED, COLOR_WHITE);
    console_clear();
}
