/* 3.5" SPI TFT (ILI9486, 480x320) on SPI0 of the BCM2835 (Raspberry Pi 1).
 * Display only: the XPT2046 touch chip on the board is not read.
 * See spilcd.h for the wiring.
 *
 * The board turns the SPI stream into a 16-bit bus with shift registers, so
 * a command or a parameter is sent as one 16-bit word (high byte 0) and a
 * pixel as its RGB565 word, high byte first. DC low = command, high = data. */
#include "spilcd.h"
#include "board.h"
#include "gpio.h"
#include "mmio.h"
#include "timer.h"
#include "audio/audio.h"
#include "bm/gfx16.h"
#include "gfx/font.h"
#include "kernel/input.h"
#include "lib/printf.h"
#include "usb/hid.h"

#include <stdlib.h>
#include <string.h>

#define PIN_TOUCH_CS 7
#define PIN_LCD_CS   8      /* CE0 */
#define PIN_MISO     9
#define PIN_MOSI     10
#define PIN_SCLK     11
#define PIN_DC       24
#define PIN_RESET    25

#define SPI0_BASE   (PERIPHERAL_BASE + 0x204000)
#define SPI0_CS     (SPI0_BASE + 0x00)
#define SPI0_FIFO   (SPI0_BASE + 0x04)
#define SPI0_CLK    (SPI0_BASE + 0x08)

#define CS_CLEAR    (3u << 4)   /* clear both FIFOs */
#define CS_TA       (1u << 7)
#define CS_DONE     (1u << 16)
#define CS_RXD      (1u << 17)
#define CS_TXD      (1u << 18)

/* The core clock is 250 MHz: /16 = 15.6 MHz, inside the ILI9486 write limit. */
#define SPI_DIV     16

/* MADCTL: MV (rows and columns swapped, landscape) + BGR. If the picture is
 * mirrored on a given panel, change the MX/MY bits (0x40, 0x80) here. */
#ifndef SPILCD_MADCTL
#define SPILCD_MADCTL 0x28
#endif

static int lcd_ready;

static int is_pi1(void)
{
    uint8_t m = board()->model;
    return m >= BOARD_PI1_A && m <= BOARD_CM1;
}

int spilcd_ready(void)
{
    return lcd_ready;
}

/* Sends `n` bytes in one chip-select cycle. Reads the receive FIFO as it
 * goes: a full one stops the clock. */
static void spi_send(const uint8_t *p, unsigned n)
{
    unsigned rx = n, spin = 0;

    mmio_write(SPI0_CS, CS_CLEAR);
    mmio_write(SPI0_CS, CS_TA);
    while (n || rx) {
        uint32_t cs = mmio_read(SPI0_CS);
        if (n && (cs & CS_TXD)) {
            mmio_write(SPI0_FIFO, *p++);
            n--;
            spin = 0;
        }
        if (rx && (cs & CS_RXD)) {
            (void)mmio_read(SPI0_FIFO);
            rx--;
            spin = 0;
        }
        if (++spin > 100000u)
            break;
    }
    spin = 0;
    while (!(mmio_read(SPI0_CS) & CS_DONE) && ++spin < 100000u)
        ;
    mmio_write(SPI0_CS, 0);
}

static void lcd_cmd(uint8_t c)
{
    uint8_t w[2] = { 0, c };
    gpio_write(PIN_DC, 0);
    spi_send(w, 2);
}

static void lcd_data(const uint8_t *d, unsigned n)
{
    uint8_t w[2];
    gpio_write(PIN_DC, 1);
    while (n--) {
        w[0] = 0;
        w[1] = *d++;
        spi_send(w, 2);
    }
}

static void lcd_cmd_args(uint8_t c, const uint8_t *d, unsigned n)
{
    lcd_cmd(c);
    if (n)
        lcd_data(d, n);
}

static void lcd_window(int x0, int y0, int x1, int y1)
{
    uint8_t col[4] = { x0 >> 8, x0 & 255, x1 >> 8, x1 & 255 };
    uint8_t row[4] = { y0 >> 8, y0 & 255, y1 >> 8, y1 & 255 };
    lcd_cmd_args(0x2A, col, 4);
    lcd_cmd_args(0x2B, row, 4);
    lcd_cmd(0x2C);
    gpio_write(PIN_DC, 1);
}

/* Streams RGB565 words, high byte first, in one chip-select cycle. */
static void lcd_stream_begin(void)
{
    mmio_write(SPI0_CS, CS_CLEAR);
    mmio_write(SPI0_CS, CS_TA);
}

static void lcd_stream_pixel(uint16_t px, unsigned *rx)
{
    uint8_t b[2] = { px >> 8, px & 255 };
    for (int i = 0; i < 2; i++) {
        for (unsigned spin = 0; spin < 100000u; spin++) {
            uint32_t cs = mmio_read(SPI0_CS);
            if (cs & CS_RXD) {
                (void)mmio_read(SPI0_FIFO);
                if (*rx)
                    (*rx)--;
            }
            if (cs & CS_TXD)
                break;
        }
        mmio_write(SPI0_FIFO, b[i]);
        (*rx)++;
    }
}

static void lcd_stream_end(unsigned *rx)
{
    for (unsigned spin = 0; *rx && spin < 200000u; spin++) {
        if (mmio_read(SPI0_CS) & CS_RXD) {
            (void)mmio_read(SPI0_FIFO);
            (*rx)--;
            spin = 0;
        }
    }
    for (unsigned spin = 0; !(mmio_read(SPI0_CS) & CS_DONE) && spin < 100000u; spin++)
        ;
    mmio_write(SPI0_CS, 0);
}

