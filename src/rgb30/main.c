/*
 * bm on the PowKiddy RGB30 (RK3566): kernel entry. Also runs on QEMU's
 * virt machine, where the tests drive it through the serial port.
 *
 * LEDs while booting (the screen may not be up yet):
 *   red on                 -> init in progress (stuck = early hang)
 *   green blinking at 1 Hz -> running (the 1 kHz timer interrupt works)
 *   red blinking N times   -> fatal exception N (9 = panic)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "a64.h"
#include "plat.h"
#include "arch/mmu.h"
#include "drivers/fb.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "gfx/console.h"
#include "gfx/font.h"
#include "kernel/crumbs.h"
#include "kernel/exceptions.h"
#include "kernel/irq.h"
#include "kernel/tick.h"
#include "kernel/version.h"
#include "lib/heap.h"
#include "lib/printf.h"
#include "script/luavm.h"
#include "drivers/sd.h"
#include "fs/fat.h"
#include "kernel/config.h"
#include "kernel/fiber.h"
#include "wifi/wifi.h"
#include "net/net.h"
#include "net/wifi_auto.h"
#include "bt/bt.h"
#include "ui.h"
#include "battery.h"
#include "smp.h"
#include "b3d_rgb30.h"
#include "net/release.h"
#include "mbedtls/sha256.h"
#include "pad.h"
#include "kernel/ledstate.h"
#include "kernel/splash.h"
#include "kernel/notice.h"
#include "kernel/reports.h"
#include "kernel/sdlog.h"
#include "bm/runtime.h"
#include "bm/r3d.h"
#include "kernel/offload.h"
#include "kernel/jobs.h"
#include "audio/audio.h"
#include "rk_display.h"
#include "mali_hw.h"
#include "mali.h"

#include "lua.h"
#include "lauxlib.h"

/* The menu and the console: 360x360, every pixel a 2x2 square on the 720x720 panel */
#define SCREEN_W 360
#define SCREEN_H 360
#define TICK_HZ  1000

uintptr_t a64_dtb;
static framebuffer_t fb;

static void heartbeat(uint32_t tick)
{
    fiber_tick();                               /* fiber_over: a fiber past its slice (fiber.h) */
    ledstate_tick(tick * (1000 / TICK_HZ));     /* green on, or a slow blink (ledstate.h) */
    if (tick % (TICK_HZ / 10) == 0)
        crumb_tick(tick * (1000 / TICK_HZ));
}

extern char _start[];

