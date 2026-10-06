/*
 * Settings (2026-10-04): one tree for the Pi and the RGB30, the same
 * sections in the same order, each system filling them with what it has
 * (#ifdef BM_RGB30): Controllers, WiFi and network, Screen and sound,
 * Updates, Reports, System. The monitor's commands that set up or check
 * the console are rows here too (the USB scan, the connection test, the
 * test pattern, the audio test, the log). The menus draw the panels with
 * menu_ui: carts.c on the Pi, rgb30/ui.c on the RGB30 (home.h).
 */
#include "home.h"
#include "config.h"
#include "pointer.h"
#include "reports.h"
#include "update.h"
#include "version.h"
#include "bm/runtime.h"
#include "bt/bt.h"
#include "drivers/timer.h"
#include "fs/fat.h"
#include "gfx/console.h"
#include "lib/heap.h"
#include "lib/printf.h"
#include "net/http.h"
#include "net/net.h"
#include "usb/hid.h"
#include "wifi/wifi.h"
#include "audio/audio.h"
#ifdef BM_RGB30
#include "rgb30/pad.h"
#include "rgb30/plat.h"
#include "rgb30/ui.h"
#else
#include "crumbs.h"
#include "input.h"
#include "sysinfo.h"
#include "testpattern.h"
#include "drivers/board.h"
#include "drivers/prop.h"
#include "drivers/uart.h"
#include "drivers/watchdog.h"
#include "gpu/gpu3d.h"
#include "gpu/v3d.h"
#include "gpu/version3d.h"
#include "usb/smsc95xx.h"
#include "usb/usb.h"
#include <malloc.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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

/* a title line on the cleared console, before a row writes there */
static void heading(const char *what)
{
    console_clear();
    kprintf("\x1b[1;96mbm\x1b[0m \x1b[90m>\x1b[0m %s\n\n", what);
}

enum {
    R_CONTROLLERS = 1, R_WIFI, R_LAYOUT, R_SCREEN, R_DRAW, R_GPU3D, R_AA, R_VS, R_QUEUE, R_VOLUME, R_SYSTEM,
    R_PAD1, R_PAD2, R_PAD3, R_PAD4, R_KEYBOARD, R_MOUSE, R_PAIR, R_PAIR_KBD, R_PAIR_MOUSE, R_TEST,
    R_PROMPTS, R_FORGET,
    R_NETWORK, R_STATE, R_IP, R_TIME, R_CONSOLE, R_PASSWORD, R_CONNECT, R_BOOT,
    R_VERSION, R_BOARD, R_UPTIME, R_MEMORY, R_CLOCKS, R_SD, R_DRIVER3D, R_RESTART, R_MONITOR, R_PERF,
    R_UPDATE, R_INSTALL, R_REPORTS, R_REPORT_LOG,
    R_UPDATES, R_REPORTS_SUB, R_WAITING, R_USB, R_NETTEST, R_ETH, R_PATTERN, R_AUDIO, R_LOG, R_BT,
    R_CONFIRM, R_MODES, R_SOUND, R_BATTERY, R_POWEROFF,
};

static int popcount(unsigned v)
{
    int n = 0;
    for (; v; v &= v - 1)
        n++;
    return n;
}

int home_prompts_colour(void)
{
    const char *v = config_get("prompts");
    return v && strcmp(v, "colour") == 0;
}

/* Join the saved network at boot: unless wifi_boot=0 (the Pi and the
 * RGB30 alike since 2026-10-04) */
static int wifi_at_boot(void)
{
    const char *on = config_get("wifi_boot");
    return !(on && strcmp(on, "0") == 0);
}

static int wireless(void)
{
#ifdef BM_RGB30
    return 1;
#else
    return board()->wireless;
#endif
}

/* the dev kit's overlay: Settings > Screen and sound */
static const char *perf_name(void)
{
    static const char *const n[4] = { "Off", "Simple", "Detailed", "Functions" };
    return n[bm_perf()];
}

