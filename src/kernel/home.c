#include "home.h"
#include "carts.h"
#include "bench.h"
#include "config.h"
#include "crumbs.h"
#include "demo.h"
#include "dmatest.h"
#include "input.h"
#include "monitor.h"
#include "pager.h"
#include "pointer.h"
#include "sysinfo.h"
#include "testpattern.h"
#include "version.h"
#include "audio/audio.h"
#include "bm/runtime.h"
#include "bm/stress.h"
#include "bt/bt.h"
#include "drivers/prop.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "drivers/watchdog.h"
#include "fs/fat.h"
#include "gfx/console.h"
#include "lib/heap.h"
#include "lib/printf.h"
#include "drivers/board.h"
#include "net/net.h"
#include "script/repl.h"
#include "usb/hid.h"
#include "usb/usb.h"
#include "wifi/wifi.h"

#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void home_row(home_panel_t *p, int kind, int id, const char *label, const char *help,
              const char *fmt, ...)
{
    if (p->n >= HOME_ROWS_MAX)
        return;
    int i = p->n++;
    ksnprintf(p->label[i], sizeof p->label[i], "%s", label);
    p->value[i][0] = 0;
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(p->value[i], sizeof p->value[i], fmt, ap);
        va_end(ap);
    }
    p->rows[i] = (menu_row_t){ p->label[i], fmt ? p->value[i] : NULL, kind };
    p->ids[i] = id;
    p->help[i] = help;
}

void home_wait_back(void)
{
    kprintf("\n\x1b[93mA, Enter or Esc: back to the menu\x1b[0m\n");
    input_flush();
    uint32_t prev = hid_buttons();
    for (;;) {
        int quit = 0;
        if (input_key() >= 0)
            break;
        uint32_t b = input_buttons(&quit);
        if (quit || (b & ~prev & (HID_A | HID_B | HID_START)))
            break;
        prev = b;
    }
}

/* a title line on the cleared console, before a tool writes there */
static void heading(const char *what)
{
    console_clear();
    kprintf("\x1b[1;96mbm\x1b[0m \x1b[90m>\x1b[0m %s\n\n", what);
}

/* ---------------------------------------------------------------- tools */

static void t_lua(framebuffer_t *fb)
{
    (void)fb;
    heading("Lua (Esc or B, Ctrl-D or exit() returns to the menu)");
    input_pad_keys(INPUT_PAD_ESC);
    repl_run();
    input_pad_keys(0);
}

static void t_system(framebuffer_t *fb)
{
    (void)fb;
    heading("System");
    kprintf("bm %s\n", bm_version);
    sysinfo_print();
    kprintf("SD card        : %s\n", fat_describe());
    kprintf("audio          : %s\n", audio_status());
    kprintf("network        : %s, time %s\n", net_ip_text(), net_time_text());
}

static void t_log(framebuffer_t *fb)
{
    (void)fb;
    heading("Log: everything printed since boot (up/down, B quits)");
    const char *text = klog_text();
    uint32_t cols, rows, lines = 0;
    console_size(&cols, &rows);
    for (const char *p = text; *p; p++)
        lines += *p == '\n';
    input_pad_keys(INPUT_PAD_NAV);
    pager_show(text);
    input_pad_keys(0);
    if (lines + 3 < rows)                       /* it fit on one page: no pager */
        home_wait_back();
}

static void t_input(framebuffer_t *fb)
{
    (void)fb;
    heading("Input test");
    input_live_test(10);
}

static void t_audio(framebuffer_t *fb)
{
    (void)fb;
    heading("Audio test");
    audio_test();
}

static void t_cpu(framebuffer_t *fb)
{
    heading("CPU benchmark");
    bench_t b;
    bench_run(&b, fb, "now");
    bench_print(&b, 1);
}

static void t_render(framebuffer_t *fb)
{
    heading("Rendering benchmark, 640x360 RGB565");
    bm_bench_report(fb, 120);
}

static void t_stress(framebuffer_t *fb)
{
    heading("Stress test: sprites, triangles, 3D");
    extern const uint8_t bm_stress_cart[], bm_stress_cart_end[];
    bm_stress_run(fb);
    kprintf("Lua part (cartridge API):\n");
    bm_stats_t bs;
    bm_play(fb, bm_stress_cart, (size_t)(bm_stress_cart_end - bm_stress_cart), 600, &bs);
}