int spilcd_init(void)
{
    static const uint8_t pix16 = 0x55;      /* 16 bits per pixel */
    static const uint8_t pwr1[2] = { 0x09, 0x09 };
    static const uint8_t pwr2 = 0x44;
    static const uint8_t vcom[4] = { 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t pgam[15] = {
        0x0F, 0x1F, 0x1C, 0x0C, 0x0F, 0x08, 0x48, 0x98,
        0x37, 0x0A, 0x13, 0x04, 0x11, 0x0D, 0x00
    };
    static const uint8_t ngam[15] = {
        0x0F, 0x32, 0x2E, 0x0B, 0x0D, 0x05, 0x47, 0x75,
        0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00
    };
    static const uint8_t dgam[15] = {
        0x0F, 0x32, 0x2E, 0x0B, 0x0D, 0x05, 0x47, 0x75,
        0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00
    };
    static const uint8_t mad = SPILCD_MADCTL;
    static const uint8_t zero = 0;

    if (!is_pi1())
        return -1;
    if (lcd_ready)
        return 0;

    gpio_set_function(PIN_SCLK, GPIO_ALT0);
    gpio_set_function(PIN_MOSI, GPIO_ALT0);
    gpio_set_function(PIN_MISO, GPIO_ALT0);
    gpio_set_function(PIN_LCD_CS, GPIO_ALT0);
    /* the touch chip's select stays high: it never listens */
    gpio_set_function(PIN_TOUCH_CS, GPIO_OUTPUT);
    gpio_write(PIN_TOUCH_CS, 1);
    gpio_set_function(PIN_DC, GPIO_OUTPUT);
    gpio_set_function(PIN_RESET, GPIO_OUTPUT);

    mmio_write(SPI0_CLK, SPI_DIV);
    mmio_write(SPI0_CS, 0);                 /* mode 0, CS0, active low */

    gpio_write(PIN_RESET, 1);
    timer_delay_ms(20);
    gpio_write(PIN_RESET, 0);
    timer_delay_ms(20);
    gpio_write(PIN_RESET, 1);
    timer_delay_ms(150);

    lcd_cmd_args(0xB0, &zero, 1);           /* interface mode: normal */
    lcd_cmd(0x11);                          /* sleep out */
    timer_delay_ms(250);
    lcd_cmd_args(0x3A, &pix16, 1);
    lcd_cmd_args(0xC0, pwr1, 2);
    lcd_cmd_args(0xC2, &pwr2, 1);
    lcd_cmd_args(0xC5, vcom, 4);
    lcd_cmd_args(0xE0, pgam, 15);
    lcd_cmd_args(0xE1, ngam, 15);
    lcd_cmd_args(0xE2, dgam, 15);
    lcd_cmd_args(0x36, &mad, 1);
    lcd_cmd(0x11);
    timer_delay_ms(50);
    lcd_cmd(0x29);                          /* display on */
    timer_delay_ms(50);

    lcd_ready = 1;
    spilcd_fill(0);
    return 0;
}

void spilcd_fill(uint16_t rgb565)
{
    unsigned rx = 0;

    if (!lcd_ready)
        return;
    lcd_window(0, 0, SPILCD_W - 1, SPILCD_H - 1);
    lcd_stream_begin();
    for (unsigned i = 0; i < (unsigned)SPILCD_W * SPILCD_H; i++)
        lcd_stream_pixel(rgb565, &rx);
    lcd_stream_end(&rx);
}

void spilcd_blit(int x, int y, int w, int h, const uint16_t *src, int stride)
{
    unsigned rx = 0;
    int x1, y1, cw;

    if (!lcd_ready || !src)
        return;
    /* clip: skip the source pixels left of or above the screen */
    if (x < 0) { src -= x; w += x; x = 0; }
    if (y < 0) { src -= y * stride; h += y; y = 0; }
    if (x + w > SPILCD_W) w = SPILCD_W - x;
    if (y + h > SPILCD_H) h = SPILCD_H - y;
    if (w <= 0 || h <= 0)
        return;
    x1 = x + w - 1;
    y1 = y + h - 1;
    cw = w;
    lcd_window(x, y, x1, y1);
    lcd_stream_begin();
    for (int r = 0; r < h; r++, src += stride)
        for (int c = 0; c < cw; c++)
            lcd_stream_pixel(src[c], &rx);
    lcd_stream_end(&rx);
}

/* ---------------------------------------------------------------- tiny desk robot */

enum {
    ST_CODING = 0,
    ST_THINKING,
    ST_BUILDING,
    ST_DEBUGGING,
    ST_COFFEE,
    ST_BELL,
    ST_COUNT
};

static const struct {
    const char *badge;
    const char *sub;
    uint32_t col;
    unsigned frames;
} states[ST_COUNT] = {
    { "TYPING CODE",   "writing spilcd.c on Pi 1...",    0x2EE6D6, 150 },
    { "THINKING...",   "planning next bare-metal pass",  0xFFCA3A,  85 },
    { "BUILDING...",   "arm-none-eabi-gcc -O2 kernel",   0x56B6C2, 100 },
    { "DEBUGGING!",    "fixing SPI0 FIFO timing...",     0xFF5964,  85 },
    { "TASK DONE!",    "ringing the desk bell: *DING!*", 0x7AE582,  75 },
    { "COFFEE BREAK",  "recharging servos & sipping",    0xE5C07B,  75 },
};

static const struct {
    const char *text;
    uint32_t col;
} code_lines[] = {
    { "/* Pi 1 GPIO SPI0 TFT */",     0x6A7A8C },
    { "int spilcd_init(void) {",      0xC678DD },
    { "  mmio_write(SPI0_CLK, 16);",  0x61AFEF },
    { "  gpio_write(PIN_RESET, 1);",  0x98C379 },
    { "  lcd_cmd(0x11); /* wake */",  0xE5C07B },
    { "  lcd_cmd_args(0x3A, &p16);",  0x56B6C2 },
    { "  lcd_cmd(0x29); /* on! */",   0x98C379 },
    { "  return 0;",                  0xC678DD },
    { "}",                            0xC678DD },
    { "void robot_work(desk_t *d) {", 0xC678DD },
    { "  while (tasks_pending()) {",  0xE5C07B },
    { "    servo_type_keys(d->kb);",  0x61AFEF },
    { "    if (job_done())",          0xC678DD },
    { "      ring_desk_bell();",      0x7AE582 },
    { "  }",                          0xE5C07B },
    { "}",                            0xC678DD },
};
#define NCODE ((int)(sizeof code_lines / sizeof code_lines[0]))

static void bevel_box(g16_t *g, int x, int y, int w, int h,
                      uint32_t fill, uint32_t hi, uint32_t lo)
{
    g16_rectfill(g, x, y, w, h, g16_rgb24(fill));
    g16_rectfill(g, x, y, w, 1, g16_rgb24(hi));
    g16_rectfill(g, x, y, 1, h, g16_rgb24(hi));
    g16_rectfill(g, x, y + h - 1, w, 1, g16_rgb24(lo));
    g16_rectfill(g, x + w - 1, y, 1, h, g16_rgb24(lo));
}

static void round_box(g16_t *g, int x, int y, int w, int h, int r, uint32_t rgb)
{
    uint16_t c = g16_rgb24(rgb);
    g16_rectfill(g, x + r, y, w - 2 * r, h, c);
    g16_rectfill(g, x, y + r, w, h - 2 * r, c);
    g16_circfill(g, x + r, y + r, r, c);
    g16_circfill(g, x + w - 1 - r, y + r, r, c);
    g16_circfill(g, x + r, y + h - 1 - r, r, c);
    g16_circfill(g, x + w - 1 - r, y + h - 1 - r, r, c);
}

static void draw_backdrop(g16_t *g, unsigned frame, int st, unsigned tasks, unsigned loc, uint32_t t0)
{
    /* Dark studio wall with subtle blueprint grid */
    g16_cls(g, g16_rgb24(0x111622));
    uint16_t grid = g16_rgb24(0x171F30);
    for (int x = 24; x < SPILCD_W; x += 24)
        g16_rectfill(g, x, 26, 1, 210, grid);
    for (int y = 48; y < 236; y += 24)
        g16_rectfill(g, 0, y, SPILCD_W, 1, grid);

    /* Warm lamp glow on the left wall */
    g16_circfill(g, 58, 128, 62, g16_rgb24(0x1A202C));
    g16_circfill(g, 58, 128, 42, g16_rgb24(0x232832));

    /* Wall shelf (top-left) with pixel books and succulent plant */
    g16_rectfill(g, 18, 72, 92, 5, g16_rgb24(0x5C4033));
    g16_rectfill(g, 18, 72, 92, 1, g16_rgb24(0x8B654E));
    g16_rectfill(g, 24, 52, 8, 20, g16_rgb24(0xE06C75));
    g16_rectfill(g, 33, 55, 7, 17, g16_rgb24(0x61AFEF));
    g16_rectfill(g, 41, 49, 9, 23, g16_rgb24(0xE5C07B));
    g16_rectfill(g, 51, 56, 7, 16, g16_rgb24(0xC678DD));
    /* Tiny potted plant on shelf */
    g16_rectfill(g, 82, 61, 14, 11, g16_rgb24(0xC86D51));
    g16_rectfill(g, 80, 59, 18, 3, g16_rgb24(0xDF8266));
    g16_circfill(g, 89, 54, 6, g16_rgb24(0x4EA854));
    g16_circfill(g, 84, 56, 4, g16_rgb24(0x68C46E));
    g16_circfill(g, 94, 56, 4, g16_rgb24(0x3B8840));

    /* Articulated studio desk lamp on the left */
    g16_rectfill(g, 16, 226, 28, 6, g16_rgb24(0x3A4454));
    g16_line(g, 26, 226, 14, 162, g16_rgb24(0x58667A));
    g16_line(g, 27, 226, 15, 162, g16_rgb24(0x7A8A9E));
    g16_line(g, 14, 162, 42, 118, g16_rgb24(0x58667A));
    g16_line(g, 15, 162, 43, 118, g16_rgb24(0x7A8A9E));
    g16_circfill(g, 14, 162, 3, g16_rgb24(0xE5C07B));
    g16_circfill(g, 48, 118, 9, g16_rgb24(0x465264));
    g16_rectfill(g, 42, 118, 18, 8, g16_rgb24(0x465264));
    g16_circfill(g, 51, 125, 5, g16_rgb24((frame & 16) ? 0xFFE885 : 0xFFD866));

    /* Top studio status bar */
    g16_rectfill(g, 0, 0, SPILCD_W, 25, g16_rgb24(0x1B2334));
    g16_rectfill(g, 0, 25, SPILCD_W, 1, g16_rgb24(0x2E3D58));
    g16_circfill(g, 12, 12, 4, g16_rgb24(states[st].col));

    g->font = &font_console_8x14;
    g16_text(g, 22, 6, states[st].badge, g16_rgb24(states[st].col));

    g->font = &font_console_6x12;
    g16_text(g, 144, 7, states[st].sub, g16_rgb24(0xABB2BF));

    char stat[40];
    uint32_t sec = (timer_ticks() - t0) / 1000000u;
    ksnprintf(stat, sizeof stat, "TASKS:%02u LOC:%u %02lu:%02lu",
              tasks, loc, (unsigned long)(sec / 60), (unsigned long)(sec % 60));
    g16_text(g, SPILCD_W - (int)strlen(stat) * 6 - 8, 7, stat, g16_rgb24(0x7FE7DC));
}

static void draw_monitor(g16_t *g, unsigned frame, int st, unsigned loc)
{
    /* Monitor stand & shadow on the right side of the desk */
    const int mx = 276, my = 66, mw = 190, mh = 136;
    g16_rectfill(g, mx + 72, my + mh, 46, 22, g16_rgb24(0x2C3442));
    g16_rectfill(g, mx + 78, my + mh, 12, 22, g16_rgb24(0x3E495C));
    bevel_box(g, mx + 52, my + mh + 20, 86, 8, 0x3A4456, 0x58667E, 0x202630);

    /* Outer monitor casing */
    round_box(g, mx - 2, my + 2, mw + 4, mh, 7, 0x0B0F17);
    round_box(g, mx, my, mw, mh, 6, 0x2E3748);
    round_box(g, mx + 2, my + 2, mw - 4, mh - 4, 5, 0x1D2430);

    /* Inner screen */
    const int sx = mx + 7, sy = my + 7, sw = mw - 14, sh = mh - 18;
    g16_rectfill(g, sx, sy, sw, sh, g16_rgb24(0x0D111A));

    /* IDE top title bar inside monitor */
    g16_rectfill(g, sx, sy, sw, 13, g16_rgb24(0x182132));
    g16_circfill(g, sx + 6,  sy + 6, 2, g16_rgb24(0xFF5F56));
    g16_circfill(g, sx + 13, sy + 6, 2, g16_rgb24(0xFFBD2E));
    g16_circfill(g, sx + 20, sy + 6, 2, g16_rgb24(0x27C93F));
    g->font = &font_console_6x12;
    g16_text(g, sx + 28, sy + 1, "spilcd.c - Pi 1 ARMv6", g16_rgb24(0x8FA2BE));

    /* Scrolling code editor area */
    g16_clip(g, sx + 2, sy + 14, sw - 4, sh - 36);
    int top_line = (int)((frame / 14) % NCODE);
    for (int i = 0; i < 6; i++) {
        int idx = (top_line + i) % NCODE;
        int ly = sy + 15 + i * 12;
        char lnum[4];
        ksnprintf(lnum, sizeof lnum, "%2d", idx + 1);
        g16_text(g, sx + 3, ly, lnum, g16_rgb24(0x3E4B61));

        const char *line = code_lines[idx].text;
        uint16_t col = g16_rgb24(code_lines[idx].col);
        if (st == ST_DEBUGGING && i == 3)
            col = g16_rgb24((frame & 4) ? 0xFF5964 : 0xFFD166);

        if (i == 5 && (st == ST_CODING || st == ST_DEBUGGING)) {
            int len = (int)strlen(line);
            int chars = (int)((frame / 2) % (unsigned)(len + 1));
            char partial[36];
            if (chars > (int)sizeof partial - 1)
                chars = (int)sizeof partial - 1;
            memcpy(partial, line, (size_t)chars);
            partial[chars] = '\0';
            int cx = g16_text(g, sx + 19, ly, partial, col);
            if ((frame & 4) == 0)
                g16_rectfill(g, cx + 1, ly + 1, 5, 10, g16_rgb24(0x2EE6D6));
        } else {
            g16_text(g, sx + 19, ly, line, col);
        }
    }
    g16_clip(g, 0, 0, SPILCD_W, SPILCD_H);

    /* Bottom terminal / status pane inside monitor */
    int ty = sy + sh - 21;
    g16_rectfill(g, sx, ty, sw, 21, g16_rgb24(0x141B29));
    g16_rectfill(g, sx, ty, sw, 1, g16_rgb24(0x28354E));
    if (st == ST_BUILDING) {
        int pct = (int)((frame * 2) % 101);
        int bw = (sw - 12) * pct / 100;
        g16_text(g, sx + 4, ty + 2, "BUILD kernel.img", g16_rgb24(0x56B6C2));
        g16_rect(g, sx + 4, ty + 14, sw - 8, 5, g16_rgb24(0x3B4C6B));
        g16_rectfill(g, sx + 5, ty + 15, bw, 3, g16_rgb24(0x7AE582));
    } else if (st == ST_BELL) {
        g16_text(g, sx + 4, ty + 4, "[PASS] All checks green! *DING*", g16_rgb24(0x7AE582));
    } else if (st == ST_DEBUGGING) {
        g16_text(g, sx + 4, ty + 4, "[WARN] SPI0 FIFO -> patching...", g16_rgb24(0xFF5964));
    } else {
        char tline[36];
        ksnprintf(tline, sizeof tline, "$ pi1-gcc -O2 (%u lines)", loc);
        g16_text(g, sx + 4, ty + 4, tline, g16_rgb24(0x98C379));
    }

    /* Power LED on bottom bezel */
    g16_circfill(g, mx + mw - 12, my + mh - 6, 2,
                 g16_rgb24((frame & 8) ? 0x27C93F : 0x1E8E2E));
}

static void draw_robot(g16_t *g, unsigned frame, int st)
{
    /* Subtle breathing / typing bounce */
    int bob = 0;
    if (st == ST_CODING || st == ST_DEBUGGING)
        bob = (frame & 2) ? -1 : 1;
    else if (st == ST_BUILDING)
        bob = ((frame >> 2) & 1) ? -2 : 0;
    else if (st == ST_BELL)
        bob = ((frame >> 1) & 1) ? -3 : 0;
    else
        bob = ((frame >> 4) & 1) ? -1 : 0;

    int rx = 104, ry = 86 + bob;

    /* Antenna on top of the robot's head */
    int ax = rx + 46 + ((st == ST_THINKING) ? ((int)(frame >> 2 & 3) - 1) : 0);
    g16_rectfill(g, rx + 42, ry - 5, 10, 6, g16_rgb24(0x465366));
    g16_line(g, rx + 47, ry - 5, ax, ry - 19, g16_rgb24(0x8FA2BE));
    g16_line(g, rx + 48, ry - 5, ax + 1, ry - 19, g16_rgb24(0x8FA2BE));
    g16_circfill(g, ax, ry - 21, 5, g16_rgb24(states[st].col));
    g16_circfill(g, ax - 1, ry - 22, 2, g16_rgb24(0xFFFFFF));

    /* Side ear bolts / servos */
    round_box(g, rx - 7, ry + 18, 8, 22, 3, 0x4E5D73);
    g16_rectfill(g, rx - 5, ry + 22, 4, 14, g16_rgb24(0x2EE6D6));
    round_box(g, rx + 93, ry + 18, 8, 22, 3, 0x4E5D73);
    g16_rectfill(g, rx + 95, ry + 22, 4, 14, g16_rgb24(0x2EE6D6));

    /* Robot Head (3D-printed off-white / silver housing with dark OLED face) */
    round_box(g, rx - 1, ry + 2, 96, 62, 10, 0x202838);
    round_box(g, rx, ry, 94, 60, 10, 0xDCE3EC);
    round_box(g, rx + 2, ry + 2, 90, 56, 9, 0xC6D0DE);
    /* Top brow highlight */
    g16_rectfill(g, rx + 12, ry + 3, 70, 2, g16_rgb24(0xF4F7FB));

    /* Glossy dark OLED Face Screen */
    int fx = rx + 9, fy = ry + 9, fw = 76, fh = 42;
    round_box(g, fx - 1, fy - 1, fw + 2, fh + 2, 6, 0x2E3846);
    round_box(g, fx, fy, fw, fh, 5, 0x0A0E17);

    /* Expressive OLED Eyes & Mouth */
    uint16_t eye_col = g16_rgb24(states[st].col);
    int blink = (frame % 72) >= 68;
    int look_x = 0, look_y = 0;
    if (st == ST_CODING || st == ST_BUILDING) {
        look_x = 4 + (int)((frame >> 3) % 5) - 2;
        look_y = 1;
    } else if (st == ST_THINKING) {
        look_x = -2;
        look_y = -4;
    } else if (st == ST_DEBUGGING) {
        look_x = 5;
        look_y = (frame & 2) ? 0 : 2;
    }

    int elx = fx + 18 + look_x, erx = fx + 46 + look_x, ey = fy + 12 + look_y;

    if (st == ST_BELL) {
        /* Happy ^ ^ eyes */
        for (int i = 0; i < 2; i++) {
            int cx = (i == 0 ? elx : erx) + 6;
            int cy = ey + 8;
            g16_circfill(g, cx, cy, 8, eye_col);
            g16_circfill(g, cx, cy + 4, 7, g16_rgb24(0x0A0E17));
        }
        /* Big happy smile */
        g16_rectfill(g, fx + 31, fy + 31, 14, 3, eye_col);
        g16_rectfill(g, fx + 29, fy + 29, 3, 3, eye_col);
        g16_rectfill(g, fx + 44, fy + 29, 3, 3, eye_col);
    } else if (st == ST_COFFEE) {
        /* Relaxed content arc eyes */
        g16_rectfill(g, elx, ey + 6, 13, 3, eye_col);
        g16_rectfill(g, erx, ey + 6, 13, 3, eye_col);
        g16_circfill(g, fx + 38, fy + 31, 3, eye_col);
    } else if (st == ST_DEBUGGING) {
        /* Determined > < / furrowed brow eyes */
        round_box(g, elx, ey + 1, 13, 12, 3, states[st].col);
        round_box(g, erx, ey + 1, 13, 12, 3, states[st].col);
        /* Slanted eyebrows */
        g16_line(g, elx - 1, ey - 1, elx + 13, ey + 4, g16_rgb24(0x0A0E17));
        g16_line(g, elx - 1, ey,     elx + 13, ey + 5, g16_rgb24(0x0A0E17));
        g16_line(g, erx + 13, ey - 1, erx - 1, ey + 4, g16_rgb24(0x0A0E17));
        g16_line(g, erx + 13, ey,     erx - 1, ey + 5, g16_rgb24(0x0A0E17));
        /* Wavy mouth */
        g16_rectfill(g, fx + 30, fy + 32, 6, 2, eye_col);
        g16_rectfill(g, fx + 36, fy + 30, 6, 2, eye_col);
        g16_rectfill(g, fx + 42, fy + 32, 5, 2, eye_col);
    } else if (blink) {
        g16_rectfill(g, elx, ey + 6, 13, 2, eye_col);
        g16_rectfill(g, erx, ey + 6, 13, 2, eye_col);
        g16_rectfill(g, fx + 33, fy + 31, 10, 2, eye_col);
    } else {
        /* Glowing pixel eyes with specular glint */
        round_box(g, elx, ey, 13, 14, 3, states[st].col);
        round_box(g, erx, ey, 13, 14, 3, states[st].col);
        if (st == ST_THINKING)
            g16_rectfill(g, erx, ey, 13, 3, g16_rgb24(0x0A0E17)); /* quizzical brow */
        g16_rectfill(g, elx + 3, ey + 3, 3, 3, g16_rgb24(0xFFFFFF));
        g16_rectfill(g, erx + 3, ey + 3, 3, 3, g16_rgb24(0xFFFFFF));
        /* Mouth */
        if (st == ST_THINKING)
            g16_rectfill(g, fx + 35, fy + 31, 7, 3, eye_col);
        else
            g16_rectfill(g, fx + 32, fy + 31, 12, 2, eye_col);
    }

    /* Thought bubble / alert icon above head */
    if (st == ST_THINKING) {
        g16_circfill(g, rx + 8, ry - 4, 3, g16_rgb24(0xDCE3EC));
        g16_circfill(g, rx - 2, ry - 12, 5, g16_rgb24(0xDCE3EC));
        round_box(g, rx - 34, ry - 38, 36, 22, 6, 0xDCE3EC);
        g->font = &font_console_8x14;
        g16_text(g, rx - 27, ry - 34, (frame & 8) ? "01?" : "{*}", g16_rgb24(0x1D2430));
    } else if (st == ST_DEBUGGING) {
        round_box(g, rx - 24, ry - 22, 20, 20, 4, 0xFF5964);
        g->font = &font_console_8x14;
        g16_text(g, rx - 18, ry - 19, "!", g16_rgb24(0xFFFFFF));
    }

    /* Neck joint */
    g16_rectfill(g, rx + 35, ry + 60, 24, 8, g16_rgb24(0x465366));
    g16_rectfill(g, rx + 38, ry + 62, 18, 2, g16_rgb24(0x2C3542));

    /* Robot Torso / Workstation Body */
    int tx = rx + 10, ty = ry + 66, tw = 74, th = 64;
    round_box(g, tx - 1, ty + 2, tw + 2, th, 8, 0x1E2636);
    round_box(g, tx, ty, tw, th, 8, 0xBCC8D8);
    round_box(g, tx + 3, ty + 3, tw - 6, th - 6, 6, 0xAAB7C8);

    /* Chest telemetry panel */
    bevel_box(g, tx + 11, ty + 10, 52, 28, 0x1A2230, 0x0E131C, 0x3A475C);
    /* Chest LEDs and mini waveform */
    g16_circfill(g, tx + 18, ty + 17, 3, g16_rgb24(0x27C93F));
    g16_circfill(g, tx + 27, ty + 17, 3, g16_rgb24((frame & 4) ? 0xFFBD2E : 0x4A3B18));
    g16_circfill(g, tx + 36, ty + 17, 3, g16_rgb24((st == ST_DEBUGGING) ? 0xFF5964 : 0x2EE6D6));
    for (int i = 0; i < 6; i++) {
        int bh = 3 + ((int)((frame + i * 5) * 3) % 9);
        g16_rectfill(g, tx + 16 + i * 7, ty + 34 - bh, 5, bh, g16_rgb24(states[st].col));
    }
    g->font = &font_console_6x12;
    g16_text(g, tx + 22, ty + 42, "PI-BOT", g16_rgb24(0x364254));

    /* Articulated Servo Arms & Hands */
    int l_shoulder_x = tx + 2,  l_shoulder_y = ty + 18;
    int r_shoulder_x = tx + tw - 2, r_shoulder_y = ty + 18;

    int l_hand_x = 136, l_hand_y = 228;
    int r_hand_x = 186, r_hand_y = 228;

    if (st == ST_CODING || st == ST_BUILDING) {
        l_hand_y = 224 + ((frame & 2) ? 6 : -3);
        r_hand_y = 224 + ((frame & 2) ? -3 : 6);
        l_hand_x = 132 + (int)((frame >> 1) % 14);
        r_hand_x = 176 + (int)(((frame >> 1) + 5) % 16);
    } else if (st == ST_DEBUGGING) {
        l_hand_y = 223 + ((frame & 1) ? 7 : -4);
        r_hand_y = 223 + ((frame & 1) ? -4 : 7);
        l_hand_x = 130 + (int)(frame % 18);
        r_hand_x = 174 + (int)((frame + 7) % 18);
    } else if (st == ST_THINKING) {
        /* Left hand raised to chin, right hand resting on keyboard */
        l_hand_x = rx + 26;
        l_hand_y = ry + 54;
        r_hand_x = 182;
        r_hand_y = 229;
    } else if (st == ST_COFFEE) {
        /* Left hand holds the steaming mug */
        l_hand_x = 84;
        l_hand_y = 204;
        r_hand_x = 178;
        r_hand_y = 229;
    } else if (st == ST_BELL) {
        /* Right hand reaches over and taps the brass desk bell! */
        l_hand_x = 138;
        l_hand_y = 228;
        r_hand_x = 238;
        r_hand_y = (frame & 2) ? 215 : 223;
    }

    int l_elbow_x = (l_shoulder_x + l_hand_x) / 2 - 12;
    int l_elbow_y = (l_shoulder_y + l_hand_y) / 2 + 8;
    int r_elbow_x = (r_shoulder_x + r_hand_x) / 2 + 10;
    int r_elbow_y = (r_shoulder_y + r_hand_y) / 2 + 8;

    /* Draw left arm (upper, elbow servo, forearm, gripper hand) */
    g16_circfill(g, l_shoulder_x, l_shoulder_y, 6, g16_rgb24(0x465366));
    for (int d = -2; d <= 2; d++) {
        g16_line(g, l_shoulder_x + d, l_shoulder_y, l_elbow_x + d, l_elbow_y, g16_rgb24(0x7C8DA3));
        g16_line(g, l_elbow_x + d, l_elbow_y, l_hand_x + d, l_hand_y, g16_rgb24(0x9BAAC0));
    }
    g16_circfill(g, l_elbow_x, l_elbow_y, 5, g16_rgb24(0x3A4658));
    g16_circfill(g, l_elbow_x, l_elbow_y, 2, g16_rgb24(0xE5C07B));
    g16_circfill(g, l_hand_x, l_hand_y, 6, g16_rgb24(0x2EE6D6));
    g16_circfill(g, l_hand_x, l_hand_y, 4, g16_rgb24(0xDCE3EC));

    /* Draw right arm */
    g16_circfill(g, r_shoulder_x, r_shoulder_y, 6, g16_rgb24(0x465366));
    for (int d = -2; d <= 2; d++) {
        g16_line(g, r_shoulder_x + d, r_shoulder_y, r_elbow_x + d, r_elbow_y, g16_rgb24(0x7C8DA3));
        g16_line(g, r_elbow_x + d, r_elbow_y, r_hand_x + d, r_hand_y, g16_rgb24(0x9BAAC0));
    }
    g16_circfill(g, r_elbow_x, r_elbow_y, 5, g16_rgb24(0x3A4658));
    g16_circfill(g, r_elbow_x, r_elbow_y, 2, g16_rgb24(0xE5C07B));
    g16_circfill(g, r_hand_x, r_hand_y, 6, g16_rgb24(0x2EE6D6));
    g16_circfill(g, r_hand_x, r_hand_y, 4, g16_rgb24(0xDCE3EC));
}

static void draw_desk(g16_t *g, unsigned frame, int st)
{
    /* Wooden desktop surface & bevel */
    g16_rectfill(g, 0, 224, SPILCD_W, 10, g16_rgb24(0x6E4B32));
    g16_rectfill(g, 0, 234, SPILCD_W, 8,  g16_rgb24(0x8A6042));
    g16_rectfill(g, 0, 242, SPILCD_W, 4,  g16_rgb24(0x523622));
    g16_rectfill(g, 0, 246, SPILCD_W, 74, g16_rgb24(0x161C28));

    /* Mechanical keyboard in front of the robot */
    int kx = 114, ky = 224, kw = 96, kh = 16;
    bevel_box(g, kx, ky, kw, kh, 0x263040, 0x45546C, 0x141A24);
    int hit_a = (st == ST_CODING || st == ST_DEBUGGING || st == ST_BUILDING)
                ? (int)((frame * 3) % 10) : -1;
    int hit_b = (st == ST_CODING || st == ST_DEBUGGING)
                ? (int)((frame * 7 + 3) % 10) : -1;
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 10; col++) {
            int active = (row == 0 && col == hit_a) || (row == 1 && col == hit_b);
            uint32_t kc = active ? states[st].col : (col == 0 || col == 9 ? 0x4C5C78 : 0x7A8CA8);
            g16_rectfill(g, kx + 5 + col * 9, ky + 3 + row * 6 + (active ? 1 : 0),
                         7, 4, g16_rgb24(kc));
        }
    }

    /* Floating typing particles above keyboard */
    if (st == ST_CODING || st == ST_DEBUGGING) {
        static const char *const sym[4] = { "{ }", ";", "01", "()" };
        int py = 210 - (int)((frame * 2) % 24);
        int px = 128 + (int)((frame / 12 * 23) % 64);
        g->font = &font_console_6x12;
        g16_text(g, px, py, sym[(frame / 12) & 3], g16_rgb24(states[st].col));
    }

    /* Steaming coffee mug (lifts when in ST_COFFEE) */
    int mug_x = (st == ST_COFFEE) ? 74 : 58;
    int mug_y = (st == ST_COFFEE) ? 194 : 208;
    round_box(g, mug_x + 15, mug_y + 4, 8, 12, 3, 0xD8DEE9);
    round_box(g, mug_x + 17, mug_y + 6, 4, 8,  2, 0x161C28);
    round_box(g, mug_x, mug_y, 18, 20, 3, 0xECEFF4);
    g16_rectfill(g, mug_x + 2, mug_y + 2, 14, 3, g16_rgb24(0x6F4E37));
    g16_rectfill(g, mug_x + 3, mug_y + 9, 12, 5, g16_rgb24(0xE06C75));
    /* Rising steam wisps */
    for (int s = 0; s < 2; s++) {
        int sy = mug_y - 5 - (int)(((frame + s * 8) / 2) % 14);
        int sx = mug_x + 5 + s * 6 + (((frame >> 2) + s) & 1 ? 2 : -1);
        g16_circfill(g, sx, sy, 1, g16_rgb24(0x8FA2BE));
    }

    /* Brass Desk Bell (rings with waves on ST_BELL!) */
    int bx = 236, by = 226;
    g16_rectfill(g, bx - 12, by + 2, 24, 4, g16_rgb24(0x2B3340));
    g16_circfill(g, bx, by + 1, 10, g16_rgb24(0xE5C07B));
    g16_circfill(g, bx - 3, by - 2, 3, g16_rgb24(0xFFF3B0));
    g16_rectfill(g, bx - 11, by + 2, 22, 3, g16_rgb24(0xB89246));
    g16_rectfill(g, bx - 2, by - 11, 4, 3, g16_rgb24(0xFFE082));
    if (st == ST_BELL) {
        int r1 = 14 + (int)((frame * 2) % 16);
        g16_circ(g, bx, by - 4, r1, g16_rgb24(0x7AE582));
        g16_circ(g, bx, by - 4, r1 + 5, g16_rgb24(0xFFCA3A));
        g->font = &font_console_8x14;
        g16_text(g, bx - 20, by - 36 - (int)(frame & 3), "*DING!*", g16_rgb24(0xFFE082));
    }

    /* Bottom telemetry cards & controls bar */
    bevel_box(g, 12, 254, 456, 56, 0x1D2536, 0x2F3D58, 0x0F131D);
    g->font = &font_console_8x14;
    g16_text(g, 24, 262, "TINY ENGINEER // PI 1 GPIO WORKSTATION", g16_rgb24(0xECEFF4));
    g->font = &font_console_6x12;
    g16_text(g, 24, 280, "ILI9486 480x320 SPI0 (15.6 MHz) - Autonomous Desk Companion",
             g16_rgb24(0x8FA2BE));
    g16_text(g, 24, 294, "Space/Enter: ring bell   Left/Right: mode   B/Esc/q: back to menu",
             g16_rgb24(0x56B6C2));
}