#ifndef BM_RGB30
/* what plays as the first player without a pad (the USB one) */
static const char *local_devices(void)
{
    int k = usb_info()->kind;
    if (k == USB_KEYBOARD) return "USB keyboard";
    if (k == USB_GAMEPAD || k == USB_XBOX360) return "USB gamepad";
    return "keyboard / USB";
}

/* the V3D draws the 3D of the games (M33) unless gpu3d=0 */
static int gpu3d_on(void)
{
    const char *on = config_get("gpu3d");
    return !(on && strcmp(on, "0") == 0);
}

/* gpu3d_aa=1: the GPU smooths the edges of the 3D (MSAA 4x) */
static int aa_on(void)
{
    const char *on = config_get("gpu3d_aa");
    return on && strcmp(on, "1") == 0;
}

static const char *aa_choice(void)
{
    if (!aa_on())
        return "Off";
    if (gpu3d_ready() && !gpu3d_msaa())
        return "4x: not on this GPU";
    return "4x (MSAA)";
}

/* gpu3d_vs=1: the GPU's vertex shader places the corners of the scenery
 * (models unlit or with baked light), 2: of every model (M36); the ARM
 * only sends them */
static int vs_on(void)
{
    const char *on = config_get("gpu3d_vs");
    return on && on[0] >= '1' && on[0] <= '2' ? on[0] - '0' : 0;
}

static const char *vs_choice(void)
{
    if (!vs_on())
        return "ARM";
    if (gpu3d_ready() && !gpu3d_vshader())
        return "GPU: not on this GPU";
    return vs_on() == 1 ? "GPU: scenery" : "GPU: all models";
}

/* gpu3d_queue=1 (M35): the end of a frame's 3D starts on the GPU and the
 * game's next _update runs meanwhile */
static int queue_on(void)
{
    const char *on = config_get("gpu3d_queue");
    return on && strcmp(on, "1") == 0;
}

static const char *queue_choice(void)
{
    if (!queue_on())
        return "Off";
    if (gpu3d_ready() && !gpu3d_queue_ok())
        return "On: not on this GPU";
    return "On";
}

static const char *gpu3d_choice(void)
{
    if (!gpu3d_on())
        return "ARM";
    if (v3d_init() != 0)
        return "ARM (no GPU)";          /* QEMU */
    return gpu3d_failed() ? "GPU: failed" : "GPU";
}
#endif

/* the network's state in a word or an address */
static const char *net_word(void)
{
    if (net_ip())
        return net_ip_text();
    return wifi_linked() || net_link_kind() != NET_LINK_NONE ? "connected" : "off";
}

static void reports_row(home_panel_t *p, int kind, int id, const char *label, const char *help)
{
    const int n = reports_pending();
    if (n)
        home_row(p, kind, id, label, help, "%d waiting", n);
    else
        home_row(p, kind, id, label, help, "none waiting");
}