/* the other cores: 4 in QEMU; on the console the key cores=2, 3 or 4 in bm/config.txt gives that many cores, absent gives one (docs/progress/multicore.md, V5) */
#ifdef PLAT_VIRT
/* M5: release_check_file hashes a big buffer on a worker; a changed byte is still refused */
static void smp_selftest_offload(void)
{
    enum { N = 256 << 10 };
    uint8_t *buf = malloc(N);
    if (!buf)
        return;
    for (int i = 0; i < N; i++)
        buf[i] = (uint8_t)(i * 31 + (i >> 8));
    release_file_t f = { .size = N };
    mbedtls_sha256(buf, N, f.sha256, 0);
    int before = smp_offload_count();
    int good = release_check_file(&f, buf, N);
    buf[N / 2] ^= 1;
    int bad = release_check_file(&f, buf, N);
    kprintf("smp: offload sha256 %s (%d jobs on workers)%s", good == 0 && bad == -1 ? "ok" : "FAILED",
            smp_offload_count() - before, "\n");
    free(buf);
}
#ifdef PLAT_VIRT
/* V1: the same scene drawn by one core and by bands on the workers must give the same pixels */
static void smp_selftest_raster(void)
{
    enum { W = 160, H = 120 };
    uint16_t *px[2] = { malloc(W * H * 2), malloc(W * H * 2) };
    uint32_t drawn[2] = { 0, 0 };
    if (!px[0] || !px[1]) {
        free(px[0]);
        free(px[1]);
        return;
    }
    r3d_mesh_t m, mt;
    g16_sheet_t sheet;
    if (r3d_mesh_sphere(&m, 10, 14, 0xFF8040, 0x40A0FF) != 0 || r3d_mesh_sphere(&mt, 10, 14, 0, 0) != 0 ||
        r3d_mesh_alloc_uv(&mt) != 0 || g16_sheet_alloc(&sheet, 32, 32) != 0) {
        free(px[0]);
        free(px[1]);
        return;
    }
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            g16_sheet_set(&sheet, x, y, (uint16_t)(x * 2000 + y * 31 + ((x ^ y) & 4) * 900), 1);
    for (int f = 0; f < mt.nfaces; f++) {
        mt.colors[f] = R3D_TEXTURED;
        for (int i = 0; i < 3; i++) {
            mt.uv[f * 6 + i * 2] = (float)((mt.faces[f * 3 + i] * 5) % 32);
            mt.uv[f * 6 + i * 2 + 1] = (float)((mt.faces[f * 3 + i] * 3) % 32);
        }
    }
    mt.tex = &sheet;
    for (int pass = 0; pass < 2; pass++) {
        g16_t g = { .px = px[pass], .stride = W, .w = W, .h = H, .cx0 = 0, .cy0 = 0, .cx1 = W, .cy1 = H };
        memset(px[pass], 0, W * H * 2);
        r3d_bands_set(offload_workers, offload_par, pass);       /* pass 0: one core; pass 1: bands */
        r3d_t r;
        if (r3d_init(&r, &g) != 0)
            continue;
        r3d_camera(&r, 0, 0, -7, 0, 0, 60);
        for (int i = 0; i < 5; i++) {
            const v3_t p = { -2.4f + 1.2f * (float)i, 0.8f * (float)((i * 5) % 3 - 1), (float)(i % 2) };
            r3d_draw_flags(&r, i == 2 ? &mt : &m, p, 0.3f * (float)i, 0.5f, 0, 1.2f, i & 1 ? R3D_SMOOTH : 0);
        }
        r3d_flush(&r);
        drawn[pass] = r.pixels;
        r3d_free(&r);
    }
    r3d_bands_set(offload_workers, offload_par, 1);
    kprintf("smp: raster bands %s (%lu pixels by one core, %lu by bands)\n",
            memcmp(px[0], px[1], W * H * 2) == 0 && drawn[0] == drawn[1] && drawn[0] > 0 ? "ok" : "FAILED",
            (unsigned long)drawn[0], (unsigned long)drawn[1]);
    r3d_mesh_free(&m);
    r3d_mesh_free(&mt);
    g16_sheet_free(&sheet);
    free(px[0]);
    free(px[1]);
}
#endif
#endif

static void smp_boot(void)
{
#ifdef PLAT_VIRT
    const int cores = 4;
#else
    const char *c = config_get("cores");
    const int cores = c && c[0] >= '2' && c[0] <= '4' && !c[1] ? c[0] - '0' : 1;
#endif
    if (cores < 2)
        return;
    if (smp_start(cores) > 0 && smp_selftest() == 0) {
        const char *rb = config_get("r3d_bands");
        r3d_bands_set(offload_workers, offload_par, !(rb && rb[0] == '0'));    /* V1: the ARM raster by bands */
        const char *mp = config_get("mali_par");
        mali_par_set(offload_workers, (mp && mp[0] == '0') ? NULL : offload_par);    /* the Mali draw build on the cores */
        jobs_selftest();
#ifdef PLAT_VIRT
        smp_selftest_offload();
        smp_selftest_raster();
        smp_selftest_faults();              /* QEMU only: it loses two cores on purpose */
#endif
    }
}

static void print_cpu(void)
{
    uint64_t midr = read_sysreg(midr_el1);
    uint32_t part = (uint32_t)(midr >> 4) & 0xfff;
    kprintf("CPU: %s r%lup%lu, EL%lu, counter %lu Hz\n",
            part == 0xd05 ? "Cortex-A55" : "ARMv8 core",
            (uint32_t)(midr >> 20) & 0xf, (uint32_t)midr & 0xf,
            (uint32_t)(read_sysreg(CurrentEL) >> 2) & 3,
            (uint32_t)read_sysreg(cntfrq_el0));
    kprintf("RAM: kernel at %08lx, heap %lu MB, framebuffers at %08lx; device tree at %08lx\n",
            (uint32_t)(uintptr_t)_start,
            (uint32_t)((heap_end() - heap_start()) >> 20), (uint32_t)PLAT_FB_START,
            (uint32_t)a64_dtb);
}