/* Diffs `cur` against `prev` in 16-row horizontal strips and sends only the
 * changed span of each strip to the SPI TFT, keeping the animation smooth. */
static void blit_diff(const uint16_t *cur, uint16_t *prev, int full)
{
    if (!lcd_ready)
        return;
    if (full) {
        spilcd_blit(0, 0, SPILCD_W, SPILCD_H, cur, SPILCD_W);
        memcpy(prev, cur, (size_t)SPILCD_W * SPILCD_H * sizeof(uint16_t));
        return;
    }
    const int strip = 16;
    for (int y0 = 0; y0 < SPILCD_H; y0 += strip) {
        int h = y0 + strip <= SPILCD_H ? strip : SPILCD_H - y0;
        int x_min = SPILCD_W, x_max = -1;
        for (int r = 0; r < h; r++) {
            const uint16_t * row_c = cur  + (y0 + r) * SPILCD_W;
            const uint16_t * row_p = prev + (y0 + r) * SPILCD_W;
            for (int x = 0; x < SPILCD_W; x++) {
                if (row_c[x] != row_p[x]) {
                    if (x < x_min) x_min = x;
                    if (x > x_max) x_max = x;
                }
            }
        }
        if (x_max >= x_min) {
            int w = x_max - x_min + 1;
            spilcd_blit(x_min, y0, w, h, cur + y0 * SPILCD_W + x_min, SPILCD_W);
            for (int r = 0; r < h; r++)
                memcpy(prev + (y0 + r) * SPILCD_W + x_min,
                       cur  + (y0 + r) * SPILCD_W + x_min,
                       (size_t)w * sizeof(uint16_t));
        }
    }
}