static void t_dma(framebuffer_t *fb)
{
    heading("DMA test");
    dma_test(fb);
}

static void t_demo(framebuffer_t *fb)
{
    heading("Animation demo (10 s)");
    demo_stats_t st;
    demo_run(fb, 10, &st);
    demo_print(&st);
}

static void t_pattern(framebuffer_t *fb)
{
    console_suspend(1);
    draw_test_pattern(fb);
    input_pad_keys(INPUT_PAD_NAV);
    input_flush();
    input_getc();
    input_pad_keys(0);
    console_suspend(0);
    heading("Test pattern");
    kprintf("HDMI test pattern shown\n");
}

/* the development assistant (M30) on its own: ask, see the code and the
 * sprites it would give an editor (also the monitor's I) */
void home_assistant(framebuffer_t *fb)
{
    extern const uint8_t bm_assistant_cart[], bm_assistant_cart_end[];
    bm_stats_t bs;
    bm_play(fb, bm_assistant_cart, (size_t)(bm_assistant_cart_end - bm_assistant_cart), 24u * 3600u, &bs);
}

static void t_diag(framebuffer_t *fb)
{
    (void)fb;
    heading("Boot diagnostics: benchmarks, the bm demo, Lua boot script");
    diagnostics_run();
}

typedef struct {
    const char *title, *about;
    int icon;
    uint32_t rgb;
    void (*run)(framebuffer_t *fb);     /* NULL: the monitor */
    int wait;
    g16_sheet_t cover;
} tool_t;

/* bm Code, the code editor */
static void t_code(framebuffer_t *fb)
{
    carts_code(fb, NULL);
}

static tool_t tools[] = {
    { "Code", "code editor: tabs, two pages, small font", MENU_ICON_CODE, 0x3A4A8A, t_code, 0, { 0 } },
    { "Assistant", "help with code and sprites; F6 in the tools", MENU_ICON_ASSIST, 0x2A6A9A, home_assistant, 0, { 0 } },
    { "Monitor", "the text console with every command (h: help)", MENU_ICON_TERMINAL, 0x2A3A4A, NULL, 0, { 0 } },
    { "Lua", "Lua 5.4 prompt (USB keyboard); Esc or Ctrl-D returns", MENU_ICON_LUA, 0x2A3A9A, t_lua, 0, { 0 } },
    { "System", "board, clocks, memory, SD card, network", MENU_ICON_CHIP, 0x2A7A5A, t_system, 1, { 0 } },
    { "Log", "everything printed since boot", MENU_ICON_LOG, 0x6A6A7A, t_log, 0, { 0 } },
    { "Input test", "the buttons each player holds, for 10 s", MENU_ICON_PAD, 0x8A3A8A, t_input, 1, { 0 } },
    { "Audio test", "HDMI sound status and a test tune", MENU_ICON_SOUND, 0xB05A2A, t_audio, 1, { 0 } },
    { "CPU bench", "CPU and memory benchmark", MENU_ICON_GAUGE, 0x3A5A8A, t_cpu, 1, { 0 } },
    { "Render bench", "drawing benchmark, 640x360 RGB565", MENU_ICON_TRIANGLES, 0x5A3AA0, t_render, 1, { 0 } },
    { "Stress test", "sprites, triangles and 3D, in C and in Lua", MENU_ICON_FLAME, 0xA03A3A, t_stress, 1, { 0 } },
    { "DMA test", "copies by the CPU against the DMA, step by step", MENU_ICON_ARROWS, 0x2A7A8A, t_dma, 1, { 0 } },
    { "Demo", "the 60 fps animation demo, 10 s", MENU_ICON_PLAY, 0x3A8A3A, t_demo, 1, { 0 } },
    { "Test pattern", "HDMI colour bars; any button returns", MENU_ICON_BARS, 0x404050, t_pattern, 0, { 0 } },
    { "Diagnostics", "the old boot sequence: benchmarks and demos", MENU_ICON_CHECK, 0x7A6A2A, t_diag, 1, { 0 } },
};