void home_panel(int id, home_panel_t *p)
{
    memset(p, 0, sizeof *p);
    switch (id) {
    case HOME_SETTINGS: {
        ksnprintf(p->title, sizeof p->title, "Settings");
        int pads = popcount(bt_pads());
#ifdef BM_RGB30
        home_row(p, MENU_ROW_SUB, R_CONTROLLERS, "Controllers",
                 "Pads, keyboards, mice, the buttons",
                 pads ? "built in + %d" : "built in", pads);
#else
        home_row(p, MENU_ROW_SUB, R_CONTROLLERS, "Controllers",
                 "Pads, keyboards, mice, the buttons",
                 pads ? "%d connected" : "none connected", pads);
#endif
        home_row(p, MENU_ROW_SUB, R_WIFI, wireless() ? "WiFi and network" : "Network",
                 "The link, the address, a test", "%s", net_word());
#ifdef BM_RGB30
        home_row(p, MENU_ROW_SUB, R_SCREEN, "Screen and sound",
                 "Screen modes, performance overlay", "360x360 x2");
#else
        home_row(p, MENU_ROW_SUB, R_SCREEN, "Screen and sound",
                 "Drawing, 3D, volume, test pattern", "%s%s, vol %d",
                 gpu3d_choice(),
                 strcmp(gpu3d_choice(), "GPU") == 0 && vs_on() ? (vs_on() == 1 ? "+VS1" : "+VS") : "",
                 audio_volume());
#endif
        home_row(p, MENU_ROW_SUB, R_UPDATES, "Updates",
                 "The latest bm from GitHub, signed", "%s", update_ready() ? update_ready() : update_state());
        reports_row(p, MENU_ROW_SUB, R_REPORTS_SUB, "Reports", "Waiting on the SD card, to GitHub");
        home_row(p, MENU_ROW_SUB, R_SYSTEM, "System",
                 "Version, memory, the log", "%s", bm_version);
        /* the last two, on every system (the user's request, 2026-10-04) */
        home_row(p, MENU_ROW_ACTION, R_RESTART, "Restart", "Restarts the console", NULL);
#ifdef BM_RGB30
        home_row(p, MENU_ROW_ACTION, R_POWEROFF, "Shut down", "Turns the console off", NULL);
#else
        home_row(p, MENU_ROW_ACTION, R_POWEROFF, "Shut down",
                 "Stops the Pi: it starts again when its power is plugged back", NULL);
#endif
        break;
    }
    case HOME_CONTROLLERS: {
        ksnprintf(p->title, sizeof p->title, "Settings > Controllers");
#ifdef BM_RGB30
        home_row(p, MENU_ROW_INFO, R_PAD1, "Player 1",
                 "The console's buttons and sticks, with a Bluetooth pad if one is on",
                 bt_pads() ? "built in + Bluetooth" : "built in");
        home_row(p, MENU_ROW_INFO, R_BT, "Bluetooth", "Starts when a row below needs it",
                 "%s", bt_started() ? "on" : "off");
#else
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
#endif
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
                         "USB or Bluetooth: only a mouse moves the pointer",
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
#ifdef BM_RGB30
                 "Every button and both sticks, live (Start + Select: back)", NULL);
        home_row(p, MENU_ROW_CHOICE, R_CONFIRM, "Confirm button",
                 "The button that says yes; the other goes back (confirm= in bm/config.txt)",
                 "%s", pad_ok == PAD_A ? "A (B back)" : "B (A back)");
#else
                 "The buttons each player holds, for 10 s", NULL);
#endif
        home_row(p, MENU_ROW_CHOICE, R_LAYOUT, "Keyboard layout",
                 "Layout of the USB or Bluetooth keyboard", "%s",
                 hid_layout()[0] == 'i' ? "Italian" : "US");
        home_row(p, MENU_ROW_CHOICE, R_PROMPTS, "Button icons",
                 "DS4 buttons in the hints: white or in colour", "%s",
                 home_prompts_colour() ? "Colour" : "White");
#ifndef BM_RGB30
        home_row(p, MENU_ROW_ACTION, R_USB, "Scan the USB again",
                 "The port and the hub: keyboards, pads, mice, Ethernet", NULL);
#endif
        home_row(p, MENU_ROW_ACTION, R_FORGET, "Forget all controllers",
                 "Removes every pairing; pair them again after", NULL);
        break;
    }
    case HOME_WIFI: {
        /* the Pi Zero W and the RGB30 have WiFi, the Pi 1 B / B+ an Ethernet port */
        int wl = wireless();
        ksnprintf(p->title, sizeof p->title, "Settings > %s", wl ? "WiFi and network" : "Network");
        const char *ssid = config_get("wifi_ssid"), *pw = config_get("net_password");
        if (wl) {
            home_row(p, MENU_ROW_INFO, R_NETWORK, "Network", "The saved network (bm/config.txt)",
                     "%s", ssid && ssid[0] ? ssid : "none saved");
            home_row(p, MENU_ROW_INFO, R_STATE, "State", "The link to the access point",
                     "%s", wifi_linked() || net_link_kind() == NET_LINK_WIFI ? "connected" : "not connected");
        }
#ifndef BM_RGB30
        else {
            home_row(p, MENU_ROW_INFO, R_STATE, "Ethernet", "The cable to the router",
                     "%s", net_link_kind() == NET_LINK_ETHERNET ? "connected" :
                     board()->ethernet ? "no cable" : "none on this board");
        }
#endif
        home_row(p, MENU_ROW_INFO, R_IP, "Address", "From the router (DHCP)", "%s", net_ip_text());
        home_row(p, MENU_ROW_INFO, R_TIME, "Time", "From the network (SNTP)", "%s", net_time_text());
        home_row(p, MENU_ROW_INFO, R_CONSOLE, "Network console",
                 "From the PC: tools/bm_net.py ADDRESS", "port 3333");
        home_row(p, MENU_ROW_INFO, R_PASSWORD, "Console password",
                 "net_password in bm/config.txt", "%s", pw && pw[0] ? pw : "made when the network starts");
        if (wl) {
            home_row(p, MENU_ROW_ACTION, R_CONNECT, "Connect to a network",
#ifdef BM_RGB30
                     "Lists the networks, joins wifi_ssid of bm/config.txt", NULL);
#else
                     "Lists the networks; the USB keyboard types the password", NULL);
#endif
            home_row(p, MENU_ROW_CHOICE, R_BOOT, "Connect at boot",
                     "Join the saved network when the console starts", "%s",
                     wifi_at_boot() ? "On" : "Off");
        }
        home_row(p, MENU_ROW_ACTION, R_NETTEST, "Test the connection",
                 "Gets a web page over HTTPS: status, size, speed", NULL);
