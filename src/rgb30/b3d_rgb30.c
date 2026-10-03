/*
 * The 3D Bench (src/bm/b3d.c) on the RGB30, as the Pi's src/kernel/b3dpi.c:
 * the 640x360 video mode (made as big as the panel), the Cortex-A55's
 * counters (cycles, instructions, L1 data cache refills), the reports in
 * /bm/bench on the SD card, the console's buttons. The 3D is the ARM's
 * (no Mali driver yet), so every GPU row is skipped.
 */
#include "b3d_rgb30.h"
#include "pad.h"
#include "bm/b3d.h"
#include "bm/runtime.h"
#include "drivers/timer.h"
#include "fs/fat.h"
#include "kernel/version.h"
#include "lib/printf.h"
#include "net/net.h"

#include <stdlib.h>
#include <string.h>

#define DIR "/bm/bench"

static framebuffer_t *fbp;
static g16_t page;

static uint32_t us(void) { return timer_ticks(); }

static uint32_t present(void) { return bm_video_present(fbp, &page); }

/* --- the Cortex-A55's performance counters (EL1; start.S gives them all
 * to EL1 through MDCR_EL2.HPMN) --- */

#define read_sys(r) ({ uint64_t v_; __asm__ volatile("mrs %0, " #r : "=r"(v_)); v_; })
#define write_sys(r, v) __asm__ volatile("msr " #r ", %0" :: "r"((uint64_t)(v)))

static int pmu_start(void)
{
    uint64_t n = (read_sys(pmcr_el0) >> 11) & 0x1f;
    if (n < 2)
        return 0;
    write_sys(pmevtyper0_el0, 0x08);                /* INST_RETIRED */
    write_sys(pmevtyper1_el0, 0x03);                /* L1D_CACHE_REFILL */
    write_sys(pmccfiltr_el0, 0);                    /* cycles at EL1 too */
    write_sys(pmcntenset_el0, (1ull << 31) | 3);
    write_sys(pmcr_el0, read_sys(pmcr_el0) | 1 | 2 | 4);   /* on, events and cycles reset */
    __asm__ volatile("isb");
    return 1;
}

static void pmu_stop(void)
{
    write_sys(pmcntenclr_el0, (1ull << 31) | 3);
}

static void count(b3d_count_t *c)
{
    c->cycles = (uint32_t)read_sys(pmccntr_el0);
    c->instr = (uint32_t)read_sys(pmevcntr0_el0);
    c->dmiss = (uint32_t)read_sys(pmevcntr1_el0);
    c->wait_instr = 0;
}

static int key(void)
{
    const uint32_t p = pad_pressed();
    if (p & (pad_back | PAD_SELECT))
        return B3D_KEY_BACK;
    if (p & (PAD_LEFT | PAD_UP | PAD_L1))
        return B3D_KEY_LEFT;
    if (p & (PAD_RIGHT | PAD_DOWN | PAD_R1 | pad_ok | PAD_START))
        return B3D_KEY_RIGHT;
    return B3D_KEY_NONE;
}

static void log_line(const char *s)
{
    kprintf("%s\n", s);
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
        if (n[0] == '3' && (n[1] == 'D' || n[1] == 'd') && n[2] >= '0' && n[2] <= '9') {
            const int k = atoi(n + 2);
            if (k > best)
                best = k;
        }
    }
    return best;
}

static int save(const char *text, size_t len, char *name, size_t n)
{
    if (fat_mkdirs(DIR) != 0)
        return -1;
    char file[16];
    ksnprintf(file, sizeof file, "3D%04d.TXT", last_report() + 1);
    if (fat_write_file(DIR, file, text, len) != 0)
        return -1;
    ksnprintf(name, n, "bm/bench/%s", file);
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

void rgb30_bench3d(framebuffer_t *fb)
{
    const uint32_t con_w = fb->width, con_h = fb->height;
    static char machine[96];
    ksnprintf(machine, sizeof machine, "%s, Cortex-A55, the 3D on the ARM (no Mali driver yet)",
              PLAT_NAME);
    fbp = fb;
    if (bm_video_enter(fb, 640, 360, &page) != 0) {
        kprintf("3D Bench: cannot set the video mode\n");
        return;
    }
    const int counting = pmu_start();
    while (pad_state())                             /* the button that opened it */
        timer_delay_ms(10);
    pad_pressed();
    b3d_platform_t p = {
        .g = &page, .us = us, .present = present, .count = count, .counting = counting,
        .pmu = "Cortex-A55 PMU", .key = key, .back = pad_back_name(), .log = log_line,
        .save = save, .load_last = load_last, .kernel = bm_version, .machine = machine,
        .date = net_time() ? net_time_text() : "",
    };
    const int err = b3d_run(&p);
    if (counting)
        pmu_stop();
    bm_video_leave(fb, con_w, con_h);
    kprintf("3D Bench: done (%s); the report is in bm/bench on the SD card%s\n", machine,
            err ? " (could not be saved)" : "");
}