#define NTOOLS ((int)(sizeof tools / sizeof tools[0]))

void home_init(void)
{
    for (int i = 0; i < NTOOLS; i++)
        if (!tools[i].cover.px)
            menu_make_tool_cover(&tools[i].cover, tools[i].title, tools[i].icon, tools[i].rgb);
}

int home_tools(void) { return NTOOLS; }
const char *home_tool_title(int i) { return tools[i].title; }
const char *home_tool_about(int i) { return tools[i].about; }
const g16_sheet_t *home_tool_cover(int i) { return tools[i].cover.px ? &tools[i].cover : NULL; }

void home_tool_start(int i, home_do_t *d)
{
    memset(d, 0, sizeof *d);
    crumb("dev tool", tools[i].title);
    if (!tools[i].run) {
        d->what = HOME_MONITOR;
        return;
    }
    d->what = HOME_TEXT;
    d->text = tools[i].run;
    d->wait = tools[i].wait;
}

/* ---------------------------------------------------------------- settings */

enum {
    R_CONTROLLERS = 1, R_WIFI, R_LAYOUT, R_DRAW, R_VOLUME, R_SYSTEM,
    R_PAD1, R_PAD2, R_PAD3, R_PAD4, R_KEYBOARD, R_MOUSE, R_PAIR, R_PAIR_KBD, R_PAIR_MOUSE, R_TEST,
    R_PROMPTS, R_FORGET,
    R_NETWORK, R_STATE, R_IP, R_TIME, R_CONSOLE, R_PASSWORD, R_CONNECT, R_BOOT,
    R_VERSION, R_BOARD, R_UPTIME, R_MEMORY, R_CLOCKS, R_SD, R_RESTART, R_MONITOR,
};

static int popcount(unsigned v)
{
    int n = 0;
    for (; v; v &= v - 1)
        n++;
    return n;
}

/* what plays as the first player without a pad (the USB one) */
static const char *local_devices(void)
{
    int k = usb_info()->kind;
    if (k == USB_KEYBOARD) return "USB keyboard";
    if (k == USB_GAMEPAD || k == USB_XBOX360) return "USB gamepad";
    return "keyboard / USB";
}

int home_prompts_colour(void)
{
    const char *v = config_get("prompts");
    return v && strcmp(v, "colour") == 0;
}

static int wifi_at_boot(void)
{
    const char *on = config_get("wifi_boot");
    return !(on && strcmp(on, "0") == 0);
}