static void lua_selftest(void)
{
    if (!luavm_init()) {
        kprintf("\x1b[91mLua: cannot create the state\x1b[0m\n");
        return;
    }
    static const char code[] =
        "print(('Lua: %s, 2^10 = %s'):format(_VERSION, 2^10))\n"
        "local function fib(n) if n < 2 then return n end return fib(n-1) + fib(n-2) end\n"
        "local t = bm.micros(); local r = fib(25)\n"
        "print(('Lua: fib(25) = %d in %d ms'):format(r, (bm.micros() - t) // 1000))\n";
    luavm_run(code, sizeof code - 1, "=selftest");
}

/* Lua lines from the serial port, until exit() or Ctrl-D */
static int repl_done;

static int l_exit(lua_State *L)
{
    (void)L;
    repl_done = 1;
    return 0;
}

static int l_crash(lua_State *L)
{
    (void)L;
    __builtin_trap();
}

void ui_serial_repl(void)
{
    char line[256];
    size_t n = 0;
    lua_State *L = luavm_state();
    if (L) {
        lua_pushcfunction(L, l_exit);
        lua_setglobal(L, "exit");
        lua_pushcfunction(L, l_crash);      /* a test of the crash report: a BRK */
        lua_setglobal(L, "crash");
    }
    repl_done = 0;
    kprintf("Lua %s - exit() or Ctrl-D: back to the menu\n> ", LUA_RELEASE + 4);
    while (!repl_done) {
        char c = uart_getc();
        if (c == 4)
            break;
        if (c == '\r' || c == '\n') {
            kprintf("\n");
            if (n) {
                luavm_run(line, n, "=serial");
                n = 0;
            }
            if (!repl_done)
                kprintf("> ");
        } else if ((c == 8 || c == 127) && n) {
            n--;
            kprintf("\b \b");
        } else if (c >= 32 && n < sizeof line - 1) {
            line[n++] = c;
            klog_putc(c);
        }
    }
    kprintf("back to the menu\n");
}

static void sd_boot(void)
{
    ledstate_set(LED_NO_SD, 1);         /* until it is read */
    if (sd_init() != 0) {
        kprintf("SD: %s\n", sd_error());
        return;
    }
    if (fat_mount() != 0) {
        kprintf("SD: %lu MB (%s), %s\n", sd_blocks() / 2048, sd_controller(), fat_error());
        return;
    }
    ledstate_set(LED_NO_SD, 0);
    fat_set_clock(net_time);            /* SNTP time for the file dates, when the net has it */
    kprintf("SD: %s (%s)\n", fat_describe(), sd_controller());
    fat_write_tick = fiber_slice_tick;  /* a fiber's long write gives the CPU back (fat.h) */
    config_load();
    pad_config();                       /* confirm= */
    sdlog_prev();
}

static int booting = 1;                 /* until "ready" */

void rgb30_display_stage(const char *what)
{
    if (!booting)
        return;
    kprintf("display: %s\n", what);
    sdlog_boot(0);
}

/* Before a restart or a power off (plat_reset, plat_poweroff, before the
 * hardware goes quiet): this run's log as bm/lastrun.txt. Not from an
 * interrupt, nor while a write is half way through (fat_writing: a kernel
 * from the PC, a fiber's write). */
void rgb30_save_lastrun(void)
{
    uint64_t daif;
    __asm__ volatile("mrs %0, daif" : "=r"(daif));
    if ((daif & (1u << 7)) || fat_writing() || !sd_blocks())
        return;
    sdlog_lastrun();
}

/* The saved WiFi network is joined at boot, as on the Pi (wifi_boot=0 in
 * bm/config.txt turns it off, 2026-10-04: it used to wait for wifi_boot=1);
 * the address comes later, in the background. Two tries; if it fails the
 * menu tries again by itself (net/wifi_auto.c, 2026-10-10). */
static void wifi_boot(void)
{
    wifi_auto_boot();
}