/* Mirrors the 480x320 canvas centered on the HDMI framebuffer when present. */
static void blit_hdmi(framebuffer_t *fb, const uint16_t *canvas, int first)
{
    if (!fb || !fb->base || fb->width < (uint32_t)SPILCD_W || fb->height < (uint32_t)SPILCD_H)
        return;
    int ox = ((int)fb->width - SPILCD_W) / 2;
    int oy = ((int)fb->height - SPILCD_H) / 2;
    if (first)
        memset(fb->base, 0, (size_t)fb->pitch * fb->height);
    if (fb->depth == 16) {
        uint16_t *dst = (uint16_t *)fb->base + oy * (fb->pitch / 2) + ox;
        for (int y = 0; y < SPILCD_H; y++)
            memcpy(dst + y * (fb->pitch / 2), canvas + y * SPILCD_W,
                   (size_t)SPILCD_W * sizeof(uint16_t));
    } else if (fb->depth == 32) {
        for (int y = 0; y < SPILCD_H; y++) {
            uint32_t *dst = (uint32_t *)(fb->base + (oy + y) * fb->pitch) + ox;
            const uint16_t *src = canvas + y * SPILCD_W;
            for (int x = 0; x < SPILCD_W; x++) {
                uint32_t rgb = g16_to_rgb24(src[x]);
                dst[x] = fb_color(fb, (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
            }
        }
    }
}

static void bell_chime(void)
{
    audio_note(0, 1047, 70, 4, 90);
    audio_note(1, 1568, 65, 4, 140);
}

void spilcd_desk_robot(framebuffer_t *fb)
{
    size_t bytes = (size_t)SPILCD_W * SPILCD_H * sizeof(uint16_t);
    uint16_t *canvas = malloc(bytes);
    uint16_t *prev_canvas = malloc(bytes);
    if (!canvas || !prev_canvas) {
        free(canvas);
        free(prev_canvas);
        return;
    }

    (void)spilcd_init();

    g16_t g;
    g16_target(&g, canvas, SPILCD_W, SPILCD_W, SPILCD_H, &font_console_6x12);

    input_pad_keys(INPUT_PAD_NAV);
    input_flush();
    uint32_t prev_btn = hid_buttons();
    uint32_t t0 = timer_ticks();

    int st = ST_CODING;
    unsigned st_frames = 0, frame = 0, tasks = 1, loc = 1240;

    for (;;) {
        uint32_t frame_t0 = timer_ticks();

        draw_backdrop(&g, frame, st, tasks, loc, t0);
        draw_monitor(&g, frame, st, loc);
        draw_robot(&g, frame, st);
        draw_desk(&g, frame, st);

        blit_diff(canvas, prev_canvas, frame == 0);
        blit_hdmi(fb, canvas, frame == 0);

        /* Poll keys and controller buttons */
        int leave = 0;
        for (;;) {
            int c = input_key();
            if (c < 0)
                break;
            if (c == 0x1B || c == 'q' || c == 'Q' || c == INPUT_NET_PLAY) {
                leave = 1;
                break;
            } else if (c == ' ' || c == '\r' || c == '\n') {
                st = ST_BELL;
                st_frames = 0;
                tasks++;
                bell_chime();
            } else if (c == HID_KEY_RIGHT || c == 'd' || c == 'D') {
                st = (st + 1) % ST_COUNT;
                st_frames = 0;
            } else if (c == HID_KEY_LEFT || c == 'a' || c == 'A') {
                st = (st + ST_COUNT - 1) % ST_COUNT;
                st_frames = 0;
            } else if (c >= '1' && c <= '6') {
                st = c - '1';
                st_frames = 0;
                if (st == ST_BELL)
                    bell_chime();
            }
        }
        int quit = 0;
        uint32_t b = input_buttons(&quit);
        uint32_t pressed = b & ~prev_btn;
        prev_btn = b;
        if (quit || (pressed & HID_B))
            leave = 1;
        else if (pressed & (HID_A | HID_START)) {
            st = ST_BELL;
            st_frames = 0;
            tasks++;
            bell_chime();
        } else if (pressed & HID_RIGHT) {
            st = (st + 1) % ST_COUNT;
            st_frames = 0;
        } else if (pressed & HID_LEFT) {
            st = (st + ST_COUNT - 1) % ST_COUNT;
            st_frames = 0;
        }
        if (leave)
            break;

        frame++;
        st_frames++;
        if ((st == ST_CODING || st == ST_DEBUGGING) && (frame % 6) == 0)
            loc++;
        if (st_frames >= states[st].frames) {
            st_frames = 0;
            st = (st + 1) % ST_COUNT;
            if (st == ST_BELL) {
                tasks++;
                bell_chime();
            }
        }

        uint32_t elapsed = timer_ticks() - frame_t0;
        if (elapsed < 33000u)
            timer_delay_us(33000u - elapsed);
    }

    input_pad_keys(0);
    free(prev_canvas);
    free(canvas);
}