void home_panel(int id, home_panel_t *p)
{
    memset(p, 0, sizeof *p);
    switch (id) {
    case HOME_SETTINGS: {
        ksnprintf(p->title, sizeof p->title, "Settings");
        int pads = popcount(bt_pads());
        home_row(p, MENU_ROW_SUB, R_CONTROLLERS, "Controllers",
                 "Pair Bluetooth controllers, test the buttons",
                 pads ? "%d connected" : "none connected", pads);
        home_row(p, MENU_ROW_SUB, R_WIFI, board()->wireless ? "WiFi and network" : "Network",
                 "Link, address, network console", "%s",
                 net_ip() ? net_ip_text() : net_link_kind() != NET_LINK_NONE ? "connected" : "off");
        home_row(p, MENU_ROW_CHOICE, R_LAYOUT, "Keyboard layout",
                 "Layout of the USB keyboard", "%s",
                 hid_layout()[0] == 'i' ? "Italian" : "US");
        home_row(p, MENU_ROW_CHOICE, R_DRAW, "Game drawing (.bm)",
                 "Direct on screen, or via RAM (compare: Render bench)", "%s",
                 bm_via_ram() ? "Via RAM" : "Direct");
        home_row(p, MENU_ROW_CHOICE, R_VOLUME, "Volume",
                 "Sound of the games and tools (games can change it in their pause menu)",
                 "%d / %d", audio_volume(), AUDIO_VOLUME_MAX);
        home_row(p, MENU_ROW_SUB, R_SYSTEM, "System",
                 "Version, memory, SD card, restart", "%s", bm_version);
        break;
    }
    case HOME_CONTROLLERS: {
        ksnprintf(p->title, sizeof p->title, "Settings > Controllers");
        int local = input_local_player(), ble = input_ble_player();
        for (int s = 0; s < BT_PADS; s++) {
            char addr[18], label[16];
            int on = bt_pad_addr(s, addr);
            ksnprintf(label, sizeof label, "Player %d", s + 1);
            if (addr[0] && on)
                home_row(p, MENU_ROW_INFO, R_PAD1 + s, label, "A Bluetooth pad, connected",
                         "%s on", addr);
            else if (s == ble)
                home_row(p, MENU_ROW_INFO, R_PAD1 + s, label, "The Bluetooth keyboard plays here",
                         "Bluetooth keyboard");
            else if (s == local)
                home_row(p, MENU_ROW_INFO, R_PAD1 + s, label, "The USB keyboard or gamepad plays here",
                         "%s", local_devices());
            else if (addr[0])
                home_row(p, MENU_ROW_INFO, R_PAD1 + s, label, "Paired: press its PS button to connect",
                         "%s off", addr);
            else
                home_row(p, MENU_ROW_INFO, R_PAD1 + s, label, "Free: pair a controller", "-");
        }
        home_row(p, MENU_ROW_INFO, R_KEYBOARD, "Bluetooth keyboard",
                 bt_keyboard() ? "Connected: it types, and plays as its own player" :
                 bt_keyboard_paired() ? "Paired: press a key on it to connect" : "None paired",
                 "%s", bt_keyboard() ? "on" : bt_keyboard_paired() ? "off" : "-");
        {
            unsigned mice = pointer_devices();
            if (!pointer_enabled())
                home_row(p, MENU_ROW_INFO, R_MOUSE, "Mouse",
                         "mouse=off in bm/config.txt: no pointer anywhere", "off");
            else
                home_row(p, MENU_ROW_INFO, R_MOUSE, "Mouse",
                         mice ? "Connected: it moves the pointer in the menu" :
                         bt_mouse_paired() ? "Paired: move it or click to connect" :
                         "USB or Bluetooth; the right stick of a pad moves the pointer too",
                         "%s", mice == (POINTER_USB | POINTER_BLUETOOTH) ? "USB + Bluetooth" :
                         mice & POINTER_USB ? "USB" : mice ? "Bluetooth on" :
                         bt_mouse_paired() ? "Bluetooth off" : "-");
        }
        home_row(p, MENU_ROW_ACTION, R_PAIR, "Pair a new controller",
                 "DS4: hold Share + PS until the light flashes", NULL);
        home_row(p, MENU_ROW_ACTION, R_PAIR_KBD, "Pair a keyboard",
                 "Bluetooth LE (MX Keys: hold an Easy-Switch key 3 s)", NULL);
        home_row(p, MENU_ROW_ACTION, R_PAIR_MOUSE, "Pair a mouse",
                 "Bluetooth LE or classic: put the mouse in pairing mode first", NULL);
        home_row(p, MENU_ROW_ACTION, R_TEST, "Test the buttons",
                 "The buttons each player holds, for 10 s", NULL);
        home_row(p, MENU_ROW_CHOICE, R_PROMPTS, "Button icons",
                 "DS4 buttons in the hints: white or in colour", "%s",
                 home_prompts_colour() ? "Colour" : "White");
        home_row(p, MENU_ROW_ACTION, R_FORGET, "Forget all controllers",
                 "Removes every pairing; pair them again after", NULL);
        break;
    }
    case HOME_WIFI: {
        /* the Pi Zero W has WiFi, the Pi 1 B / B+ an Ethernet port */
        int wl = board()->wireless;
        ksnprintf(p->title, sizeof p->title, "Settings > %s", wl ? "WiFi and network" : "Network");
        const char *ssid = config_get("wifi_ssid"), *pw = config_get("net_password");
        if (wl) {
            home_row(p, MENU_ROW_INFO, R_NETWORK, "Network", "The saved network (bm/config.txt)",
                     "%s", ssid && ssid[0] ? ssid : "none saved");
            home_row(p, MENU_ROW_INFO, R_STATE, "State", "The link to the access point",
                     "%s", net_link_kind() == NET_LINK_WIFI ? "connected" : "not connected");
        } else {
            home_row(p, MENU_ROW_INFO, R_STATE, "Ethernet", "The cable to the router",
                     "%s", net_link_kind() == NET_LINK_ETHERNET ? "connected" :
                     board()->ethernet ? "no cable" : "none on this board");
        }
        home_row(p, MENU_ROW_INFO, R_IP, "Address", "From the router (DHCP)", "%s", net_ip_text());
        home_row(p, MENU_ROW_INFO, R_TIME, "Time", "From the network (SNTP)", "%s", net_time_text());
        home_row(p, MENU_ROW_INFO, R_CONSOLE, "Network console",
                 "From the PC: tools/bm_net.py ADDRESS", "port 3333");
        home_row(p, MENU_ROW_INFO, R_PASSWORD, "Console password",
                 "net_password in bm/config.txt", "%s", pw && pw[0] ? pw : "made when the network starts");
        if (wl) {
            home_row(p, MENU_ROW_ACTION, R_CONNECT, "Connect to a network",
                     "Lists the networks; the USB keyboard types the password", NULL);
            home_row(p, MENU_ROW_CHOICE, R_BOOT, "Connect at boot",
                     "Join the saved network when the console starts", "%s",
                     wifi_at_boot() ? "On" : "Off");
        }
        break;
    }
    case HOME_SYSTEM: {
        ksnprintf(p->title, sizeof p->title, "Settings > System");
        uint32_t s = timer_ticks() / 1000000;
        struct mallinfo mi = mallinfo();
        uint32_t temp[2] = { 0, 0 };
        prop_query(PROP_GET_TEMPERATURE, temp, 2);
        home_row(p, MENU_ROW_INFO, R_VERSION, "Version", "The kernel build (git describe)", "%s", bm_version);
        home_row(p, MENU_ROW_INFO, R_BOARD, "Board", "From the firmware's revision code",
                 "Raspberry %s", board()->name);
        home_row(p, MENU_ROW_INFO, R_UPTIME, "Uptime", "Since the console was turned on",
                 "%02lu:%02lu:%02lu", s / 3600, s / 60 % 60, s % 60);
        home_row(p, MENU_ROW_INFO, R_MEMORY, "Memory in use", "The heap of the kernel and games",
                 "%lu of %lu MiB", (uint32_t)mi.uordblks >> 20,
                 (uint32_t)((heap_end() - heap_start()) >> 20));
        home_row(p, MENU_ROW_INFO, R_CLOCKS, "CPU", "ARM clock and chip temperature",
                 "%lu MHz, %lu.%lu C", prop_clock_rate(CLOCK_ARM) / 1000000,
                 temp[1] / 1000, temp[1] % 1000 / 100);
        home_row(p, MENU_ROW_INFO, R_SD, "SD card", "The card the console started from",
                 "%s", fat_describe());
        home_row(p, MENU_ROW_ACTION, R_RESTART, "Restart", "Restarts the console", NULL);
        home_row(p, MENU_ROW_ACTION, R_MONITOR, "Open the monitor",
                 "The text console with every command", NULL);
        break;
    }
    }
}