#ifndef BM_RGB30
        if (board()->ethernet)
            home_row(p, MENU_ROW_ACTION, R_ETH, "Ethernet details",
                     "The link, the counters and the chip's registers", NULL);
#endif
        break;
    }
    case HOME_GRAPHICS:
        ksnprintf(p->title, sizeof p->title, "Settings > Screen and sound");
#ifdef BM_RGB30
        home_row(p, MENU_ROW_ACTION, R_MODES, "Screen modes",
                 "The modes for games and the GPU, each with a test image", "%s", plat_display_info());
#else
        home_row(p, MENU_ROW_CHOICE, R_DRAW, "Game drawing (.bm)",
                 "Direct on screen, or via RAM (compare: Render bench)", "%s",
                 bm_via_ram() ? "Via RAM" : "Direct");
        home_row(p, MENU_ROW_CHOICE, R_GPU3D, "3D of the games",
                 "Drawn by the GPU (V3D), or by the ARM", "%s", gpu3d_choice());
        home_row(p, MENU_ROW_CHOICE, R_AA, "3D anti-aliasing",
                 "Smooth edges of the GPU's 3D (try Dev > GPU test)", "%s", aa_choice());
        home_row(p, MENU_ROW_CHOICE, R_VS, "3D vertices",
                 "Who places the corners: ARM, or the GPU for the scenery or all", "%s", vs_choice());
        home_row(p, MENU_ROW_CHOICE, R_QUEUE, "3D frame queue",
                 "The game goes on while the GPU draws the frame before", "%s", queue_choice());
#endif
        home_row(p, MENU_ROW_CHOICE, R_PERF, "Performance overlay",
                 "fps, ms, Lua, costliest functions (F11 too)", "%s",
                 perf_name());
#ifdef BM_RGB30
        /* the speaker, or the headphones (the console switches by itself) */
        home_row(p, MENU_ROW_INFO, R_SOUND, "Sound", audio_status(), "%s", audio_ready() ? "on" : "off");
        home_row(p, MENU_ROW_CHOICE, R_VOLUME, "Volume",
                 "Sound of the games and tools (the + and - keys too)",
                 "%d / %d", audio_volume(), AUDIO_VOLUME_MAX);
        home_row(p, MENU_ROW_ACTION, R_AUDIO, "Test the sound",
                 "The sound's state and a test tune", NULL);
#else
        home_row(p, MENU_ROW_CHOICE, R_VOLUME, "Volume",
                 "Sound of the games and tools (games can change it in their pause menu)",
                 "%d / %d", audio_volume(), AUDIO_VOLUME_MAX);
        home_row(p, MENU_ROW_ACTION, R_PATTERN, "Test pattern",
                 "HDMI colour bars; any button returns", NULL);
        home_row(p, MENU_ROW_ACTION, R_AUDIO, "Test the sound",
                 "HDMI sound status and a test tune", NULL);
