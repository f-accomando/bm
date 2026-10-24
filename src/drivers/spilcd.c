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

/* ---------------------------------------------------------------- polygon & 3D-shaded helpers */

typedef struct { int x, y; } pt_t;

static void fill_poly(g16_t *g, const pt_t *pts, int n, uint32_t rgb)
{
    if (n < 3)
        return;
    int ymin = pts[0].y, ymax = pts[0].y;
    for (int i = 1; i < n; i++) {
        if (pts[i].y < ymin) ymin = pts[i].y;
        if (pts[i].y > ymax) ymax = pts[i].y;
    }
    if (ymin < 0) ymin = 0;
    if (ymax >= SPILCD_H) ymax = SPILCD_H - 1;
    uint16_t col = g16_rgb24(rgb);

    for (int y = ymin; y <= ymax; y++) {
        int xs[16], nx = 0;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            int y0 = pts[j].y, y1 = pts[i].y;
            int x0 = pts[j].x, x1 = pts[i].x;
            if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
                if (nx < 16)
                    xs[nx++] = x0 + (y - y0) * (x1 - x0) / (y1 - y0);
            }
        }
        for (int i = 0; i < nx - 1; i++)
            for (int k = i + 1; k < nx; k++)
                if (xs[k] < xs[i]) { int t = xs[i]; xs[i] = xs[k]; xs[k] = t; }
        for (int i = 0; i + 1 < nx; i += 2) {
            int x0 = xs[i], x1 = xs[i + 1];
            if (x1 >= x0)
                g16_rectfill(g, x0, y, x1 - x0 + 1, 1, col);
        }
    }
}

static void fill_quad(g16_t *g, int x0, int y0, int x1, int y1,
                      int x2, int y2, int x3, int y3, uint32_t rgb)
{
    pt_t p[4] = { { x0, y0 }, { x1, y1 }, { x2, y2 }, { x3, y3 } };
    fill_poly(g, p, 4, rgb);
}

static void thick_line(g16_t *g, int x0, int y0, int x1, int y1, int r, uint32_t rgb)
{
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int steps = dx > dy ? dx : dy;
    uint16_t c = g16_rgb24(rgb);
    if (steps == 0) {
        g16_circfill(g, x0, y0, r, c);
        return;
    }
    for (int i = 0; i <= steps; i++) {
        int x = x0 + (x1 - x0) * i / steps;
        int y = y0 + (y1 - y0) * i / steps;
        g16_circfill(g, x, y, r, c);
    }
}

