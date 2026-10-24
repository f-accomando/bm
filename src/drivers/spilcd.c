/* 3.5" SPI TFT (ILI9486, 480x320) on SPI0 of the BCM2835. Display only: the
 * XPT2046 touch chip on the board is not read. See spilcd.h for the wiring.
 *
 * The board turns the SPI stream into a 16-bit bus with shift registers, so
 * a command or a parameter is sent as one 16-bit word (high byte 0) and a
 * pixel as its RGB565 word, high byte first. DC low = command, high = data. */
#include "spilcd.h"
#include "gpio.h"
#include "mmio.h"
#include "timer.h"

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

/* Sends `n` bytes in one chip-select cycle. Reads the receive FIFO as it
 * goes: a full one stops the clock. */
static void spi_send(const uint8_t *p, unsigned n)
{
    unsigned rx = n;

    mmio_write(SPI0_CS, CS_CLEAR);
    mmio_write(SPI0_CS, CS_TA);
    while (n || rx) {
        uint32_t cs = mmio_read(SPI0_CS);
        if (n && (cs & CS_TXD)) {
            mmio_write(SPI0_FIFO, *p++);
            n--;
        }
        if (rx && (cs & CS_RXD)) {
            (void)mmio_read(SPI0_FIFO);
            rx--;
        }
    }
    while (!(mmio_read(SPI0_CS) & CS_DONE))
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
        for (;;) {
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
    while (*rx) {
        if (mmio_read(SPI0_CS) & CS_RXD) {
            (void)mmio_read(SPI0_FIFO);
            (*rx)--;
        }
    }
    while (!(mmio_read(SPI0_CS) & CS_DONE))
        ;
    mmio_write(SPI0_CS, 0);
}

int spilcd_init(void)
{
    static const uint8_t pix16 = 0x55;      /* 16 bits per pixel */
    static const uint8_t pwr2 = 0x44;
    static const uint8_t vcom[4] = { 0, 0, 0, 0 };
    static const uint8_t mad = SPILCD_MADCTL;
    static const uint8_t zero = 0;

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
    lcd_cmd_args(0xC2, &pwr2, 1);
    lcd_cmd_args(0xC5, vcom, 4);
    lcd_cmd_args(0x36, &mad, 1);
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