#endif
        break;
    case HOME_UPDATES:
        ksnprintf(p->title, sizeof p->title, "Settings > Updates");
        home_row(p, MENU_ROW_INFO, R_VERSION, "Version", "The kernel build (git describe)", "%s", bm_version);
        home_row(p, MENU_ROW_ACTION, R_UPDATE, "Check for updates",
                 "The latest release on GitHub, signed: what would change", "%s", update_state());
        if (update_ready())
            home_row(p, MENU_ROW_ACTION, R_INSTALL, "Install the update",
                     "Keeps the old kernels in /bm/backup, restarts", "%s", update_ready());
        break;
    case HOME_REPORTS:
        ksnprintf(p->title, sizeof p->title, "Settings > Reports");
        reports_row(p, MENU_ROW_INFO, R_WAITING, "On the SD card", "In bm/reports, until they are sent");
        reports_row(p, MENU_ROW_ACTION, R_REPORTS, "Send the reports",
                    "The tests' reports on the SD card, to GitHub (github_token)");
        home_row(p, MENU_ROW_ACTION, R_REPORT_LOG, "Report the log",
                 "Everything printed since boot, as a report (Send the reports)", NULL);
        break;
    case HOME_SYSTEM: {
        ksnprintf(p->title, sizeof p->title, "Settings > System");
        uint32_t s = timer_ticks() / 1000000;
        home_row(p, MENU_ROW_INFO, R_VERSION, "Version", "The kernel build (git describe)", "%s", bm_version);
#ifdef BM_RGB30
        home_row(p, MENU_ROW_INFO, R_BOARD, "Board", "The console", "%s", PLAT_NAME);
#else
        home_row(p, MENU_ROW_INFO, R_BOARD, "Board", "From the firmware's revision code",
                 "Raspberry %s", board()->name);
#endif
        home_row(p, MENU_ROW_INFO, R_UPTIME, "Uptime", "Since the console was turned on",
                 "%02lu:%02lu:%02lu", s / 3600, s / 60 % 60, s % 60);
#ifdef BM_RGB30
        home_row(p, MENU_ROW_INFO, R_MEMORY, "Memory in use", "The heap of the kernel and games",
                 "%lu of %lu MiB", (uint32_t)((heap_brk() - heap_start()) >> 20),
                 (uint32_t)((heap_end() - heap_start()) >> 20));
        {
            uint64_t midr;
            __asm__ volatile("mrs %0, midr_el1" : "=r"(midr));
            home_row(p, MENU_ROW_INFO, R_CLOCKS, "CPU", "The processor", "%s",
                     ((midr >> 4) & 0xfff) == 0xd05 ? "Cortex-A55" : "ARMv8");
        }
#else
        {
            struct mallinfo mi = mallinfo();
            uint32_t temp[2] = { 0, 0 };
            prop_query(PROP_GET_TEMPERATURE, temp, 2);
            home_row(p, MENU_ROW_INFO, R_MEMORY, "Memory in use", "The heap of the kernel and games",
                     "%lu of %lu MiB", (uint32_t)mi.uordblks >> 20,
                     (uint32_t)((heap_end() - heap_start()) >> 20));
            home_row(p, MENU_ROW_INFO, R_CLOCKS, "CPU", "Processor, clock and chip temperature",
                     "%s, %lu MHz, %lu.%lu C", sysinfo_cpu(), prop_clock_rate(CLOCK_ARM) / 1000000,
                     temp[1] / 1000, temp[1] % 1000 / 100);
        }
#endif
        home_row(p, MENU_ROW_INFO, R_SD, "SD card", "The card the console started from",
                 "%s", fat_describe());
#ifdef BM_RGB30
        {
            int mv, charge;
            if (plat_battery(&mv, &charge) == 0)
                home_row(p, MENU_ROW_INFO, R_BATTERY, "Battery", "Its charge, its voltage, and the charger",
                         "%d%%, %d.%02d V%s", battery_percent(mv), mv / 1000, mv % 1000 / 10,
                         charge == 2 ? ", full" : charge == 1 ? ", charging" : "");
            else
                home_row(p, MENU_ROW_INFO, R_BATTERY, "Battery", "Its charge, its voltage, and the charger",
                         "unknown");
        }
#else
        {
            /* the drivers' version, and the one the 3D settings reproduce */
            const int gpu = gpu3d_on() && !gpu3d_failed() && v3d_init() == 0;
            home_row(p, MENU_ROW_INFO, R_DRIVER3D, "3D driver", "bm3d version (block); the games' 3D now",
                     "bm3d %s (%s), as %s", BM3D_VERSION, BM3D_BLOCK,
                     bm3d_mode_q(gpu, gpu ? vs_on() : 0, gpu && queue_on()));
        }
#endif
        home_row(p, MENU_ROW_ACTION, R_LOG, "Log since boot",
                 "Everything printed since the console started", NULL);
#ifndef BM_RGB30
        home_row(p, MENU_ROW_ACTION, R_MONITOR, "Open the monitor",
                 "The text console with every command", NULL);
#endif
        break;
    }
    }
}