void kernel_main(uintptr_t dtb)
{
    mmu_init(0, 0);             /* first: library code needs normal memory */
    a64_dtb = dtb;
    uart_init();
    plat_led(0, 1);
    heap_init(PLAT_HEAP_END);

    kprintf("\n\x1b[1;36mbm\x1b[0m kernel %s - %s (AArch64)\n", bm_version, PLAT_NAME);
    print_cpu();
    /* the SD card before the screen: if the display hangs, bm/bootlog.txt
     * says so (and the LEDs say where, docs/RGB30.md) */
    sd_boot();
    smp_boot();
    kprintf("display: starting\n");
    sdlog_boot(0);

    int err = fb_init(&fb, SCREEN_W, SCREEN_H, 2);
    if (err == 0) {
        exceptions_set_panic_fb(&fb);
        console_init(&fb, &font_console_8x16);
        char title[40];
        ksnprintf(title, sizeof title, "bm %s", bm_version);
        console_set_status(title, PLAT_NAME);
        splash_show(&fb, title);                /* the logo; the console keeps the boot's lines */
        for (const char *s = klog_text(); *s; s++)
            console_putc(*s);                   /* what came before, in the console too */
        kprintf_set_sink(console_putc);
    }
    kprintf("display: %s%s\n", plat_display_info(), err ? " - not available" : "");
    sdlog_boot(0);

    irq_init();
    tick_init(TICK_HZ);
    tick_set_hook(heartbeat);
    irq_cpu_enable();
    crumbs_boot();

    uint32_t t0 = timer_ticks(), n0 = tick_count();
    timer_delay_us(200000);
    kprintf("IRQ on: timer %lu Hz (measured %lu Hz)\n", tick_hz(),
            (uint32_t)((uint64_t)(tick_count() - n0) * 1000000u / (timer_ticks() - t0)));
    plat_led(-1, 0);
    /* the sound: the RK817's codec on the console, QEMU's sink in the tests */
    if (audio_init() == 0) {
        kprintf("audio: %s\n", audio_status());
        audio_note(0, 523, 90, 1, 110);         /* short chime, as on the Pi: sound works */
        audio_note(1, 784, 160, 1, 90);
    } else {
        kprintf("audio: off - %s\n", audio_status());
    }
    lua_selftest();
    if (plat_display_problem()) {
        plat_led(-1, 1);                    /* red stays on: see bm/bootlog.txt */
        ledstate_set(LED_DISPLAY, 1);       /* and the green blinks slowly */
    }
    {
        /* the boot before ended in a crash: a report, with the lines printed
         * before it; a "freeze" record is not one here: no watchdog yet */
        const char *kind;
        size_t n;
        const char *t = crumbs_last(&kind, &n);
        if (t && !strcmp(kind, "crash"))
            reports_text(kind, t, n);
    }
    kprintf("ready\n");
    sdlog_boot(1);
    booting = 0;
    if (config_get("mali_probe") && !strcmp(config_get("mali_probe"), "1")) {
        reports_begin("gpu");
        rgb30_mali_probe_triangle(&fb);
        reports_end();
    }
    wifi_boot();
    /* bench3d_boot=spheres.draws (set through the bridge: config bench3d_boot=...): one run of those tests at this
     * boot, the key removed first so a freeze does not loop; the bridge allows no comma in a value, '.' and '-' stand for it */
    const char *bb = config_get("bench3d_boot");
    if (bb && bb[0] && err == 0) {
        static char tests[96];
        size_t n = 0;
        for (; bb[n] && n < sizeof tests - 1; n++)
            tests[n] = (bb[n] == '.' || bb[n] == '-') ? ',' : bb[n];
        tests[n] = 0;
        config_unset("bench3d_boot");
        config_save();
        kprintf("bench3d_boot: %s\n", tests);
        fb_show(&fb, 0);
        console_suspend(0);
        /* dev mode (dev=1): the bench closes by itself at its end (summary 3 s) and the report goes at once */
        rgb30_bench3d_part(&fb, strcmp(tests, "all") ? tests : NULL, dev_mode());
        console_suspend(1);
        /* the report goes now, not at the menu's next idle: wait for the network (up to 90 s), then send what waits */
        for (int t = 0; t < 900 && reports_pending() > 0; t++) {
            net_poll();
            if (reports_auto_due()) {
                reports_send_pending();
                kprintf("report %s\n", reports_last());
                break;
            }
            timer_delay_ms(100);
        }
    }
    if (bt_paired() && bt_start() != 0)     /* a paired pad, keyboard or mouse comes back by itself, as on the Pi */
        kprintf("bt: not started: a paired pad will not reconnect (Settings > Controllers starts it)\n");
    bm_set_notice(notice_now);              /* the games show a kernel arriving */
    bm_set_battery(battery_state);          /* battery(), and its icon over them when low */
    bm_permissions(1);                      /* a game's network and reports: the player says */
    ledstate_set(LED_BOOT, 0);              /* started: green stays on if all is well */
    if (err == 0)
        ui_home(&fb);
    ui_serial_repl();               /* no screen: the serial port only */
    for (;;)
        ui_serial_repl();
}
