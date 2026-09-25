/*
 * bm33 serial chainloader.
 *
 * Protocol (all integers little endian), see tools/bm33_load.py:
 *   loader -> host : "\x03\x03\x03"          every second while idle
 *   host -> loader : "BM33" size:u32 crc32:u32
 *   loader -> host : "OK" | "SE" (bad size)
 *   host -> loader : <size bytes of kernel.img>
 *   loader -> host : "OK" and jump to 0x8000 | "CE" (crc mismatch) | "TO" (timeout)
 */
#include <stdint.h>

#include "drivers/led.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "lib/crc32.h"

#define KERNEL_ADDR     0x8000u
#define KERNEL_MAX      (0x02000000u - KERNEL_ADDR - 0x100000u)  /* keep 1 MiB for our stack */
#define BYTE_TIMEOUT_US 2000000u

void boot_kernel(uint32_t r0, uint32_t r1, uint32_t r2) __attribute__((noreturn));

static const char magic[4] = { 'B', 'M', '3', '3' };

static void wait_for_magic(void)
{
    uint32_t last = timer_ticks() - 1000000;
    unsigned matched = 0;
    int led = 0;

    while (matched < sizeof magic) {
        if (timer_ticks() - last >= 1000000) {
            last = timer_ticks();
            uart_write("\x03\x03\x03", 3);
            led = !led;
            led_set(led);
        }
        if (uart_rx_ready()) {
            char c = uart_getc();
            if (c == magic[matched])
                matched++;
            else
                matched = (c == magic[0]);
        }
    }
}

static int read_u32(uint32_t *v)
{
    uint32_t r = 0;
    for (int i = 0; i < 4; i++) {
        char c;
        if (!uart_getc_timeout(BYTE_TIMEOUT_US, &c))
            return 0;
        r |= (uint32_t)(uint8_t)c << (8 * i);
    }
    *v = r;
    return 1;
}

void loader_main(uint32_t r0, uint32_t r1, uint32_t r2)
{
    uint8_t *dst = (uint8_t *)KERNEL_ADDR;

    led_init();
    uart_init();
    uart_puts("\nbm33 chainloader: waiting for kernel\n");

    for (;;) {
        uint32_t size, crc;

        wait_for_magic();
        if (!read_u32(&size) || !read_u32(&crc)) {
            uart_write("TO", 2);
            continue;
        }
        if (size == 0 || size > KERNEL_MAX) {
            uart_write("SE", 2);
            continue;
        }
        uart_write("OK", 2);

        led_set(1);
        uint32_t i;
        for (i = 0; i < size; i++) {
            char c;
            if (!uart_getc_timeout(BYTE_TIMEOUT_US, &c))
                break;
            dst[i] = (uint8_t)c;
        }
        led_set(0);

        if (i != size) {
            uart_write("TO", 2);
            continue;
        }
        if (crc32(dst, size) != crc) {
            uart_write("CE", 2);
            continue;
        }

        uart_write("OK", 2);
        uart_flush();
        boot_kernel(r0, r1, r2);
    }
}