int home_sub_panel(int row)
{
    switch (row) {
    case R_CONTROLLERS: return HOME_CONTROLLERS;
    case R_WIFI: return HOME_WIFI;
    case R_SCREEN: return HOME_GRAPHICS;
    case R_UPDATES: return HOME_UPDATES;
    case R_REPORTS_SUB: return HOME_REPORTS;
    case R_SYSTEM: return HOME_SYSTEM;
    }
    return 0;
}

/* ---------------------------------------------------------------- the rows that run */

#ifdef BM_RGB30
/* the RGB30 starts its Bluetooth when a row needs it (the Pi at boot) */
static int bt_up(void)
{
    if (bt_start() != 0) {
        kprintf("Bluetooth is off (see above)\n");
        return 0;
    }
    return 1;
}
#else
static int bt_up(void) { return 1; }
#endif

static void x_pair(framebuffer_t *fb)
{
    (void)fb;
    heading("Pair a new controller");
    kprintf("DS4: hold Share + PS until the light flashes quickly.\n\n");
    if (bt_up())
        bt_scan(8);
}

static void x_pair_kbd(framebuffer_t *fb)
{
    (void)fb;
    heading("Pair a keyboard");
    kprintf("MX Keys: hold an Easy-Switch key for 3 s, until its light blinks fast.\n"
            "Then type the code shown here on the keyboard, and Enter.\n\n");
    if (bt_up())
        bt_pair_keyboard(15);
}

static void x_pair_mouse(framebuffer_t *fb)
{
    (void)fb;
    heading("Pair a mouse");
    kprintf("Put the mouse in pairing mode (MX mice: hold the Easy-Switch button 3 s,\n"
            "until its light blinks fast). No code is needed.\n\n");
    if (bt_up())
        bt_pair_mouse(10);
}

static void x_test(framebuffer_t *fb)
{
    (void)fb;
#ifdef BM_RGB30
    ui_input_test();
#else
    heading("Test the buttons");
    input_live_test(10);
#endif
}

static void x_connect(framebuffer_t *fb)
{
    (void)fb;
    heading("Connect to a network (B cancels)");
#ifndef BM_RGB30
    input_pad_keys(INPUT_PAD_ESC);
#endif
    if (wifi_start() == 0 && wifi_scan() > 0 && wifi_connect() == 0 && net_start(&net_wifi) == 0)
        net_wait_ip(15000);
#ifndef BM_RGB30
    input_pad_keys(0);
#endif
}

/* the monitor's G without the typing: a page of the web, how it came */
static void x_nettest(framebuffer_t *fb)
{
    (void)fb;
    static const char url[] = "https://example.com/";
    heading("Test the connection");
    kprintf("address %s, getting %s\n", net_ip() ? net_ip_text() : "none yet", url);
    uint32_t t0 = timer_ticks();
    uint8_t *data = NULL;
    size_t len = 0;
    http_info_t info;
    int st = http_get_buffer(url, NULL, 1u << 20, &data, &len, &info);
    uint32_t ms = (timer_ticks() - t0) / 1000;
    if (st < 0)
        kprintf("\x1b[91mno: %s\x1b[0m\n", info.error);
    else
        kprintf("HTTP %d, %lu bytes in %lu ms (%lu KiB/s): the connection works\n", st,
                (unsigned long)len, (unsigned long)ms, ms ? (unsigned long)(len * 1000 / 1024 / ms) : 0ul);
    free(data);
}