static void fill_ellipse(g16_t *g, int cx, int cy, int rx, int ry, uint32_t rgb)
{
    if (rx <= 0 || ry <= 0)
        return;
    uint16_t c = g16_rgb24(rgb);
    int64_t rx2 = (int64_t)rx * rx, ry2 = (int64_t)ry * ry;
    int64_t r2 = rx2 * ry2;
    for (int dy = -ry; dy <= ry; dy++) {
        int dx = 0;
        while (dx <= rx && (int64_t)dx * dx * ry2 + (int64_t)dy * dy * rx2 <= r2)
            dx++;
        dx--;
        if (dx >= 0)
            g16_rectfill(g, cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
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

/* ---------------------------------------------------------------- Tiny Robot Works On My Desk */

enum {
    ST_CODING = 0,
    ST_THINKING,
    ST_BUILDING,
    ST_DEBUGGING,
    ST_BELL,
    ST_COFFEE,
    ST_COUNT
};

static const struct {
    const char *badge;
    const char *sub;
    uint32_t col;
    unsigned frames;
} states[ST_COUNT] = {
    { "TYPING CODE",  "working at the desk laptop...", 0x5CE1E6, 150 },
    { "THINKING...",  "looking around & planning...",  0xFFD166,  85 },
    { "COMPILING",    "building bare-metal kernel...", 0x7AE582,  95 },
    { "DEBUGGING",    "fast typing on the keyboard!",  0xFF6B6B,  85 },
    { "TASK DONE!",   "ringing the desk bell *DING!*", 0x9BF6FF,  75 },
    { "COFFEE BREAK", "resting servos for a moment",   0xF4A261,  75 },
};

/* Background: blurred studio desk environment + warm pink-beige tabletop
 * matching the PiSugarStudio "This Tiny Robot Works On My Desk" video. */
static void draw_studio_bg(g16_t *g)
{
    /* Upper blurred studio workbench backdrop */
    for (int y = 0; y < 175; y++) {
        int r = 165 - y / 5;
        int gr = 172 - y / 5;
        int b = 175 - y / 5;
        g16_rectfill(g, 0, y, SPILCD_W, 1, g16_rgb((uint32_t)r, (uint32_t)gr, (uint32_t)b));
    }
    /* Subtle out-of-focus studio equipment shapes on the workbench behind */
    round_box(g, -10, 32, 95, 110, 8, 0x7E878C);
    g16_rectfill(g, -6, 48, 84, 3, g16_rgb24(0x9BA4A8));
    g16_rectfill(g, -6, 78, 84, 3, g16_rgb24(0x9BA4A8));
    g16_rectfill(g, -6, 108, 84, 3, g16_rgb24(0x9BA4A8));
    round_box(g, 290, 26, 70, 90, 6, 0x8D9699);
    /* Dark PCB / dev board silhouette on background table */
    fill_quad(g, 286, 44, 368, 52, 352, 92, 272, 82, 0x2B302C);
    fill_quad(g, 268, 78, 376, 86, 368, 126, 260, 118, 0xDEDAD0);

    /* Warm pink-beige tabletop surface in the lower half */
    for (int y = 175; y < SPILCD_H; y++) {
        int d = y - 175;
        int r = 206 - d / 9;
        int gr = 174 - d / 8;
        int b = 164 - d / 8;
        g16_rectfill(g, 0, y, SPILCD_W, 1, g16_rgb((uint32_t)r, (uint32_t)gr, (uint32_t)b));
    }
    /* Diagonal desk surface boundary shadow */
    fill_quad(g, 0, 198, 120, 150, 480, 150, 480, 200, 0xC8ACA2);
}

/* Draws the robot's black office chair, beige boxy torso with 3 vent slots,
 * blue SG90 shoulder/neck servos with yellow labels, and turning/tilting
 * wide beige visor head with glowing white eyes. */
static void draw_robot_upper(g16_t *g, unsigned frame, int st)
{
    int bob = 0, turn_x = 0, tilt_y = 0;
    if (st == ST_CODING) {
        bob = (frame & 2) ? -1 : 0;
        turn_x = (int)((frame >> 3) % 7) - 3;
        tilt_y = 2;
    } else if (st == ST_DEBUGGING) {
        bob = (frame & 1) ? -1 : 1;
        turn_x = (int)((frame >> 2) % 9) - 4;
        tilt_y = 3;
    } else if (st == ST_THINKING) {
        turn_x = ((frame >> 4) & 1) ? -6 : 5;
        tilt_y = -3;
    } else if (st == ST_BUILDING) {
        bob = ((frame >> 2) & 1) ? -2 : 0;
        tilt_y = 1;
    } else if (st == ST_BELL) {
        bob = ((frame >> 1) & 1) ? -2 : 1;
        turn_x = -4;
        tilt_y = -1;
    }

    /* 1. Black curved office chair backrest behind the robot */
    fill_quad(g, 82, 46, 178, 36, 192, 146, 98, 160, 0x12151B);
    fill_quad(g, 80, 50, 86, 46, 102, 158, 95, 160, 0x262C36);
    g16_circfill(g, 92, 56, 10, g16_rgb24(0x12151B));
    /* Lower chair armrest / bracket on the left */
    fill_quad(g, 88, 120, 114, 134, 116, 168, 90, 154, 0x1A1E26);
    g16_line(g, 88, 120, 114, 134, g16_rgb24(0x363E4C));

    /* 2. Robot Torso (3D isometric beige box with 3 horizontal vent slots) */
    int ty = bob;
    /* Left side of torso (shaded beige) */
    fill_quad(g, 112, 74 + ty, 134, 88 + ty, 138, 148 + ty, 116, 134 + ty, 0x9E9578);
    /* Top shoulder deck of torso (light cream) */
    fill_quad(g, 112, 74 + ty, 240, 52 + ty, 262, 64 + ty, 134, 88 + ty, 0xDED6BA);
    /* Dark recessed servo bay inside top of torso */
    fill_quad(g, 124, 74 + ty, 236, 55 + ty, 250, 63 + ty, 138, 83 + ty, 0x141820);

    /* Blue SG90 micro-servos in left shoulder, neck, and right shoulder */
    /* Left shoulder blue servo + yellow sticker */
    fill_quad(g, 124, 72 + ty, 162, 65 + ty, 172, 73 + ty, 134, 80 + ty, 0x1A46B8);
    fill_quad(g, 124, 80 + ty, 134, 80 + ty, 134, 92 + ty, 124, 90 + ty, 0x143896);
    fill_quad(g, 132, 68 + ty, 156, 64 + ty, 164, 70 + ty, 140, 75 + ty, 0xD4B830);
    /* Right shoulder blue servo + yellow sticker */
    fill_quad(g, 204, 58 + ty, 238, 52 + ty, 248, 59 + ty, 214, 65 + ty, 0x1A46B8);
    fill_quad(g, 212, 56 + ty, 234, 52 + ty, 240, 57 + ty, 218, 61 + ty, 0xD4B830);
    /* Center blue neck column servo */
    g16_rectfill(g, 178, 54 + ty, 22, 18, g16_rgb24(0x163BA0));
    g16_rectfill(g, 182, 56 + ty, 14, 12, g16_rgb24(0x2456D4));

    /* Front face of torso (warm retro beige/cream) */
    fill_quad(g, 134, 88 + ty, 262, 64 + ty, 264, 124 + ty, 138, 150 + ty, 0xC9C0A3);
    /* Top edge highlight of front torso plate */
    g16_line(g, 134, 88 + ty, 262, 64 + ty, g16_rgb24(0xE8E0C5));
    g16_line(g, 134, 89 + ty, 262, 65 + ty, g16_rgb24(0xDFD7BC));

    /* 3 horizontal cooling vent slots on the left of the front chest (exact match!) */
    for (int v = 0; v < 3; v++) {
        int vy = 98 + v * 6 + ty;
        g16_line(g, 150, vy,     172, vy - 4, g16_rgb24(0x464236));
        g16_line(g, 150, vy + 1, 172, vy - 3, g16_rgb24(0x2E2B22));
        g16_line(g, 150, vy + 2, 172, vy - 2, g16_rgb24(0xDFD7BC));
    }

    /* Orange / red / brown servo ribbon wires looping out the left side */
    g16_line(g, 128 + turn_x / 2, 42 + ty, 118, 62 + ty, g16_rgb24(0xE86F2C));
    g16_line(g, 130 + turn_x / 2, 43 + ty, 120, 64 + ty, g16_rgb24(0xD63230));
    g16_line(g, 132 + turn_x / 2, 44 + ty, 122, 66 + ty, g16_rgb24(0x5C3A28));

    /* 3. Robot Head (wide retro beige visor with raised crown & black eye screen) */
    int hx = 132 + turn_x;
    int hy = 18 + ty + tilt_y;

    /* Raised top cap on crown of head */
    fill_quad(g, hx + 8, hy - 6, hx + 76, hy - 14, hx + 104, hy + 2, hx + 34, hy + 12, 0xDFD7BC);
    fill_quad(g, hx + 8, hy - 6, hx + 34, hy + 12, hx + 34, hy + 18, hx + 8, hy - 1, 0xB5AC90);
    /* Left side cheek of visor */
    fill_quad(g, hx, hy + 8, hx + 16, hy + 18, hx + 18, hy + 50, hx + 2, hy + 38, 0xACA386);
    /* Small side bracket & screw on left of head */
    fill_quad(g, hx - 4, hy + 6, hx + 4, hy + 11, hx + 5, hy + 28, hx - 3, hy + 22, 0xC8BFA2);
    g16_circfill(g, hx, hy + 16, 2, g16_rgb24(0x4A4E54));
    /* Top bevel of main visor */
    fill_quad(g, hx, hy + 8, hx + 112, hy - 6, hx + 128, hy + 4, hx + 16, hy + 18, 0xE6DEC3);
    /* Front face of wide visor */
    fill_quad(g, hx + 16, hy + 18, hx + 128, hy + 4, hx + 128, hy + 36, hx + 18, hy + 50, 0xD2C9AC);
    g16_line(g, hx + 16, hy + 18, hx + 128, hy + 4, g16_rgb24(0xF0E8CE));

    /* Recessed black rectangular OLED screen in the middle of the visor */
    fill_quad(g, hx + 42, hy + 19, hx + 102, hy + 11, hx + 102, hy + 34, hx + 43, hy + 42, 0x2A2622);
    fill_quad(g, hx + 45, hy + 21, hx + 99,  hy + 14, hx + 99,  hy + 32, hx + 46, hy + 39, 0x0D0F14);

    /* Expressive glowing white eyes inside the black OLED screen */
    int blink = (frame % 70) >= 66;
    int ex_shift = (turn_x > 0) ? 2 : (turn_x < 0) ? -2 : 0;
    int elx = hx + 53 + ex_shift, ely = hy + 24;
    int erx = hx + 79 + ex_shift, ery = hy + 21;

    if (st == ST_BELL) {
        /* Happy glowing white ^ ^ eyes */
        g16_circfill(g, elx + 5, ely + 5, 6, g16_rgb24(0xFFFFFF));
        g16_circfill(g, elx + 5, ely + 8, 5, g16_rgb24(0x0D0F14));
        g16_circfill(g, erx + 5, ery + 5, 6, g16_rgb24(0xFFFFFF));
        g16_circfill(g, erx + 5, ery + 8, 5, g16_rgb24(0x0D0F14));
    } else if (blink || st == ST_COFFEE) {
        g16_line(g, elx, ely + 4, elx + 10, ely + 3, g16_rgb24(0xFFFFFF));
        g16_line(g, elx, ely + 5, elx + 10, ely + 4, g16_rgb24(0xFFFFFF));
        g16_line(g, erx, ery + 4, erx + 10, ery + 3, g16_rgb24(0xFFFFFF));
        g16_line(g, erx, ery + 5, erx + 10, ery + 4, g16_rgb24(0xFFFFFF));
    } else {
        /* Glowing white rounded-rectangle eyes as in the photo */
        round_box(g, elx - 1, ely - 1, 13, 10, 3, 0x8AB4F8);
        round_box(g, elx,     ely,     11,  8, 2, 0xFFFFFF);
        round_box(g, erx - 1, ery - 1, 13, 10, 3, 0x8AB4F8);
        round_box(g, erx,     ery,     11,  8, 2, 0xFFFFFF);
        if (st == ST_DEBUGGING) {
            /* Focused squinting top mask */
            g16_line(g, elx, ely, elx + 11, ely + 2, g16_rgb24(0x0D0F14));
            g16_line(g, erx, ery + 2, erx + 11, ery, g16_rgb24(0x0D0F14));
        }
    }
}

/* Draws the black isometric desk box with 4 beige corner bumpers and silver
 * screws, the dome button, the coffee mug, the glowing mushroom lamp, the
 * robot's black articulated arms/hands, and the open cream laptop. */
static void draw_desk_and_arms(g16_t *g, unsigned frame, int st)
{
    /* 1. Black isometric desk box base (left wall and front wall) */
    /* Left side wall of the black desk box */
    fill_quad(g, 36, 160, 118, 232, 124, 308, 44, 224, 0x14171D);
    /* Subtle geometric vent pattern on left side wall */
    for (int i = 0; i < 4; i++) {
        int vx = 54 + i * 14, vy = 192 + i * 12;
        g16_line(g, vx, vy, vx + 8, vy + 24, g16_rgb24(0x222730));
        g16_line(g, vx + 6, vy - 4, vx - 2, vy + 20, g16_rgb24(0x222730));
    }
    /* Front wall of the black desk box */
    fill_quad(g, 118, 232, 444, 168, 430, 246, 124, 308, 0x1A1E26);
    /* Two small mounting holes on the front wall (visible in photo!) */
    g16_circfill(g, 228, 250, 2, g16_rgb24(0x090B0E));
    g16_circfill(g, 356, 222, 2, g16_rgb24(0x090B0E));

    /* Cream/beige trim line right under the black desktop plate */
    thick_line(g, 36, 160, 118, 232, 1, 0xD6CEB2);
    thick_line(g, 118, 232, 444, 168, 1, 0xD6CEB2);

    /* Top matte black desktop surface */
    fill_quad(g, 36, 157, 326, 110, 444, 165, 118, 229, 0x20252E);
    /* Subtle specular sheen across the black desktop */
    fill_quad(g, 52, 156, 322, 113, 360, 130, 82, 182, 0x262C37);

    /* 4 Beige 3D-printed corner bumpers + 4 metallic silver screws */
    /* Left corner bumper & screw */
    round_box(g, 28, 153, 14, 12, 4, 0xD6CEB2);
    g16_circfill(g, 48, 158, 4, g16_rgb24(0xC8D0D8));
    g16_circfill(g, 48, 158, 2, g16_rgb24(0x5A626A));
    /* Front-bottom corner bumper & screw */
    round_box(g, 106, 224, 24, 14, 5, 0xD6CEB2);
    fill_quad(g, 108, 230, 130, 226, 128, 236, 110, 238, 0xB8AF93);
    g16_circfill(g, 118, 220, 5, g16_rgb24(0xD8E0E8));
    g16_line(g, 116, 220, 120, 220, g16_rgb24(0x4A525A));
    g16_line(g, 118, 218, 118, 222, g16_rgb24(0x4A525A));
    /* Back-right screw */
    g16_circfill(g, 324, 115, 4, g16_rgb24(0xC8D0D8));
    g16_circfill(g, 324, 115, 2, g16_rgb24(0x5A626A));
    /* Front-right corner bumper & screw */
    round_box(g, 434, 162, 16, 12, 4, 0xD6CEB2);
    g16_circfill(g, 430, 163, 4, g16_rgb24(0xC8D0D8));
    g16_circfill(g, 430, 163, 2, g16_rgb24(0x5A626A));

    /* 2. Cream Dome Button / Bell on left of desk */
    int bell_x = 132, bell_y = 160;
    fill_ellipse(g, bell_x, bell_y + 4, 13, 6, 0x151920);
    int bell_press = (st == ST_BELL && (frame & 2)) ? 2 : 0;
    g16_circfill(g, bell_x, bell_y - 1 + bell_press, 9, g16_rgb24(0xCFC7AA));
    g16_circfill(g, bell_x - 2, bell_y - 3 + bell_press, 5, g16_rgb24(0xEAE2C8));
    fill_ellipse(g, bell_x, bell_y + 3, 10, 3, 0x20252E);
    if (st == ST_BELL) {
        int r = 13 + (int)((frame * 2) % 14);
        g16_circ(g, bell_x, bell_y - 2, r, g16_rgb24(0x9BF6FF));
        g16_circ(g, bell_x, bell_y - 2, r + 4, g16_rgb24(0xFFD166));
    }

    /* 3. Laptop Keyboard Base (cream/beige with round keycaps) */
    fill_quad(g, 174, 158, 282, 136, 314, 162, 204, 190, 0xC8BFA2);
    fill_quad(g, 174, 155, 282, 133, 314, 159, 204, 186, 0xDED6BA);
    /* Left/front thickness of laptop base */
    fill_quad(g, 174, 155, 204, 186, 204, 192, 174, 161, 0xABA286);
    /* Round cream keycaps on the keyboard */
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 7; c++) {
            int kx = 190 + c * 12 + r * 6;
            int ky = 156 - c * 2 + r * 6;
            int active = (st == ST_CODING || st == ST_DEBUGGING) &&
                         ((int)((frame + r * 3 + c * 5) % 9) == 0);
            g16_circfill(g, kx, ky + (active ? 1 : 0), 3,
                         g16_rgb24(active ? 0xFFFFFF : 0xEFE8CE));
            g16_circ(g, kx, ky + (active ? 1 : 0), 3, g16_rgb24(0x9E9578));
        }
    }

    /* 4. Robot's Black Articulated Servo Arms & 4-Fingered Hands */
    /* Left arm (viewer's left: robot's right arm) */
    int l_sh_x = 112, l_sh_y = 92;
    int l_el_x = 118, l_el_y = 140;
    int l_wr_x = 186, l_wr_y = 150;
    /* Right arm (viewer's right: robot's left arm) */
    int r_sh_x = 258, r_sh_y = 68;
    int r_el_x = 282, r_el_y = 104;
    int r_wr_x = 268, r_wr_y = 136;

    if (st == ST_CODING || st == ST_BUILDING) {
        l_wr_y += (frame & 2) ? 3 : -2;
        r_wr_y += (frame & 2) ? -2 : 3;
        l_wr_x += (int)((frame >> 1) % 7) - 3;
        r_wr_x += (int)(((frame >> 1) + 3) % 7) - 3;
    } else if (st == ST_DEBUGGING) {
        l_wr_y += (frame & 1) ? 4 : -3;
        r_wr_y += (frame & 1) ? -3 : 4;
        l_wr_x += (int)(frame % 9) - 4;
        r_wr_x += (int)((frame + 4) % 9) - 4;
    } else if (st == ST_BELL) {
        /* Robot's right hand (viewer's left) taps the dome button! */
        l_wr_x = bell_x - 4;
        l_wr_y = bell_y - 6 + bell_press;
    } else if (st == ST_THINKING) {
        l_wr_y -= 4;
    }

    /* Draw viewer-left black arm & shoulder/elbow silver screws */
    thick_line(g, l_sh_x, l_sh_y, l_el_x, l_el_y, 5, 0x181C22);
    thick_line(g, l_sh_x - 1, l_sh_y, l_el_x - 1, l_el_y, 2, 0x2E3540);
    g16_circfill(g, l_sh_x + 2, l_sh_y + 2, 3, g16_rgb24(0xC8D0D8));
    thick_line(g, l_el_x, l_el_y, l_wr_x, l_wr_y, 4, 0x181C22);
    thick_line(g, l_el_x, l_el_y - 1, l_wr_x, l_wr_y - 1, 1, 0x343C48);
    g16_circfill(g, l_el_x, l_el_y, 3, g16_rgb24(0x9AA2AC));
    /* 4 articulated black fingers on viewer-left hand */
    for (int f = 0; f < 4; f++) {
        int fx = l_wr_x + 10 + f * 4;
        int fy = l_wr_y + 8 - f * 2 + (((frame + f) & 2) && (st == ST_CODING || st == ST_DEBUGGING) ? 2 : 0);
        thick_line(g, l_wr_x + 3, l_wr_y + 1, fx, fy, 1, 0x14171D);
    }

    /* Draw viewer-right black arm & shoulder/elbow silver screws */
    thick_line(g, r_sh_x, r_sh_y, r_el_x, r_el_y, 5, 0x181C22);
    thick_line(g, r_sh_x + 1, r_sh_y, r_el_x + 1, r_el_y, 2, 0x2E3540);
    g16_circfill(g, r_sh_x, r_sh_y + 2, 3, g16_rgb24(0xC8D0D8));
    thick_line(g, r_el_x, r_el_y, r_wr_x, r_wr_y, 4, 0x181C22);
    g16_circfill(g, r_el_x, r_el_y, 3, g16_rgb24(0x9AA2AC));
    /* 4 articulated black fingers on viewer-right hand */
    for (int f = 0; f < 4; f++) {
        int fx = r_wr_x - 8 + f * 5;
        int fy = r_wr_y + 12 - f + (((frame + f + 1) & 2) && (st == ST_CODING || st == ST_DEBUGGING) ? 2 : 0);
        thick_line(g, r_wr_x, r_wr_y + 2, fx, fy, 1, 0x14171D);
    }

    /* 5. Tilted Cream Laptop Lid (seen from the back/3/4 angle, as in the photo) */
    /* Soft screen glow spilling from the front of the open laptop */
    thick_line(g, 206, 144, 338, 120, 2, (frame & 4) ? 0xA8F0FF : 0x7CE0F8);
    /* Back of the tilted cream laptop screen lid */
    fill_quad(g, 204, 145, 342, 120, 316, 166, 204, 192, 0xC6BDA0);
    fill_quad(g, 206, 143, 340, 118, 314, 163, 204, 189, 0xDDD5B8);
    /* Rounded top rim of the laptop screen lid */
    thick_line(g, 206, 143, 340, 118, 2, 0xEDE6CC);
    /* Small dark diamond cutout logo in the center of the laptop lid */
    fill_quad(g, 274, 152, 281, 149, 279, 156, 271, 157, 0x26282A);
    /* Laptop bottom hinge base */
    fill_quad(g, 204, 185, 314, 160, 314, 168, 204, 194, 0xACA386);

    /* 6. Right Side: Cream Mushroom Lamp with Glowing Blue-White Column */
    /* Cream rectangular lamp base */
    fill_quad(g, 332, 134, 378, 125, 408, 146, 358, 158, 0xBFB69A);
    fill_quad(g, 332, 131, 378, 122, 408, 143, 358, 155, 0xDED6BA);
    /* Soft blue-white light pool on the lamp base */
    fill_ellipse(g, 370, 139, 16, 7, (frame & 8) ? 0xF2F8FF : 0xDCEEFF);
    /* Small dark hole in front-right of lamp base */
    fill_ellipse(g, 386, 147, 3, 2, 0x22262E);
    /* Glowing ice-blue / white vertical cylindrical light column */
    int lx = 364, ly = 114;
    g16_rectfill(g, lx - 1, ly, 16, 24, g16_rgb24(0x88C8F8));
    g16_rectfill(g, lx + 1, ly, 12, 24, g16_rgb24(0xCBE8FF));
    g16_rectfill(g, lx + 4, ly,  6, 24, g16_rgb24(0xFFFFFF));
    fill_ellipse(g, lx + 7, ly + 24, 8, 3, 0xE6F4FF);
    /* Mushroom dome cap on top of the lamp */
    fill_ellipse(g, lx + 7, ly, 17, 6, 0xB5AC90);
    g16_circfill(g, lx + 7, ly - 4, 15, g16_rgb24(0xD8CFAF));
    g16_circfill(g, lx + 4, ly - 7, 10, g16_rgb24(0xEAE2C6));
    fill_ellipse(g, lx + 7, ly + 1, 16, 4, 0xC4BB9E);

    /* 7. Front-Left Corner: Cream 3D-Printed Coffee Mug */
    int mx = 128, my = 186;
    /* Saucer lip at bottom of mug */
    fill_ellipse(g, mx + 12, my + 24, 16, 7, 0xABA286);
    fill_ellipse(g, mx + 12, my + 22, 16, 6, 0xDDD4B6);
    /* Mug cylindrical body */
    g16_rectfill(g, mx, my, 25, 22, g16_rgb24(0xCFC6A8));
    g16_rectfill(g, mx + 2, my, 12, 22, g16_rgb24(0xDFD7BA));
    fill_ellipse(g, mx + 12, my + 21, 12, 5, 0xCFC6A8);
    /* Mug handle on the left */
    thick_line(g, mx, my + 4, mx - 5, my + 8, 2, 0xDED6BA);
    thick_line(g, mx - 5, my + 8, mx - 4, my + 16, 2, 0xCFC6A8);
    thick_line(g, mx - 4, my + 16, mx + 1, my + 19, 2, 0xB8AF93);
    /* Top rim of mug & dark interior */
    fill_ellipse(g, mx + 12, my, 12, 5, 0xECE4CA);
    fill_ellipse(g, mx + 12, my, 9, 3, 0x4A3B2C);
    /* Subtle rising steam wisps */
    for (int s = 0; s < 2; s++) {
        int sy = my - 6 - (int)(((frame + s * 9) / 2) % 12);
        int sx = mx + 9 + s * 6 + (((frame >> 2) + s) & 1 ? 1 : -1);
        g16_circfill(g, sx, sy, 1, g16_rgb24(0xEAE2C8));
    }
}

static void draw_hud(g16_t *g, int st, unsigned tasks, uint32_t t0)
{
    /* Compact translucent-style status pill at top-left and bottom-right */
    round_box(g, 8, 6, 228, 18, 4, 0x161A22);
    g16_circfill(g, 17, 15, 4, g16_rgb24(states[st].col));
    g->font = &font_console_6x12;
    g16_text(g, 26, 9, states[st].badge, g16_rgb24(states[st].col));
    g16_text(g, 104, 9, states[st].sub, g16_rgb24(0xD8DEE9));

    char stat[32];
    uint32_t sec = (timer_ticks() - t0) / 1000000u;
    ksnprintf(stat, sizeof stat, "TASKS:%02u  %02lu:%02lu",
              tasks, (unsigned long)(sec / 60), (unsigned long)(sec % 60));
    round_box(g, SPILCD_W - 104, 6, 96, 18, 4, 0x161A22);
    g16_text(g, SPILCD_W - 98, 9, stat, g16_rgb24(0xEAE2C8));
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
            const uint16_t *row_c = cur  + (y0 + r) * SPILCD_W;
            const uint16_t *row_p = prev + (y0 + r) * SPILCD_W;
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
    uint16_t *bg_canvas = malloc(bytes);
    uint16_t *canvas = malloc(bytes);
    uint16_t *prev_canvas = malloc(bytes);
    if (!bg_canvas || !canvas || !prev_canvas) {
        free(bg_canvas);
        free(canvas);
        free(prev_canvas);
        return;
    }

    (void)spilcd_init();

    g16_t g;
    g16_target(&g, bg_canvas, SPILCD_W, SPILCD_W, SPILCD_H, &font_console_6x12);
    draw_studio_bg(&g);

    g16_target(&g, canvas, SPILCD_W, SPILCD_W, SPILCD_H, &font_console_6x12);

    input_pad_keys(INPUT_PAD_NAV);
    input_flush();
    uint32_t prev_btn = hid_buttons();
    uint32_t t0 = timer_ticks();

    int st = ST_CODING;
    unsigned st_frames = 0, frame = 0, tasks = 1;

    for (;;) {
        uint32_t frame_t0 = timer_ticks();

        memcpy(canvas, bg_canvas, bytes);
        draw_robot_upper(&g, frame, st);
        draw_desk_and_arms(&g, frame, st);
        draw_hud(&g, st, tasks, t0);

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
    free(bg_canvas);
}