/* the text-console parts of the settings */

static void x_pair(framebuffer_t *fb)
{
    (void)fb;
    heading("Pair a new controller");
    kprintf("DS4: hold Share + PS until the light flashes quickly.\n\n");
    bt_scan(8);
}

static void x_pair_kbd(framebuffer_t *fb)
{
    (void)fb;
    heading("Pair a keyboard");
    kprintf("MX Keys: hold an Easy-Switch key for 3 s, until its light blinks fast.\n"
            "Then type the code shown here on the keyboard, and Enter.\n\n");
    bt_pair_keyboard(15);
}

static void x_pair_mouse(framebuffer_t *fb)
{
    (void)fb;
    heading("Pair a mouse");
    kprintf("Put the mouse in pairing mode (MX mice: hold the Easy-Switch button 3 s,\n"
            "until its light blinks fast). No code is needed.\n\n");
    bt_pair_mouse(10);
}

static void x_test(framebuffer_t *fb)
{
    (void)fb;
    heading("Test the buttons");
    input_live_test(10);
}

static void x_connect(framebuffer_t *fb)
{
    (void)fb;
    heading("Connect to a network (B cancels)");
    input_pad_keys(INPUT_PAD_ESC);
    if (wifi_start() == 0 && wifi_scan() > 0 && wifi_connect() == 0 && net_start(&net_wifi) == 0)
        net_wait_ip(15000);
    input_pad_keys(0);
}