static void x_audio(framebuffer_t *fb)
{
    (void)fb;
    heading("Test the sound");
    audio_test();
}

static void x_log(framebuffer_t *fb)
{
#ifdef BM_RGB30
    (void)fb;
    ui_show_log();
#else
    home_show_log(fb);
#endif
}

#ifndef BM_RGB30
static void x_usb(framebuffer_t *fb)
{
    (void)fb;
    heading("Scan the USB again");
    usb_init();
    usb_print();
}

static void x_eth(framebuffer_t *fb)
{
    (void)fb;
    heading("Ethernet details");
    eth_diag();
    if (eth_present())
        kprintf("net: IP %s, time %s\n", net_ip_text(), net_time_text());
}

static void x_pattern(framebuffer_t *fb)
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

#else
static void x_modes(framebuffer_t *fb)
{
    (void)fb;
    ui_screen_modes();
}
#endif

static void t_send_reports(framebuffer_t *fb)
{
    (void)fb;
    heading("Send the reports");
    int left = reports_send_pending();
    kprintf("%s; %d waiting on the SD card\n", reports_last(), left);
}

static void t_report_log(framebuffer_t *fb)
{
    (void)fb;
    heading("Report the log");
    const char *t = klog_text();
    reports_text("log", t, strlen(t));
}

static void x_update_check(framebuffer_t *fb)
{
    heading("Check for updates");
    update_check(fb);
}

/* a row that runs on the text console (own: it draws its own screen) */
static void text(home_do_t *d, void (*fn)(framebuffer_t *), int wait, int own)
{
    d->what = HOME_TEXT;
    d->text = fn;
    d->wait = wait;
    d->own = own;
}

static void ask(home_do_t *d, const char *q, const char *detail, const char *yes)
{
    d->what = HOME_ASK;
    ksnprintf(d->ask, sizeof d->ask, "%s", q);
    ksnprintf(d->ask_detail, sizeof d->ask_detail, "%s", detail);
    ksnprintf(d->ask_yes, sizeof d->ask_yes, "%s", yes);
}

