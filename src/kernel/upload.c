#include "upload.h"
#include "carts.h"
#include "bm/runtime.h"
#include "drivers/uart.h"
#include "lib/crc32.h"
#include "lib/printf.h"
#include "s32/player.h"

#include <stdlib.h>
#include <string.h>

#define MAX_CART    (16u << 20)
#define WAIT_US     15000000u
#define BYTE_US     2000000u

static int get_u32(uint32_t *v)
{
    uint32_t r = 0;
    for (int i = 0; i < 4; i++) {
        char c;
        if (!uart_getc_timeout(BYTE_US, &c))
            return 0;
        r |= (uint32_t)(uint8_t)c << (8 * i);
    }
    *v = r;
    return 1;
}

void upload_and_play(framebuffer_t *fb)
{
    static const char magic[4] = { 'B', 'M', 'L', 'D' };
    unsigned matched = 0;
    uint32_t size, crc;
    char c;

    kprintf("send a .bm or .cart now (bm_load.py --cart FILE), 15 s timeout\n");
    while (matched < 4) {
        if (!uart_getc_timeout(WAIT_US, &c)) {
            kprintf("upload: timeout\n");
            return;
        }
        matched = c == magic[matched] ? matched + 1 : (c == magic[0]);
    }
    if (!get_u32(&size) || !get_u32(&crc)) {
        uart_write("TO", 2);
        return;
    }
    uint8_t *buf = (size && size <= MAX_CART) ? malloc(size) : NULL;
    if (!buf) {
        uart_write("SE", 2);
        return;
    }
    uart_write("OK", 2);
    for (uint32_t i = 0; i < size; i++) {
        if (!uart_getc_timeout(BYTE_US, &c)) {
            uart_write("TO", 2);
            free(buf);
            return;
        }
        buf[i] = (uint8_t)c;
    }
    if (crc32(buf, size) != crc) {
        uart_write("CE", 2);
        free(buf);
        return;
    }
    uart_write("OK", 2);
    kprintf("\nupload: %lu bytes received\n", size);

    carts_play_buffer(fb, buf, size);
    free(buf);
}
