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
#include "wifi/wifi.h"
#include "net/net.h"
#include "ui.h"

#include "lua.h"
#include "lauxlib.h"

/* The menu and the console: 512x512, centred on the 720x720 panel */
#define SCREEN_W 512
#define SCREEN_H 512
#define TICK_HZ  1000

uintptr_t a64_dtb;
static framebuffer_t fb;

static void heartbeat(uint32_t tick)
{
    if (tick % (TICK_HZ / 2) == 0)
        plat_led((tick / (TICK_HZ / 2)) & 1, -1);
    if (tick % (TICK_HZ / 10) == 0)
        crumb_tick(tick * (1000 / TICK_HZ));
}

extern char _start[];

static void print_cpu(void)
{
    uint64_t midr = read_sysreg(midr_el1);
    uint32_t part = (uint32_t)(midr >> 4) & 0xfff;
    kprintf("CPU: %s r%lup%lu, EL%lu, counter %lu Hz\n",
            part == 0xd05 ? "Cortex-A55" : "ARMv8 core",
            (uint32_t)(midr >> 20) & 0xf, (uint32_t)midr & 0xf,
            (uint32_t)(read_sysreg(CurrentEL) >> 2) & 3,
            (uint32_t)read_sysreg(cntfrq_el0));
    kprintf("RAM: kernel at %08lx, heap %lu MiB, framebuffers at %08lx; device tree at %08lx\n",
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

void ui_serial_repl(void)
{
    char line[256];
    size_t n = 0;
    lua_State *L = luavm_state();
    if (L) {
        lua_pushcfunction(L, l_exit);
        lua_setglobal(L, "exit");
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
    if (sd_init() != 0) {
        kprintf("SD: %s\n", sd_error());
        return;
    }
    if (fat_mount() != 0) {
        kprintf("SD: %lu MiB (%s), %s\n", sd_blocks() / 2048, sd_controller(), fat_error());
        return;
    }
    kprintf("SD: %s (%s)\n", fat_describe(), sd_controller());
    config_load();
}

/* Everything printed so far goes to bm/bootlog.txt: if the screen stays
 * dark, the SD card read on a PC says how far the boot went. Written
 * before the display starts, after it, and at "ready". */
static void save_bootlog(int say)
{
    if (!sd_blocks())
        return;
    const char *log = klog_text();
    if (fat_mkdirs("/bm") == 0 && fat_write_file("/bm", "BOOTLOG.TXT", log, strlen(log)) == 0) {
        if (say)
            kprintf("boot log saved to bm/bootlog.txt\n");
    } else {
        kprintf("boot log: cannot write bm/bootlog.txt (%s)\n", fat_error());
    }
}

/* The saved WiFi network at boot: only with wifi_boot=1 in bm/config.txt
 * while the RGB30's WiFi is new (the menu's WiFi page joins by hand);
 * the address comes later, in the background. */
static void wifi_boot(void)
{
    const char *on = config_get("wifi_boot"), *ssid = config_get("wifi_ssid");
    if (!on || strcmp(on, "1") != 0 || !ssid || !ssid[0])
        return;
    kprintf("wifi: joining \"%s\" (wifi_boot=1 in bm/config.txt)\n", ssid);
    if (wifi_start() == 0 && wifi_connect_saved() == 0)
        net_start(&net_wifi);
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
    kprintf("display: starting\n");
    save_bootlog(0);

    int err = fb_init(&fb, SCREEN_W, SCREEN_H, 2);
    if (err == 0) {
        exceptions_set_panic_fb(&fb);
        console_init(&fb, &font_console_8x16);
        char title[40];
        ksnprintf(title, sizeof title, "bm %s", bm_version);
        console_set_status(title, PLAT_NAME);
        for (const char *s = klog_text(); *s; s++)
            console_putc(*s);                   /* what came before, on screen too */
        kprintf_set_sink(console_putc);
    }
    kprintf("display: %s%s\n", plat_display_info(), err ? " - not available" : "");
    save_bootlog(0);

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
    lua_selftest();
    if (plat_display_problem())
        plat_led(-1, 1);                    /* red stays on: see bm/bootlog.txt */
    kprintf("ready\n");
    save_bootlog(1);
    wifi_boot();
    if (err == 0)
        ui_home(&fb);
    ui_serial_repl();               /* no screen: the serial port only */
    for (;;)
        ui_serial_repl();
}
