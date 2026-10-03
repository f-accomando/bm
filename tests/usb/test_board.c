/* Host tests for src/drivers/board.c: revision codes of the BCM2835 boards
 * and of the Pi Zero 2 W; built twice, as for kernel.img and kernel7.img
 * (-DBM_ZERO2: an unknown board has the LED of the Zero 2 W). */
#include "drivers/board.h"
#include "drivers/prop.h"

#include <stdio.h>
#include <string.h>

int prop_query(uint32_t tag, uint32_t *v, unsigned n) { (void)tag; (void)v; (void)n; return -1; }

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static board_t b;

static int is(uint32_t rev, int model, const char *name, int led, int high, int wireless, int eth)
{
    board_decode(rev, &b);
    int ok = b.model == model && strcmp(b.name, name) == 0 && b.led_pin == led &&
             b.led_active_high == high && b.wireless == wireless && b.ethernet == eth;
    if (!ok)
        printf("  %08x -> model %d '%s' led %d/%d wireless %d eth %d\n", rev, b.model, b.name,
               b.led_pin, b.led_active_high, b.wireless, b.ethernet);
    return ok;
}

int main(void)
{
    /* the reference board and QEMU's raspi0 */
    CHECK(is(0x9000c1, BOARD_ZERO_W, "Pi Zero W", 47, 0, 1, 0));
    CHECK(is(0x920092, BOARD_ZERO, "Pi Zero", 47, 0, 1, 0));
    /* Pi 1 B: old-style codes, the warranty bit ignored */
    CHECK(is(0x000e, BOARD_PI1_B, "Pi 1 B rev 2.0", 16, 0, 0, 1));
    CHECK(is(0x0004, BOARD_PI1_B, "Pi 1 B rev 2.0", 16, 0, 0, 1));
    CHECK(is(0x1000003, BOARD_PI1_B, "Pi 1 B rev 1.0", 16, 0, 0, 1));
    CHECK(is(0x0008, BOARD_PI1_A, "Pi 1 A", 16, 0, 0, 0));
    CHECK(is(0x0010, BOARD_PI1_BPLUS, "Pi 1 B+", 47, 1, 0, 1));
    CHECK(is(0x0015, BOARD_PI1_APLUS, "Pi 1 A+", 47, 1, 0, 0));
    CHECK(is(0x0011, BOARD_CM1, "Compute Module 1", 0, 0, 0, 0));
    /* new-style codes of the same boards */
    CHECK(is(0x900021, BOARD_PI1_APLUS, "Pi 1 A+", 47, 1, 0, 0));
    CHECK(is(0x900032, BOARD_PI1_BPLUS, "Pi 1 B+", 47, 1, 0, 1));
    CHECK(is(0x900093, BOARD_ZERO, "Pi Zero", 47, 0, 1, 0));
    /* the Pi Zero 2 W (kernel7.img), and the Pi 2 B of QEMU's raspi2b */
    CHECK(is(0x902120, BOARD_ZERO_2W, "Pi Zero 2 W", 29, 0, 1, 0) && b.bt_on_pin == 42);
    CHECK(is(0x9000c1, BOARD_ZERO_W, "Pi Zero W", 47, 0, 1, 0) && b.bt_on_pin == 45);
    CHECK(is(0xa21041, BOARD_PI2_B, "Pi 2 B", 47, 1, 0, 1));
    CHECK(is(0xa22042, BOARD_PI2_B, "Pi 2 B", 47, 1, 0, 1));
    /* others: the defaults, the code in the name */
    CHECK(is(0xa02082, BOARD_OTHER, "Pi (unknown board) 00a02082", BOARD_LED_PIN, 0, 1, 0));
    CHECK(is(0, BOARD_UNKNOWN, "Pi (unknown board) 00000000", BOARD_LED_PIN, 0, 1, 0));
    /* no firmware answer (prop_query fails): unknown */
    CHECK(board()->model == BOARD_UNKNOWN);

    printf("test_board: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
