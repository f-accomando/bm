#include "pager.h"
#include "input.h"
#include "gfx/console.h"
#include "lib/printf.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "usb/hid.h"

#include <string.h>

#define MAX_LINES 6000

/* line starts, after wrapping at `cols` (escape sequences take no room) */
static const char *starts[MAX_LINES + 1];
static int lens[MAX_LINES];

static int split(const char *t, int cols)
{
    int n = 0;
    while (*t && n < MAX_LINES) {
        const char *s = t;
        int w = 0;
        while (*t && *t != '\n' && w < cols) {
            if (*t == 0x1B) {                       /* ESC [ ... letter */
                t++;
                while (*t && !((*t >= 'A' && *t <= 'Z') || (*t >= 'a' && *t <= 'z')))
                    t++;
                if (*t) t++;
                continue;
            }
            t++;
            w++;
        }
        starts[n] = s;
        lens[n++] = (int)(t - s);
        if (*t == '\n')
            t++;
    }
    return n;
}

/* after a serial Esc: does a sequence follow within 30 ms? */
static int serial_follows(void)
{
    uint32_t t0 = timer_ticks();
    while (timer_ticks() - t0 < 30000)
        if (uart_rx_ready())
            return 1;
    return 0;
}

static void show(int top, int n, int rows)
{
    console_clear();
    static char line[512];
    for (int i = top; i < n && i < top + rows; i++) {
        int l = lens[i] < (int)sizeof line - 1 ? lens[i] : (int)sizeof line - 1;
        memcpy(line, starts[i], (size_t)l);
        line[l] = 0;
        kprintf("%s\x1b[0m\n", line);
    }
    int last = top + rows < n ? top + rows : n;
    kprintf("\x1b[93m-- lines %d-%d of %d: up/down, PgUp/PgDn, space; q or Esc returns --\x1b[0m",
            top + 1, last, n);
}

void pager_show(const char *text)
{
    uint32_t cols, rows;
    console_size(&cols, &rows);
    int page = (int)rows - 1;
    if (page < 3) page = 3;
    int n = split(text, (int)cols - 1);
    if (n <= page) {
        kprintf("%s", text);
        return;
    }
    int top = 0, esc = 0;
    hid_text_mode(1);                               /* arrows and PgUp/PgDn as keys */
    show(top, n, page);
    for (;;) {
        int c = (unsigned char)input_getc(), d = 0;
        if (esc == 1) { esc = c == '[' ? 2 : 0; if (!esc) break; continue; }
        if (esc == 2) {
            esc = 0;
            if (c == 'A') d = -1;
            else if (c == 'B') d = 1;
            else if (c == '5') d = -page;           /* ESC [ 5 ~ */
            else if (c == '6') d = page;
            else if (c == '~') continue;
        } else if (c == 0x1B) {
            /* serial: the start of a sequence; the USB keyboard in text mode
             * sends Esc alone */
            if (serial_follows()) { esc = 1; continue; }
            break;
        } else if (c == 'q' || c == 'Q') {
            break;
        } else if (c == HID_KEY_UP || c == 'w' || c == 'k') {
            d = -1;
        } else if (c == HID_KEY_DOWN || c == 's' || c == 'j' || c == '\r') {
            d = 1;
        } else if (c == HID_KEY_PGUP || c == 'b') {
            d = -page;
        } else if (c == HID_KEY_PGDN || c == ' ') {
            d = page;
        } else if (c == HID_KEY_HOME) {
            d = -n;
        } else if (c == HID_KEY_END) {
            d = n;
        }
        int t = top + d;
        if (t > n - page) t = n - page;
        if (t < 0) t = 0;
        if (t != top) {
            top = t;
            show(top, n, page);
        }
    }
    hid_text_mode(0);
    kprintf("\n");
}