void home_act(int id, int row, int how, home_do_t *d)
{
    memset(d, 0, sizeof *d);
    d->what = HOME_STAY;
    (void)id;
    switch (row) {
    case R_CONTROLLERS: d->what = HOME_OPEN; d->panel = HOME_CONTROLLERS; break;
    case R_WIFI: d->what = HOME_OPEN; d->panel = HOME_WIFI; break;
    case R_SYSTEM: d->what = HOME_OPEN; d->panel = HOME_SYSTEM; break;
    case R_LAYOUT:
        hid_set_layout(hid_layout()[0] == 'i' ? "us" : "it");
        config_save();
        ksnprintf(d->note, sizeof d->note, "keyboard layout: %s", hid_layout());
        break;
    case R_DRAW:
        bm_set_via_ram(!bm_via_ram());
        config_save();
        ksnprintf(d->note, sizeof d->note, ".bm games draw %s", bm_via_ram() ? "via RAM" : "directly");
        break;
    case R_VOLUME: {
        int v = audio_volume() + (how ? how : 1);
        if (how == 0 && v > AUDIO_VOLUME_MAX)
            v = 0;                              /* A goes round */
        audio_set_volume(v);
        config_save();
        audio_note(0, 880, 70, 4, 140);         /* a beep at the new volume */
        ksnprintf(d->note, sizeof d->note, "volume: %d / %d", audio_volume(), AUDIO_VOLUME_MAX);
        break;
    }
    case R_PROMPTS:
        config_set("prompts", home_prompts_colour() ? "white" : "colour");
        config_save();
        ksnprintf(d->note, sizeof d->note, "button icons: %s", home_prompts_colour() ? "colour" : "white");
        break;
    case R_BOOT:
        config_set("wifi_boot", wifi_at_boot() ? "0" : "1");
        config_save();
        ksnprintf(d->note, sizeof d->note, "WiFi at boot: %s", wifi_at_boot() ? "on" : "off");
        break;
    case R_PAIR:
        if (how == 0) { d->what = HOME_TEXT; d->text = x_pair; d->wait = 1; }
        break;
    case R_PAIR_KBD:
        if (how == 0) { d->what = HOME_TEXT; d->text = x_pair_kbd; d->wait = 1; }
        break;
    case R_PAIR_MOUSE:
        if (how == 0) { d->what = HOME_TEXT; d->text = x_pair_mouse; d->wait = 1; }
        break;
    case R_TEST:
        if (how == 0) { d->what = HOME_TEXT; d->text = x_test; d->wait = 1; }
        break;
    case R_CONNECT:
        if (how == 0) { d->what = HOME_TEXT; d->text = x_connect; d->wait = 1; }
        break;
    case R_FORGET:
        if (how == HOME_YES) {
            int n = bt_forget_all();
            ksnprintf(d->note, sizeof d->note, "%d controller%s forgotten", n, n == 1 ? "" : "s");
        } else if (how == 0) {
            d->what = HOME_ASK;
            ksnprintf(d->ask, sizeof d->ask, "Forget all controllers?");
            ksnprintf(d->ask_detail, sizeof d->ask_detail, "Pads, keyboard and mouse: pair them again.");
            ksnprintf(d->ask_yes, sizeof d->ask_yes, "Forget");
        }
        break;
    case R_RESTART:
        if (how == HOME_YES) {
            kprintf("rebooting...\n");
            crumbs_clean_exit();
            uart_flush();
            watchdog_reboot();
        } else if (how == 0) {
            d->what = HOME_ASK;
            ksnprintf(d->ask, sizeof d->ask, "Restart the console?");
            ksnprintf(d->ask_detail, sizeof d->ask_detail, "A game left suspended is closed.");
            ksnprintf(d->ask_yes, sizeof d->ask_yes, "Restart");
        }
        break;
    case R_MONITOR:
        if (how == 0) d->what = HOME_MONITOR;
        break;
    }
}
