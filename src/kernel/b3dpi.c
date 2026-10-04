/*
 * The 3D Bench on the Pi (src/bm/b3d.c): the 640x360 video mode, the
 * ARM1176's counters (on a real Pi: QEMU has neither them nor a V3D),
 * the reports in /bm/bench on the SD card (3D0001.TXT, 3D0002.TXT...),
 * the keys and the pad.
 */
#include "b3dpi.h"
#include "input.h"
#include "pmu.h"
#include "reports.h"
#include "version.h"
#include "bm/b3d.h"
#include "bm/runtime.h"
#include "bm/stress.h"
#include "drivers/prop.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "fs/fat.h"
#include "gpu/v3d.h"
#include "lib/printf.h"
#include "net/net.h"
#include "usb/hid.h"

#include <stdlib.h>
#include <string.h>

#define DIR "/bm/bench"

static framebuffer_t *fbp;
static g16_t page;

static uint32_t us(void) { return timer_ticks(); }

static uint32_t present(void) { return bm_video_present(fbp, &page); }

static void count(b3d_count_t *c)
{
    pmu_t p;
    pmu_read(&p);
    c->cycles = p.cycles;
    c->instr = p.instr;
    c->dmiss = p.dmiss;
    c->wait_instr = pmu_wait_instr;
}

static int key(void)
{
    /* the USB keyboard's arrows and Esc come as a pad's (input_buttons) */
    static uint32_t prev;
    int quit = 0;
    const uint32_t b = input_buttons(&quit), e = b & ~prev;
    prev = b;
    if (quit || (e & HID_B))
        return B3D_KEY_BACK;
    if (e & (HID_LEFT | HID_UP | HID_L1))
        return B3D_KEY_LEFT;
    if (e & (HID_RIGHT | HID_DOWN | HID_R1 | HID_A))
        return B3D_KEY_RIGHT;
    const int k = input_key();
    switch (k) {
    case HID_KEY_LEFT: case HID_KEY_PGUP: case HID_KEY_UP: case 'a': case 'A':
        return B3D_KEY_LEFT;
    case HID_KEY_RIGHT: case HID_KEY_PGDN: case HID_KEY_DOWN: case '\r': case ' ': case 'd': case 'D':
        return B3D_KEY_RIGHT;
    case 0x1B: case 'q': case 'Q': case 'b': case 'B':
        return B3D_KEY_BACK;
    default:
        return B3D_KEY_NONE;
    }
}

static void log_line(const char *s)
{
    uart_puts(s);
    uart_puts("\r\n");
}

/* the number of the last report in DIR (0: none) */
static int last_report(void)
{
    fat_dir_t d;
    fat_entry_t e;
    int best = 0;
    if (fat_opendir(&d, DIR) != 0)
        return 0;
    while (fat_readdir(&d, &e)) {
        const char *n = e.name;
        if ((n[0] == '3') && (n[1] == 'D' || n[1] == 'd') && n[2] >= '0' && n[2] <= '9') {
            const int k = atoi(n + 2);
            if (k > best)
                best = k;
        }
    }
    return best;
}

static char *sent;                      /* the report, for reports.h */
static size_t sent_len;

static int save(const char *text, size_t len, char *name, size_t n)
{
    if (fat_mkdirs(DIR) != 0)
        return -1;
    char file[16];
    ksnprintf(file, sizeof file, "3D%04d.TXT", last_report() + 1);
    if (fat_write_file(DIR, file, text, len) != 0)
        return -1;
    ksnprintf(name, n, "bm/bench/%s", file);
    free(sent);                         /* a copy for the reports, sent once the console is back */
    sent = malloc(len);
    if (sent)
        memcpy(sent, text, len);
    sent_len = sent ? len : 0;
    return 0;
}

static int load_last(char **text, char *name, size_t n)
{
    const int k = last_report();
    if (!k)
        return -1;
    char path[32];
    ksnprintf(path, sizeof path, DIR "/3D%04d.TXT", k);
    fat_entry_t e;
    uint8_t *data;
    size_t len;
    if (fat_find(path, &e) != 0 || fat_load(&e, &data, &len) != 0)
        return -1;
    char *t = malloc(len + 1);
    if (!t) {
        free(data);
        return -1;
    }
    memcpy(t, data, len);
    t[len] = 0;
    free(data);
    *text = t;
    ksnprintf(name, n, "bm/bench/3D%04d.TXT", k);
    return 0;
}

void bm_bench3d(framebuffer_t *fb)
{
    const uint32_t con_w = fb->width, con_h = fb->height;
    bm_stress_settle(20000);            /* as the stress test: the boot's work done */
    static char machine[160];
    uint32_t temp[2] = { 0, 0 }, thr[1] = { 0 };
    prop_query(PROP_GET_TEMPERATURE, temp, 2);
    prop_query(PROP_GET_THROTTLED, thr, 1);
    ksnprintf(machine, sizeof machine, "ARM %lu MHz, core %lu, V3D %lu, SDRAM %lu MHz, %lu.%lu C, throttled %05lx",
              prop_clock_rate(CLOCK_ARM) / 1000000, prop_clock_rate(CLOCK_CORE) / 1000000,
              prop_clock_rate(CLOCK_V3D) / 1000000, prop_clock_rate(CLOCK_SDRAM) / 1000000, temp[1] / 1000,
              temp[1] / 100 % 10, thr[0]);
    fbp = fb;
    if (bm_video_enter(fb, 640, 360, &page) != 0) {
        kprintf("3D Bench: cannot set the video mode\n");
        return;
    }
    const int real = v3d_init() == 0;   /* a real Pi (QEMU has no V3D, nor the counters) */
    if (real)
        pmu_start();
    input_pad_keys(INPUT_PAD_NAV);
    input_flush();
    b3d_platform_t p = {
        .g = &page, .us = us, .present = present, .count = count, .counting = real, .key = key,
        .log = log_line, .save = save, .load_last = load_last, .kernel = bm_version, .machine = machine,
        .date = net_time() ? net_time_text() : "",
    };
    const int err = b3d_run(&p);
    input_pad_keys(0);
    pmu_stop();
    bm_video_leave(fb, con_w, con_h);
    kprintf("3D Bench: done (%s); the report is in bm/bench on the SD card%s\n", machine,
            err ? " (could not be saved)" : "");
    if (sent) {                         /* and to GitHub, if it can (reports.h) */
        reports_text("bench3d", sent, sent_len);
        free(sent);
        sent = NULL;
    }
}
