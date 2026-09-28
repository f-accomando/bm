#include "input.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "gfx/console.h"
#include "lib/printf.h"
#include "usb/hid.h"
#include "usb/usb.h"
#include "bt/bt.h"

static void update_uptime(void)
{
    char buf[24];
    uint32_t s = timer_ticks() / 1000000;
    ksnprintf(buf, sizeof buf, "up %02lu:%02lu:%02lu", s / 3600, s / 60 % 60, s % 60);
    console_set_status(0, buf);
}

int input_key(void)
{
    if (uart_rx_ready())
        return (unsigned char)uart_getc();
    usb_poll();
    bt_poll();
    return hid_getc();
}

char input_getc(void)
{
    uint32_t last = timer_ticks();
    int c;

    update_uptime();
    while ((c = input_key()) < 0) {
        if (timer_ticks() - last >= 1000000) {
            last += 1000000;
            update_uptime();
        }
    }
    return (char)c;
}

uint32_t input_buttons(int *quit)
{
    usb_poll();
    bt_poll();
    if (hid_quit_pressed())
        *quit = 1;
    return hid_buttons();
}

uint32_t input_pad_buttons(int *quit)
{
    usb_poll();
    bt_poll();
    if (hid_quit_pressed())
        *quit = 1;
    return hid_pad_buttons();
}

void input_flush(void)
{
    usb_poll();
    bt_poll();
    while (hid_getc() >= 0)
        ;
    hid_quit_pressed();
}