void home_act(int id, int row, int how, home_do_t *d)
{
    memset(d, 0, sizeof *d);
    d->what = HOME_STAY;
    (void)id;
    switch (row) {
    case R_CONTROLLERS: case R_WIFI: case R_SYSTEM: case R_SCREEN: case R_UPDATES: case R_REPORTS_SUB:
        d->what = HOME_OPEN;
        d->panel = home_sub_panel(row);
        break;
    case R_LAYOUT:
        hid_set_layout(hid_layout()[0] == 'i' ? "us" : "it");
        config_save();
        ksnprintf(d->note, sizeof d->note, "keyboard layout: %s", hid_layout());
        break;
    case R_PERF:
        bm_set_perf((bm_perf() + 1) % 4);       /* simple, detailed, functions, off */
        config_save();
        ksnprintf(d->note, sizeof d->note, "performance overlay: %s", perf_name());
        break;
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
        if (how == 0) text(d, x_pair, 1, 0);
        break;
    case R_PAIR_KBD:
        if (how == 0) text(d, x_pair_kbd, 1, 0);
        break;
    case R_PAIR_MOUSE:
        if (how == 0) text(d, x_pair_mouse, 1, 0);
        break;
    case R_TEST:
#ifdef BM_RGB30
        if (how == 0) text(d, x_test, 0, 1);
#else
        if (how == 0) text(d, x_test, 1, 0);
#endif
        break;
    case R_CONNECT:
        if (how == 0) text(d, x_connect, 1, 0);
        break;
    case R_NETTEST:
        if (how == 0) text(d, x_nettest, 1, 0);
        break;
    case R_LOG:
        if (how == 0) text(d, x_log, 0, 1);
        break;
    case R_FORGET:
        if (how == HOME_YES) {
            int n = bt_forget_all();
            ksnprintf(d->note, sizeof d->note, "%d controller%s forgotten", n, n == 1 ? "" : "s");
        } else if (how == 0) {
            ask(d, "Forget all controllers?", "Pads, keyboard and mouse: pair them again.", "Forget");
        }
        break;
    case R_RESTART:
        if (how == HOME_YES) {
            kprintf("rebooting...\n");
#ifdef BM_RGB30
            plat_reset();
#else
            crumbs_clean_exit();
            uart_flush();
            watchdog_reboot();
#endif
        } else if (how == 0) {
            ask(d, "Restart the console?", "A game left suspended is closed.", "Restart");
        }
        break;
    case R_POWEROFF:
        if (how == HOME_YES) {
            kprintf("shutting down\n");
#ifdef BM_RGB30
            plat_poweroff();
#else
            crumbs_clean_exit();
            uart_flush();
            watchdog_halt();
#endif
        } else if (how == 0) {
#ifdef BM_RGB30
            ask(d, "Shut the console down?", "A game left suspended is closed.", "Shut down");
#else
            ask(d, "Shut the console down?", "To start it again, plug its power back in.", "Shut down");
#endif
        }
        break;
    case R_UPDATE:
        if (how == 0) text(d, x_update_check, 1, 0);
        break;
    case R_REPORTS:
        if (how == 0) text(d, t_send_reports, 1, 0);
        break;
    case R_REPORT_LOG:
        if (how == 0) text(d, t_report_log, 1, 0);
        break;
    case R_INSTALL:
        if (how == HOME_YES) {
            text(d, update_install, 1, 0);      /* the wait only if it fails: it restarts */
        } else if (how == 0 && update_ready()) {
            char q[64];
            ksnprintf(q, sizeof q, "Install bm %s?", update_ready());
            ask(d, q, "The console restarts when it is done.", "Install");
        }
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
    case R_AUDIO:
        if (how == 0) text(d, x_audio, 1, 0);
        break;
#ifdef BM_RGB30
    case R_CONFIRM:
        config_set("confirm", pad_ok == PAD_A ? "b" : "a");
        config_save();
        pad_config();
        ksnprintf(d->note, sizeof d->note, "%s confirms, %s goes back", pad_ok_name(), pad_back_name());
        break;
    case R_MODES:
        if (how == 0) text(d, x_modes, 0, 1);
        break;
#else
    case R_DRAW:
        bm_set_via_ram(!bm_via_ram());
        config_save();
        ksnprintf(d->note, sizeof d->note, ".bm games draw %s", bm_via_ram() ? "via RAM" : "directly");
        break;
    case R_GPU3D:
        config_set("gpu3d", gpu3d_on() ? "0" : "1");
        config_save();
        if (gpu3d_on() && gpu3d_failed())
            ksnprintf(d->note, sizeof d->note, "GPU: %s", gpu3d_status());
        else
            ksnprintf(d->note, sizeof d->note, "the 3D of the next game: %s", gpu3d_choice());
        break;
    case R_AA:
        config_set("gpu3d_aa", aa_on() ? "0" : "1");
        config_save();
        ksnprintf(d->note, sizeof d->note, "anti-aliasing of the next game: %s", aa_on() ? "4x" : "off");
        break;
    case R_VS: {
        static const char *const next[3] = { "1", "2", "0" };
        config_set("gpu3d_vs", next[vs_on()]);
        config_save();
        ksnprintf(d->note, sizeof d->note, "3D vertices of the next game: %s", vs_choice());
        break;
    }
    case R_QUEUE:
        config_set("gpu3d_queue", queue_on() ? "0" : "1");
        config_save();
        ksnprintf(d->note, sizeof d->note, "3D frame queue of the next game: %s", queue_choice());
        break;
    case R_USB:
        if (how == 0) text(d, x_usb, 1, 0);
        break;
    case R_ETH:
        if (how == 0) text(d, x_eth, 1, 0);
        break;
    case R_PATTERN:
        if (how == 0) text(d, x_pattern, 0, 0);
        break;
    case R_MONITOR:
        if (how == 0) d->what = HOME_MONITOR;
        break;
#endif
    }
}
