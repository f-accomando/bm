/*
 * Settings (2026-10-04): one tree for the Pi and the RGB30, the same
 * sections in the same order, each system filling them with what it has
 * (#ifdef BM_RGB30): Controllers, WiFi and network, Screen, Sound,
 * Updates, Reports, System. The monitor's commands that set up or check
 * the console are rows here too (the USB scan, the connection test, the
 * test pattern, the audio test, the log). The menus draw the panels with
 * menu_ui: carts.c on the Pi, rgb30/ui.c on the RGB30 (home.h).
 */
#include "home.h"
#include "notice.h"
#include "config.h"
#include "controls.h"
#include "pointer.h"
#include "reports.h"
#include "update.h"
#include "version.h"
#include "bm/runtime.h"
#include "bt/bt.h"
#include "drivers/timer.h"
#include "fs/fat.h"
#include "sdmap.h"
#include "gfx/console.h"
#include "lib/heap.h"
#include "lib/printf.h"
#include "net/http.h"
#include "net/net.h"
#include "usb/hid.h"
#include "wifi/wifi.h"
#include "net/wifi_auto.h"
#include "net/netxfer.h"
#include "audio/audio.h"
#include "audio/presets.h"
#ifdef BM_RGB30
#include "rgb30/pad.h"
#include "rgb30/plat.h"
#include "rgb30/ui.h"
#include "gpu/gpu3d.h"            /* gpu3d_status: Settings > System > 3D driver */

/* the RGB30 is no Pi 1: the rows and the CRT filter of the Pi 1 stay off */
static inline int board_is_pi1(void)
{
    return 0;
}
#else
#include "crumbs.h"
#include "input.h"
#include "sysinfo.h"
#include "testpattern.h"
#include "drivers/board.h"
#include "drivers/prop.h"
#include "drivers/spilcd.h"
#include "drivers/gpio.h"
#include "drivers/mmio.h"
#include "drivers/uart.h"
#include "drivers/watchdog.h"
#include "gpu/gpu3d.h"
#include "gpu/v3d.h"
#include "gpu/version3d.h"
#include "net/netcon.h"
#include "usb/smsc95xx.h"
#include "usb/usb.h"
#include <malloc.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- the SD map */

static const char *sdmap_state_name(void)
{
    switch (sdmap_state()) {
    case SDMAP_CACHED: return "cached";
    case SDMAP_SCANNING: return "scanning";
    case SDMAP_DONE: return "done";
    }
    return "";
}

static void fmt_size(char *out, size_t n, uint64_t bytes)
{
    if (bytes < (1u << 20))
        ksnprintf(out, n, "%lu KB", (unsigned long)(bytes >> 10));
    else if (bytes < (1u << 30))
        ksnprintf(out, n, "%lu.%lu MB", (unsigned long)(bytes >> 20),
                  (unsigned long)((bytes & 0xFFFFF) * 10 >> 20));
    else
        ksnprintf(out, n, "%lu.%02lu GB", (unsigned long)(bytes >> 30),
                  (unsigned long)((bytes & 0x3FFFFFFF) * 100 >> 30));
}

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
    p->rows[i] = (menu_row_t){ p->label[i], fmt ? p->value[i] : NULL, kind, NULL, 0 };
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
    R_CONTROLLERS = 1, R_WIFI, R_LAYOUT, R_SCREEN, R_GPU3D, R_VOLUME, R_SYSTEM,
    R_PAD1, R_PAD2, R_PAD3, R_PAD4, R_KEYBOARD, R_MOUSE, R_PAIR, R_PAIR_KBD, R_PAIR_MOUSE, R_TEST,
    R_PROMPTS, R_FORGET,
    R_NETWORK, R_STATE, R_IP, R_TIME, R_CONSOLE, R_PASSWORD, R_CONNECT, R_BOOT,
    R_VERSION, R_BOARD, R_UPTIME, R_MEMORY, R_CLOCKS, R_SD, R_DRIVER3D, R_RESTART, R_MONITOR,
    R_UPDATE, R_INSTALL, R_INSTALL_STAGED, R_REPORTS, R_REPORT_LOG,
    R_UPDATES, R_REPORTS_SUB, R_WAITING, R_USB, R_NETTEST, R_ETH, R_PATTERN, R_GPIOSCREEN, R_AUDIO, R_LOG, R_BT,
    R_MODES, R_SOUND, R_BATTERY, R_POWEROFF, R_FAST3D,
    R_STYLE, R_DEPTH, R_BATTERY_ICON, R_CHANGE, R_UTILS, R_MENUSIZE, R_SOUNDMENU,
    R_SDMAP, R_SDBAR, R_SDFILES, R_SDINFO,
    R_CONTROLS, R_CTL_SCROLLER, R_CTL_TOPDOWN, R_CTL_STICK, R_CTL_SYSTEM, R_CTL_KEYS, R_CTL_RESET,
    R_CRT, R_LCD_POWER, R_VIDEO_OUT, R_AUDIO_OUT,
    R_DEV, R_REC,
    R_CTL = 300,                /* + the index of the action in kernel/controls.c (R_UTIL_BASE uses 200..223) */
};
#define R_UTIL_BASE 200   /* the rows of HOME_UTILITIES and the 3D tests of HOME_GRAPHICS: R_UTIL_BASE + i */

/* No utility until a kernel defines its own (home.c on the Pi, ui.c on the RGB30). */
__attribute__((weak)) int home_utils(void) { return 0; }
__attribute__((weak)) const char *home_util_title(int i) { (void)i; return ""; }
__attribute__((weak)) const char *home_util_about(int i) { (void)i; return ""; }
__attribute__((weak)) int home_util_group(int i) { (void)i; return 0; }
__attribute__((weak)) void home_util_start(int i, home_do_t *d) { (void)i; (void)d; }

static int util_count(int group)
{
    int n = 0;
    for (int i = 0; i < home_utils(); i++)
        n += home_util_group(i) == group;
    return n;
}

/* sound_depth: the bits of each sample at the output (what the output
 * really carries is in the row's help) */
static const char *depth_choice(void)
{
    static char t[12];
    ksnprintf(t, sizeof t, "%u-bit", audio_depth());
    return t;
}

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
    return home_wireless();
}

/* the low battery over the games and tools (runtime.c battery_draw): on
 * unless battery_icon=0 */
static int battery_icon_on(void)
{
    const char *v = config_get("battery_icon");
    return !(v && strcmp(v, "0") == 0);
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

/* gpu3d_vs=1: the GPU's vertex shader places the corners of the scenery
 * (models unlit or with baked light), 2: of every model (bm3d 3.4; the default
 * from bm3d 6.6), 0: the ARM places them */
static int vs_on(void)
{
    const char *on = config_get("gpu3d_vs");
    if (!on || !on[0])
        return 2;
    return on[0] >= '1' && on[0] <= '2' ? on[0] - '0' : 0;
}

/* gpu3d_queue=1 (bm3d 4.1): the end of a frame's 3D starts on the GPU and the
 * game's next _update runs meanwhile; 2 (bm3d 5.1): and two jobs in flight.
 * Without the key the queue is on (1); gpu3d_queue=0 turns it off. */
static int queue_on(void)
{
    const char *on = config_get("gpu3d_queue");
    return !on ? 1 : (on[0] == '1' || on[0] == '2') ? on[0] - '0' : 0;
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

#ifndef BM_RGB30
/* menu_scale: no key or 1 = 360p drawn by the ARM; 4 = 360p drawn by the GPU; 2 = 720p and 3 = 1080p drawn by the GPU */
static const char *menu_size_choice(void)
{
    const char *v = config_get("menu_scale");
    return v && v[0] == '3' ? "1080p (GPU)" : v && v[0] == '2' ? "720p (GPU)" : v && v[0] == '4' ? "360p (GPU)" : "360p (ARM)";
}

/* CRT filter mode: 1 = off, 2 = rca (default for Pi 1), 3 = hdmi, 4 = on */
int crt_mode(void)
{
    const char *v = config_get("video_crt");
    if (!v || !v[0]) {
        uint8_t m = board()->model;
        if (m == BOARD_ZERO || m == BOARD_ZERO_W || m == BOARD_ZERO_2W)
            return 1; /* default: 1 = off on Pi Zero (HDMI only, save CPU) */
        return 2;     /* default: 2 = rca on Pi 1 */
    }
    if (strcmp(v, "off") == 0 || strcmp(v, "1") == 0) return 1;
    if (strcmp(v, "rca") == 0 || strcmp(v, "2") == 0) return 2;
    if (strcmp(v, "hdmi") == 0 || strcmp(v, "3") == 0) return 3;
    if (strcmp(v, "on") == 0 || strcmp(v, "4") == 0) return 4;
    return 1;
}

const char *crt_choice(void)
{
    static const char *const names[] = { "", "1. Off", "2. RCA only", "3. HDMI only", "4. On (both)" };
    int m = crt_mode();
    return names[m >= 1 && m <= 4 ? m : 2];
}

static int video_is_hdmi(void)
{
#ifdef BM_RGB30
    return 0;
#else
    return !!(mmio_read(PERIPHERAL_BASE + 0x902000 + 0x0A0) & (1u << 16));
#endif
}

int crt_filter_is_active(void)
{
    if (!board_is_pi1())
        return 0; /* Pi 1 only: disabled on Pi Zero to save CPU and avoid composite */
    int m = crt_mode();
    if (m == 1) return 0; /* off */
    if (m == 4) return 1; /* on */
    int hdmi = video_is_hdmi();
    if (m == 2) return !hdmi; /* rca only: active when NOT hdmi */
    if (m == 3) return hdmi;  /* hdmi only: active when hdmi */
    return 0;
}

void crt_apply_filter(void *pixels, int w, int h, int pitch_bytes)
{
    if (!pixels || w <= 0 || h <= 0)
        return;
    /* Fast 75% intensity scanlines on odd lines using 32-bit SIMD arithmetic on RGB565 */
    for (int y = 1; y < h; y += 2) {
        uint32_t *row = (uint32_t *)((uint8_t *)pixels + y * pitch_bytes);
        int words = w / 2;
        for (int x = 0; x < words; x++) {
            uint32_t p = row[x];
            uint32_t half = (p >> 1) & 0x7BEF7BEFu;
            row[x] = half + ((half >> 1) & 0x7BEF7BEFu);
        }
    }
}

#ifndef BM_RGB30
/* Video output mode: 0 = auto, 1 = rca, 2 = hdmi */
static int video_out_mode(void)
{
    const char *v = config_get("video_out");
    if (!v || !*v || strcmp(v, "auto") == 0) return 0;
    if (strcmp(v, "rca") == 0 || strcmp(v, "composite") == 0) return 1;
    if (strcmp(v, "hdmi") == 0) return 2;
    return 0;
}

static const char *video_out_choice(void)
{
    static const char *const names[] = { "Auto (HDMI/RCA)", "RCA (Composite)", "HDMI only" };
    int m = video_out_mode();
    return names[m >= 0 && m <= 2 ? m : 0];
}
#endif

#ifndef BM_RGB30
/* Audio output mode: 0 = auto, 1 = pwm (3.5mm jack), 2 = hdmi */
static int audio_out_mode(void)
{
    const char *v = config_get("audio_out");
    if (!v || !*v || strcmp(v, "auto") == 0) return 0;
    if (strcmp(v, "pwm") == 0 || strcmp(v, "jack") == 0 || strcmp(v, "analog") == 0) return 1;
    if (strcmp(v, "hdmi") == 0) return 2;
    return 0;
}

static const char *audio_out_choice(void)
{
    static const char *const names[] = { "Auto (HDMI/Jack)", "3.5mm Jack (PWM)", "HDMI only" };
    int m = audio_out_mode();
    return names[m >= 0 && m <= 2 ? m : 0];
}
#endif

/* Turns off the GPIO LCD backlight on pin 18 (and deselects SPI CE0/CE1 on 8/7)
 * when HDMI or Composite is active, avoiding power drain and light bleed. */
void gpio_lcd_power(int on)
{
#ifndef BM_RGB30
    if (!board_is_pi1())
        return; /* Only Raspberry Pi 1 models have the SPI LCD hat headers */
    if (!on) {
        spilcd_sleep(1);
        gpio_set_function(18, GPIO_OUTPUT);
        gpio_write(18, 0);
        gpio_set_function(22, GPIO_OUTPUT);
        gpio_write(22, 0);
        gpio_set_function(8, GPIO_OUTPUT);
        gpio_write(8, 1);
        gpio_set_function(7, GPIO_OUTPUT);
        gpio_write(7, 1);
    } else {
        spilcd_sleep(0);
        gpio_set_function(18, GPIO_OUTPUT);
        gpio_write(18, 1);
        gpio_set_function(22, GPIO_OUTPUT);
        gpio_write(22, 1);
    }
#else
    (void)on;
#endif
}

static int lcd_gpio_is_off(void)
{
    const char *v = config_get("lcd_gpio");
    return v && strcmp(v, "0") == 0;
}
#endif

#ifdef BM_RGB30
/* no CRT filter on the RGB30 (a Pi 1 feature): the same calls, always off */
int crt_mode(void)
{
    return 1;
}

const char *crt_choice(void)
{
    return "Off";
}

int crt_filter_is_active(void)
{
    return 0;
}

void crt_apply_filter(void *pixels, int w, int h, int pitch_bytes)
{
    (void)pixels;
    (void)w;
    (void)h;
    (void)pitch_bytes;
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

/* Settings > Controls */
static const char *ctl_button(const char *b)
{
    static const struct { const char *code, *name; } n[] = {
        { "a", "A" }, { "b", "B" }, { "x", "X" }, { "y", "Y" }, { "l1", "L1" }, { "r1", "R1" },
        { "l2", "L2" }, { "r2", "R2" }, { "st", "Start" }, { "se", "Select" },
    };
    for (unsigned k = 0; k < sizeof n / sizeof n[0]; k++)
        if (!strcmp(b, n[k].code))
            return n[k].name;
    return b;
}

static int ctl_changed(const char *scheme)
{
    int c = 0;
    for (int i = 0; i < controls_count(); i++)
        if (!strcmp(controls_scheme(i), scheme) && controls_get(i))
            c++;
    return c;
}

/* the next (dir 1) or previous (-1) of: Default, a, b, x, y, l1, r1, l2, r2, st, se */
static void ctl_step(int i, int dir)
{
    static const char *const cycle[] = { NULL, "a", "b", "x", "y", "l1", "r1", "l2", "r2", "st", "se" };
    const int menu = !strcmp(controls_scheme(i), "system") && strcmp(controls_code(i), "pa");
    const int n = menu ? 5 : (int)(sizeof cycle / sizeof cycle[0]);        /* the menus' OK and Cancel: a face button */
    const char *now = controls_get(i);
    int at = 0;
    for (int k = 1; now && k < n; k++)
        if (!strcmp(now, cycle[k]))
            at = k;
    at = (at + dir + n) % n;
    controls_set(i, cycle[at]);
    home_controls_changed();
}

/* The first page of Settings: one row for each section. */
static void panel_settings(home_panel_t *p)
{
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
    home_row(p, MENU_ROW_SUB, R_CONTROLS, "Controls",
             "Which button does what in the games and the menus", "remap");
    home_row(p, MENU_ROW_SUB, R_WIFI, wireless() ? "WiFi and network" : "Network",
             "The link, the address, a test", "%s", net_word());
#ifdef BM_RGB30
    home_row(p, MENU_ROW_SUB, R_SCREEN, "Screen",
             "Screen modes, 3D Bench, battery icon", "360x360 x2");
#else
    home_row(p, MENU_ROW_SUB, R_SCREEN, "Screen",
             "Drawing, 3D, menu size, test pattern", "%s%s",
             gpu3d_choice(),
             strcmp(gpu3d_choice(), "GPU") == 0 && vs_on() ? (vs_on() == 1 ? "+VS1" : "+VS") : "");
#endif
    home_row(p, MENU_ROW_SUB, R_SOUNDMENU, "Sound",
             "Volume, style, bit depth, test", "vol %d / %d", audio_volume(), AUDIO_VOLUME_MAX);
    home_row(p, MENU_ROW_SUB, R_UPDATES, "Updates",
             "The latest bm from GitHub, signed", "%s", update_ready() ? update_ready() : update_state());
    reports_row(p, MENU_ROW_SUB, R_REPORTS_SUB, "Reports", "Waiting on the SD card, to GitHub");
    home_row(p, MENU_ROW_CHOICE, R_DEV, "Bridge",
             "PC tools: off, on (staged), fast (direct)", "%s",
             bridge_label(bridge_mode()));
    home_row(p, MENU_ROW_CHOICE, R_REC, "Screen recorder",
             "Record game to /bm/rec as .bmv (RLE565, 15 fps)", "%s",
             rec_mode() ? "On" : "Off");
    home_row(p, MENU_ROW_SUB, R_SYSTEM, "System",
             "Version, memory, the log", "%s, spec %s", bm_version, bm_spec);
    /* the last two, on every system (the user's request, 2026-10-04) */
    home_row(p, MENU_ROW_ACTION, R_RESTART, "Restart", "Restarts the console", NULL);
    home_row(p, MENU_ROW_ACTION, R_POWEROFF, "Shut down", home_poweroff_help(), NULL);
}

/* Settings > Controllers: the players, the keyboard, the mouse and how to pair them. */
static void panel_controllers(home_panel_t *p)
{
    ksnprintf(p->title, sizeof p->title, "Settings > Controllers");
#ifdef BM_RGB30
    home_row(p, MENU_ROW_INFO, R_PAD1, "Player 1",
             "The console's buttons and sticks, with a keyboard if one is on", "built in");
    for (int s = 0; s < BT_PADS; s++) {         /* the Pi's slots: the Bluetooth pads are players 2 to 4 (D03) */
        char addr[18], label[16];
        int on = bt_pad_addr(s, addr);
        ksnprintf(label, sizeof label, "Player %d", BT_PLAYER(s));
        if (addr[0] && on)
            home_row(p, MENU_ROW_INFO, R_PAD1 + BT_FIRST_PLAYER + s, label, "A Bluetooth pad, connected",
                     "%s on", addr);
        else if (addr[0])
            home_row(p, MENU_ROW_INFO, R_PAD1 + BT_FIRST_PLAYER + s, label, "Paired: press its PS button to connect",
                     "%s off", addr);
        else
            home_row(p, MENU_ROW_INFO, R_PAD1 + BT_FIRST_PLAYER + s, label, "Free: pair a controller", "-");
    }
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
    home_row(p, MENU_ROW_ACTION, R_TEST, "Test the buttons", home_test_help(), NULL);
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
}

/* Settings > Controls: the four kinds of game and the reset. */
static void panel_controls(home_panel_t *p)
{
    ksnprintf(p->title, sizeof p->title, "Settings > Controls");
    home_row(p, MENU_ROW_SUB, R_CTL_SCROLLER, "Scroller games", "Platformers and side-scrollers: actions 1 to 4",
             "%d changed", ctl_changed("scroller"));
    home_row(p, MENU_ROW_SUB, R_CTL_TOPDOWN, "Top-down games", "Adventures seen from above: actions 1 to 4",
             "%d changed", ctl_changed("topdown"));
    home_row(p, MENU_ROW_SUB, R_CTL_STICK, "Stick games", "Twin-stick shooters: actions 1 to 8",
             "%d changed", ctl_changed("stick"));
    home_row(p, MENU_ROW_SUB, R_CTL_SYSTEM, "Pause and menus", "Pause, and the menus' OK and Cancel",
             "%d changed", ctl_changed("system"));
    home_row(p, MENU_ROW_INFO, R_CTL_KEYS, "Keyboard", "Fixed for now: Enter is OK, Esc goes back and pauses",
             "Enter, Esc");
    home_row(p, MENU_ROW_ACTION, R_CTL_RESET, "Reset all", "Every action back to the game's own button", NULL);
}

/* Settings > Controls > one kind of game: a row for each action. */
static void panel_control_buttons(home_panel_t *p, int id)
{
    const char *scheme = id == HOME_CTL_SCROLLER ? "scroller" : id == HOME_CTL_TOPDOWN ? "topdown" :
                         id == HOME_CTL_STICK ? "stick" : "system";
    ksnprintf(p->title, sizeof p->title, "Settings > Controls > %s",
              id == HOME_CTL_SCROLLER ? "Scroller games" : id == HOME_CTL_TOPDOWN ? "Top-down games" :
              id == HOME_CTL_STICK ? "Stick games" : "Pause and menus");
    for (int i = 0; i < controls_count(); i++) {
        if (strcmp(controls_scheme(i), scheme))
            continue;
#ifndef BM_RGB30
        if (!strcmp(controls_code(i), "mo") || !strcmp(controls_code(i), "mc"))
            continue;               /* the Pi's menus keep their buttons */
#endif
        const char *b = controls_get(i);
        if (b)
            home_row(p, MENU_ROW_CHOICE, R_CTL + i, controls_label(i), "Enter: another button; Default is the game's own",
                     "%s", ctl_button(b));
        else
            home_row(p, MENU_ROW_CHOICE, R_CTL + i, controls_label(i), "Enter: another button; Default is the game's own",
                     "Default (%s)", ctl_button(controls_default(i)));
    }
}

/* Settings > WiFi and network (Network on a board with Ethernet). */
static void panel_wifi(home_panel_t *p)
{
    /* the Pi Zero W and the RGB30 have WiFi, the Pi 1 B / B+ an Ethernet port */
    int wl = wireless();
    ksnprintf(p->title, sizeof p->title, "Settings > %s", wl ? "WiFi and network" : "Network");
    const char *ssid = config_get("wifi_ssid"), *pw = config_get("net_password");
    if (wl) {
        home_row(p, MENU_ROW_INFO, R_NETWORK, "Network", "The saved network (bm/config.txt)",
                 "%s", ssid && ssid[0] ? ssid : "none saved");
        home_row(p, MENU_ROW_INFO, R_STATE, "State", "The link to the access point",
                 "%s", wifi_linked() || net_link_kind() == NET_LINK_WIFI ? "connected" : "not connected");
        home_row(p, MENU_ROW_ACTION, R_CONNECT, "Connect to a network", home_connect_help(), NULL);
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
}

/* Settings > Screen. */
static void panel_screen(home_panel_t *p)
{
    ksnprintf(p->title, sizeof p->title, "Settings > Screen");
#ifdef BM_RGB30
    home_row(p, MENU_ROW_ACTION, R_MODES, "Screen modes",
             "The modes for games and the GPU, each with a test image", "%s", plat_display_info());
#else
    home_row(p, MENU_ROW_CHOICE, R_MENUSIZE, "Menu size",
             "360p on the ARM, or 720p / 1080p by the GPU. Next start", "%s", menu_size_choice());
#endif
    if (board_is_pi1()) {
        home_row(p, MENU_ROW_CHOICE, R_CRT, "CRT filter",
                 "Emulates CRT scanlines: 1. Off, 2. RCA, 3. HDMI, 4. On", "%s",
                 crt_choice());
#ifndef BM_RGB30
        home_row(p, MENU_ROW_CHOICE, R_VIDEO_OUT, "Video output",
                 "Select display port: Auto (HDMI fallback to RCA), RCA only, HDMI only", "%s",
                 video_out_choice());
        home_row(p, MENU_ROW_CHOICE, R_LCD_POWER, "GPIO LCD screen",
                 "Turn off GPIO screen backlight when using TV/monitor", "%s",
                 lcd_gpio_is_off() ? "Off (power save)" : "On");
        home_row(p, MENU_ROW_ACTION, R_GPIOSCREEN, "GPIO screen",
                 "3.5\" SPI TFT on the Pi 1: a tiny robot at its computer", NULL);
#endif
    }
    if (home_has_battery())
        home_row(p, MENU_ROW_CHOICE, R_BATTERY_ICON, "Low battery icon",
                 "A small red battery over games and tools at 20% or less", "%s",
                 battery_icon_on() ? "On" : "Off");
    else
        home_row(p, MENU_ROW_ACTION, R_PATTERN, "Test pattern",
                 "HDMI colour bars; any button returns", NULL);
    for (int i = 0; i < home_utils() && i < HOME_ROWS_MAX; i++)     /* the 3D tests and benchmarks */
        if (home_util_group(i) == 1)
            home_row(p, MENU_ROW_ACTION, R_UTIL_BASE + i, home_util_title(i), home_util_about(i), NULL);
}

/* Settings > Sound. */
static void panel_sound(home_panel_t *p)
{
    ksnprintf(p->title, sizeof p->title, "Settings > Sound");
#ifdef BM_RGB30
    /* the speaker, or the headphones (the console switches by itself) */
    home_row(p, MENU_ROW_INFO, R_SOUND, "Sound", audio_status(), "%s", audio_ready() ? "on" : "off");
    home_row(p, MENU_ROW_CHOICE, R_VOLUME, "Volume",
             "Sound of the games and tools (the + and - keys too)",
             "%d / %d", audio_volume(), AUDIO_VOLUME_MAX);
    home_row(p, MENU_ROW_CHOICE, R_STYLE, "Sound style",
             "Hi-fi, or the 8-bit chip of the first versions", "%s", audio_retro_on() ? "8-bit" : "Hi-fi");
    home_row(p, MENU_ROW_CHOICE, R_DEPTH, "Bit depth",
             "Each sample: 16, 24 or 32 bits (the codec converts 24)", "%s", depth_choice());
    home_row(p, MENU_ROW_ACTION, R_AUDIO, "Test the sound",
             "The sound's state and a test tune", NULL);
#else
    home_row(p, MENU_ROW_INFO, R_SOUND, "Sound", audio_status(), "%s", audio_ready() ? "on" : "off");
    if (board_is_pi1()) {
        home_row(p, MENU_ROW_CHOICE, R_AUDIO_OUT, "Audio output",
                 "Sound routing: Auto (HDMI/3.5mm), 3.5mm Jack (PWM), HDMI only", "%s", audio_out_choice());
    }
    home_row(p, MENU_ROW_CHOICE, R_VOLUME, "Volume",
             "Sound of the games and tools (games can change it in their pause menu)",
             "%d / %d", audio_volume(), AUDIO_VOLUME_MAX);
    home_row(p, MENU_ROW_CHOICE, R_STYLE, "Sound style",
             "Hi-fi (stereo, room), or the 8-bit chip of the first versions", "%s",
             audio_retro_on() ? "8-bit" : "Hi-fi");
    home_row(p, MENU_ROW_CHOICE, R_DEPTH, "Bit depth",
             "Each sample: 16, 24 or 32 bits (HDMI carries 24)", "%s", depth_choice());
    home_row(p, MENU_ROW_ACTION, R_AUDIO, "Test the sound",
             "HDMI sound status and a test tune", NULL);
#endif
}

/* Settings > Updates. */
static void panel_updates(home_panel_t *p)
{
    ksnprintf(p->title, sizeof p->title, "Settings > Updates");
    home_row(p, MENU_ROW_INFO, R_VERSION, "Version", "The kernel build (git describe)", "%s", bm_version);
    home_row(p, MENU_ROW_ACTION, R_UPDATE, "Check for updates",
             "The latest release on GitHub, signed: what would change", "%s", update_state());
    if (netxfer_has_staged_kernel()) {
        home_row(p, MENU_ROW_ACTION, R_INSTALL_STAGED, "Apply staged kernel",
                 "Written to SD card, old kernel backed up, restarts", "%s", netxfer_staged_kernel_desc());
    }
    if (update_ready()) {
        home_row(p, MENU_ROW_ACTION, R_INSTALL, "Install the update",
                 "Keeps the old kernels in /bm/backup, restarts", "%s", update_install_state());
        /* what it would write (the check runs in the background: the
         * text console no longer shows its list) */
        static char sizes[HOME_ROWS_MAX][24];
        const char *path, *what;
        uint32_t size;
        for (int i = 0; p->n < HOME_ROWS_MAX && update_changes(i, &path, &what, &size); i++) {
            ksnprintf(sizes[i], sizeof sizes[i], "%lu KB", (unsigned long)((size + 1023) / 1024));
            home_row(p, MENU_ROW_INFO, R_CHANGE, path, sizes[i], "%s", what);
        }
    }
}

/* Settings > Reports. */
static void panel_reports(home_panel_t *p)
{
    ksnprintf(p->title, sizeof p->title, "Settings > Reports");
    reports_row(p, MENU_ROW_INFO, R_WAITING, "On the SD card", "In bm/reports, until they are sent");
    reports_row(p, MENU_ROW_ACTION, R_REPORTS, "Send the reports",
                "The tests' reports on the SD card, to GitHub (github_token)");
    home_row(p, MENU_ROW_ACTION, R_REPORT_LOG, "Report the log",
             "Everything printed since boot, as a report (Send the reports)", NULL);
}

/* Settings > System. */
static void panel_system(home_panel_t *p)
{
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
             "%lu of %lu MB", (uint32_t)((heap_brk() - heap_start()) >> 20),
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
                 "%lu of %lu MB", (uint32_t)mi.uordblks >> 20,
                 (uint32_t)((heap_end() - heap_start()) >> 20));
        home_row(p, MENU_ROW_INFO, R_CLOCKS, "CPU", "Processor, clock and chip temperature",
                 "%s, %lu MHz, %lu.%lu C", sysinfo_cpu(), prop_clock_rate(CLOCK_ARM) / 1000000,
                 temp[1] / 1000, temp[1] % 1000 / 100);
    }
#endif
    home_row(p, MENU_ROW_INFO, R_SD, "SD card", "The card the console started from",
             "%s", fat_describe());
    home_row(p, MENU_ROW_SUB, R_SDMAP, "SD map", "What is on the card, by folder", "%s", sdmap_state_name());
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
    /* the value is short (the row is narrow); the help line is the backend's own text (bm_port.c) */
    home_row(p, MENU_ROW_INFO, R_DRIVER3D, "3D driver", gpu3d_status(), "%s", gpu3d_ready() ? "Mali" : "ARM");
#else
    {
        /* the drivers' version, and the one the 3D settings reproduce */
        const int gpu = gpu3d_on() && !gpu3d_failed() && v3d_init() == 0;
        home_row(p, MENU_ROW_INFO, R_DRIVER3D, "3D driver", "bm3d version (block); the games' 3D now",
                 "bm3d %s (%s), as %s", BM3D_VERSION, BM3D_BLOCK,
                 bm3d_mode_q(gpu, gpu ? vs_on() : 0, gpu && queue_on()));
    }
#endif
    if (util_count(0) > 0)                  /* no empty page */
        home_row(p, MENU_ROW_SUB, R_UTILS, "Utilities", "Benchmarks and tests", "%d", util_count(0));
    home_row(p, MENU_ROW_CHOICE, R_DEV, "Bridge",
             "PC tools: off, on (staged), fast (direct)", "%s",
             bridge_label(bridge_mode()));
    home_row(p, MENU_ROW_CHOICE, R_REC, "Screen recorder",
             "Record game to /bm/rec as .bmv (RLE565, 15 fps)", "%s",
             rec_mode() ? "On" : "Off");
    home_row(p, MENU_ROW_ACTION, R_LOG, "Log since boot",
             "Everything printed since the console started", NULL);
#ifndef BM_RGB30
    home_row(p, MENU_ROW_ACTION, R_MONITOR, "Open the monitor",
             "The text console with every command", NULL);
#endif
}

/* Settings > System > Utilities. */
static void panel_utilities(home_panel_t *p)
{
    ksnprintf(p->title, sizeof p->title, "Settings > System > Utilities");
    for (int i = 0; i < home_utils() && i < HOME_ROWS_MAX; i++)
        if (home_util_group(i) == 0)
            home_row(p, MENU_ROW_ACTION, R_UTIL_BASE + i, home_util_title(i), home_util_about(i), NULL);
    if (util_count(0) == 0)
        home_row(p, MENU_ROW_INFO, R_UTILS, "none", "This console has no utility yet", NULL);
}

/* Settings > System > SD map: the bar and the folders. */
static void panel_sdmap(home_panel_t *p)
{
    static uint16_t parts[7];
    static const uint32_t col[7] = { 0xE06C75, 0x61AFEF, 0x98C379, 0xC678DD, 0x56B6C2, 0xE5C07B, 0x4A5060 };
    static const char *const name[7] = { "Kernel", "bm (games)", "bme (projects)", "glb (models)", "bmm (models)", "Other", "Free" };
    ksnprintf(p->title, sizeof p->title, "Settings > System > SD map");
    sdmap_request();
    const sdmap_t *m = sdmap_result();
    const uint64_t v[7] = { m->kernel, m->bm, m->bme, m->glb, m->bmm, m->other, m->free };
    for (int i = 0; i < 7; i++)
        parts[i] = m->total ? (uint16_t)(v[i] * 1000 / m->total) : 0;
    home_row(p, MENU_ROW_BAR, R_SDBAR, "SD card", "Kernel, games, projects, models, other files and free space", NULL);
    p->rows[p->n - 1].bar = parts;
    for (int i = 0; i < 7; i++) {
        char sz[24];
        fmt_size(sz, sizeof sz, v[i]);
        home_row(p, MENU_ROW_INFO, R_SDINFO, name[i], "The colour of the bar", "%s", sz);
        p->rows[p->n - 1].swatch = col[i];
    }
    home_row(p, MENU_ROW_INFO, R_SDINFO, "Scan", "The menu reads the card while it is idle",
             "%s %d%%", sdmap_state_name(), sdmap_permille() / 10);
    for (int i = 0; i < m->ndirs; i++)
        home_row(p, MENU_ROW_INFO, R_SDINFO, m->dirs[i].path, "Files directly in this folder",
                 "%lu files, %lu KB", (unsigned long)m->dirs[i].files, (unsigned long)m->dirs[i].kb);
    home_row(p, MENU_ROW_SUB, R_SDFILES, "Other files", "Files the menu cannot show, largest first",
             "%d", m->nfiles);
}

/* Settings > System > SD map > Other files. */
static void panel_sdfiles(home_panel_t *p)
{
    ksnprintf(p->title, sizeof p->title, "Settings > System > SD map > Other files");
    const sdmap_t *m = sdmap_result();
    for (int i = 0; i < m->nfiles; i++) {
        const char *path = m->files[i].path;
        size_t len = strlen(path);
        char lab[32];
        if (len > 31)
            ksnprintf(lab, sizeof lab, "..%s", path + len - 29);
        else
            ksnprintf(lab, sizeof lab, "%s", path);
        uint32_t kb = m->files[i].bytes / 1024;
        if (kb == 0 && m->files[i].bytes)
            kb = 1;
        home_row(p, MENU_ROW_INFO, R_SDINFO, lab, "Its path and size", "%lu KB", (unsigned long)kb);
    }
    if (m->nfiles == 0)
        home_row(p, MENU_ROW_INFO, R_SDINFO, "none", "Every file on the card is shown by the menu", NULL);
}

void home_panel(int id, home_panel_t *p)
{
    memset(p, 0, sizeof *p);
    switch (id) {
    case HOME_SETTINGS:
        panel_settings(p);
        break;
    case HOME_CONTROLLERS:
        panel_controllers(p);
        break;
    case HOME_CONTROLS:
        panel_controls(p);
        break;
    case HOME_CTL_SCROLLER: case HOME_CTL_TOPDOWN: case HOME_CTL_STICK: case HOME_CTL_SYSTEM:
        panel_control_buttons(p, id);
        break;
    case HOME_WIFI:
        panel_wifi(p);
        break;
    case HOME_GRAPHICS:
        panel_screen(p);
        break;
    case HOME_SOUND:
        panel_sound(p);
        break;
    case HOME_UPDATES:
        panel_updates(p);
        break;
    case HOME_REPORTS:
        panel_reports(p);
        break;
    case HOME_SYSTEM:
        panel_system(p);
        break;
    case HOME_UTILITIES:
        panel_utilities(p);
        break;
    case HOME_SDMAP:
        panel_sdmap(p);
        break;
    case HOME_SDFILES:
        panel_sdfiles(p);
        break;
    }
}

int home_sub_panel(int row)
{
    switch (row) {
    case R_CONTROLLERS: return HOME_CONTROLLERS;
    case R_WIFI: return HOME_WIFI;
    case R_SCREEN: return HOME_GRAPHICS;
    case R_SOUNDMENU: return HOME_SOUND;
    case R_UPDATES: return HOME_UPDATES;
    case R_REPORTS_SUB: return HOME_REPORTS;
    case R_SYSTEM: return HOME_SYSTEM;
    case R_UTILS: return HOME_UTILITIES;
    case R_SDMAP: return HOME_SDMAP;
    case R_SDFILES: return HOME_SDFILES;
    case R_CONTROLS: return HOME_CONTROLS;
    case R_CTL_SCROLLER: return HOME_CTL_SCROLLER;
    case R_CTL_TOPDOWN: return HOME_CTL_TOPDOWN;
    case R_CTL_STICK: return HOME_CTL_STICK;
    case R_CTL_SYSTEM: return HOME_CTL_SYSTEM;
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
    wifi_auto_stop();                   /* a try of the menu's under way: not two at once */
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
        kprintf("HTTP %d, %lu bytes in %lu ms (%lu KB/s): the connection works\n", st,
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

static void x_gpioscreen(framebuffer_t *fb)
{
    console_suspend(1);
    netcon_focus("the menu (GPIO screen: Esc or q returns)", NULL);
    spilcd_desk_robot(fb);
    console_suspend(0);
    heading("GPIO screen");
    kprintf("GPIO screen: tiny robot desk mode (%s)\n",
            spilcd_ready() ? "SPI0 480x320 active on Pi 1" : "Pi 1 only");
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

/* A row that opens the panel of a section. 1 if the row is one of them. */
static int act_open(int row, home_do_t *d)
{
    switch (row) {
    case R_CONTROLS: case R_CTL_SCROLLER: case R_CTL_TOPDOWN: case R_CTL_STICK: case R_CTL_SYSTEM:
    case R_CONTROLLERS: case R_WIFI: case R_SYSTEM: case R_SCREEN: case R_UPDATES: case R_REPORTS_SUB: case R_UTILS:
    case R_SDMAP: case R_SDFILES: case R_SOUNDMENU:
        d->what = HOME_OPEN;
        d->panel = home_sub_panel(row);
        break;
    default:
        return 0;
    }
    return 1;
}

/* The rows of Controllers and Controls. 1 if the row is one of them. */
static int act_controls(int row, int how, home_do_t *d)
{
    switch (row) {
    case R_CTL ... R_CTL + CONTROLS_COUNT - 1: {
        const int i = row - R_CTL;
        ctl_step(i, how < 0 ? -1 : 1);
        const char *b = controls_get(i);
        ksnprintf(d->note, sizeof d->note, "%s: %s", controls_label(i), b ? ctl_button(b) : "the game's own button");
        break;
    }
    case R_LAYOUT:
        hid_set_layout(hid_layout()[0] == 'i' ? "us" : "it");
        config_save();
        ksnprintf(d->note, sizeof d->note, "keyboard layout: %s", hid_layout());
        break;
    case R_PROMPTS:
        config_set("prompts", home_prompts_colour() ? "white" : "colour");
        config_save();
        ksnprintf(d->note, sizeof d->note, "button icons: %s", home_prompts_colour() ? "colour" : "white");
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
    case R_CTL_RESET:
        if (how == HOME_YES) {
            controls_reset();
            ksnprintf(d->note, sizeof d->note, "every action is back on the game's own button");
        } else if (how == 0) {
            ask(d, "Reset all the controls?", "Every action goes back to the game's own button.", "Reset");
        }
        break;
    case R_FORGET:
        if (how == HOME_YES) {
            int n = bt_forget_all();
            ksnprintf(d->note, sizeof d->note, "%d controller%s forgotten", n, n == 1 ? "" : "s");
        } else if (how == 0) {
            ask(d, "Forget all controllers?", "Pads, keyboard and mouse: pair them again.", "Forget");
        }
        break;
    default:
        return 0;
    }
    return 1;
}

/* The rows of Screen (and the boards' own output rows). 1 if the row is one of them. */
static int act_screen(int row, int how, home_do_t *d)
{
    switch (row) {
    case R_BATTERY_ICON:
        config_set("battery_icon", battery_icon_on() ? "0" : "1");
        config_save();
        ksnprintf(d->note, sizeof d->note, "low battery icon: %s", battery_icon_on() ? "on" : "off");
        break;
#ifdef BM_RGB30
    case R_MODES:
        if (how == 0) text(d, x_modes, 0, 1);
        break;
#else
    case R_MENUSIZE: {
        const char *v = config_get("menu_scale");
        if (!v || !*v || v[0] == '1')
            config_set("menu_scale", "4");
        else if (v[0] == '4')
            config_set("menu_scale", "2");
        else if (v[0] == '2')
            config_set("menu_scale", "3");
        else
            config_unset("menu_scale");
        config_save();
        ksnprintf(d->note, sizeof d->note, "menu size at the next start: %s", menu_size_choice());
        break;
    }
    case R_USB:
        if (how == 0) text(d, x_usb, 1, 0);
        break;
    case R_ETH:
        if (how == 0) text(d, x_eth, 1, 0);
        break;
    case R_PATTERN:
        if (how == 0) text(d, x_pattern, 0, 0);
        break;
    case R_GPIOSCREEN:
        if (how == 0 && board_is_pi1()) text(d, x_gpioscreen, 0, 0);
        break;
    case R_MONITOR:
        if (how == 0) d->what = HOME_MONITOR;
        break;
#endif
    case R_CRT: {
        int m = crt_mode() + (how == -1 ? -1 : 1);
        if (m > 4) m = 1;
        if (m < 1) m = 4;
        static const char *const val_names[] = { "", "off", "rca", "hdmi", "on" };
        config_set("video_crt", val_names[m]);
        config_save();
        ksnprintf(d->note, sizeof d->note, "CRT filter: %s", crt_choice());
        break;
    }
#ifndef BM_RGB30
    case R_VIDEO_OUT: {
        int m = (video_out_mode() + (how == -1 ? 2 : 1)) % 3;
        static const char *const vals[] = { "auto", "rca", "hdmi" };
        config_set("video_out", vals[m]);
        config_save();
        ksnprintf(d->note, sizeof d->note, "Video: %s (reboot to apply)", video_out_choice());
        break;
    }
    case R_AUDIO_OUT: {
        int m = (audio_out_mode() + (how == -1 ? 2 : 1)) % 3;
        static const char *const vals[] = { "auto", "pwm", "hdmi" };
        config_set("audio_out", vals[m]);
        config_save();
        ksnprintf(d->note, sizeof d->note, "Audio: %s (reboot to apply)", audio_out_choice());
        break;
    }
    case R_LCD_POWER: {
        int off = !lcd_gpio_is_off();
        config_set("lcd_gpio", off ? "0" : "1");
        config_save();
        gpio_lcd_power(!off);
        ksnprintf(d->note, sizeof d->note, "GPIO LCD backlight: %s", off ? "Off (power save)" : "On");
        break;
    }
#endif
    default:
        return 0;
    }
    return 1;
}

/* The rows of WiFi and network. 1 if the row is one of them. */
static int act_network(int row, int how, home_do_t *d)
{
    switch (row) {
    case R_BOOT:
        config_set("wifi_boot", wifi_at_boot() ? "0" : "1");
        config_save();
        ksnprintf(d->note, sizeof d->note, "WiFi at boot: %s", wifi_at_boot() ? "on" : "off");
        break;
    case R_CONNECT:
        if (how == 0) text(d, x_connect, 1, 0);
        break;
    case R_NETTEST:
        if (how == 0) text(d, x_nettest, 1, 0);
        break;
    default:
        return 0;
    }
    return 1;
}

/* The rows of System, Restart and Shut down. 1 if the row is one of them. */
static int act_system(int row, int how, home_do_t *d)
{
    switch (row) {
    case R_DEV: {
        int m = bridge_mode();
        m = how < 0 ? (m > BRIDGE_OFF ? m - 1 : m) : how > 0 ? (m < BRIDGE_FAST ? m + 1 : m) : (m + 1) % 3;
        if (bridge_set(m) != 0)
            ksnprintf(d->note, sizeof d->note, "bridge: cannot save on the SD card");
        else
            ksnprintf(d->note, sizeof d->note, "bridge: %s%s", bridge_label(m),
                      m == BRIDGE_FAST ? " (kernel and files written at once)" : m == BRIDGE_ON ? " (a kernel waits for the menu)" : " (no PC tool)");
        break;
    }
    case R_REC: {
        int on = how < 0 ? 0 : how > 0 ? 1 : !rec_mode();
        rec_set_mode(on);
        ksnprintf(d->note, sizeof d->note, "recorder: %s (/bm/rec, RLE565 15 fps)", on ? "on" : "off");
        break;
    }
    case R_LOG:
        if (how == 0) text(d, x_log, 0, 1);
        break;
    case R_RESTART:
        if (how == HOME_YES) {
            kprintf("rebooting...\n");
            wifi_auto_stop();
            home_restart();
        } else if (how == 0) {
            ask(d, "Restart the console?", "A game left suspended is closed.", "Restart");
        }
        break;
    case R_POWEROFF:
        if (how == HOME_YES) {
            kprintf("shutting down\n");
            home_poweroff();
        } else if (how == 0) {
            ask(d, "Shut the console down?", home_poweroff_detail(), "Shut down");
        }
        break;
    default:
        return 0;
    }
    return 1;
}

/* The rows of Updates and Reports. 1 if the row is one of them. */
static int act_updates(int row, int how, home_do_t *d)
{
    switch (row) {
    case R_UPDATE:
        /* in the menu's background (update.c): the row says how it goes */
        if (how == 0 && update_start(0) == 0)
            ksnprintf(d->note, sizeof d->note, "checking for updates...");
        else if (how == 0) {
            d->fail = 1;
            ksnprintf(d->note, sizeof d->note, "%s: wait", update_busy() ? update_busy() : "out of memory");
        }
        break;
    case R_REPORTS:
        if (how == 0) text(d, t_send_reports, 1, 0);
        break;
    case R_REPORT_LOG:
        if (how == 0) text(d, t_report_log, 1, 0);
        break;
    case R_INSTALL:
        if (how == HOME_YES) {
            /* in the menu's background: a box over the menu, then the restart */
            if (update_start(1) == 0)
                ksnprintf(d->note, sizeof d->note, "installing bm %s: the console restarts when it is done",
                          update_ready());
            else {
                d->fail = 1;
                ksnprintf(d->note, sizeof d->note, "%s: wait", update_busy() ? update_busy() : "out of memory");
            }
        } else if (how == 0 && update_busy()) {
            d->fail = 1;
            ksnprintf(d->note, sizeof d->note, "%s: wait", update_busy());
        } else if (how == 0 && update_ready()) {
            char q[64];
            ksnprintf(q, sizeof q, "Install bm %s?", update_ready());
            ask(d, q, "The console restarts when it is done.", "Install");
        }
        break;
    case R_INSTALL_STAGED:
        if (how == HOME_YES) {
            if (netxfer_apply_staged_kernel() == 0) {
                ksnprintf(d->note, sizeof d->note, "Applying kernel: restarting in 3 seconds");
            } else {
                d->fail = 1;
                ksnprintf(d->note, sizeof d->note, "Failed to apply staged kernel");
            }
        } else if (how == 0) {
            ask(d, "Apply downloaded kernel?", "The old kernel is backed up in /bm/backup. Restarts when done.", "Apply & Restart");
        }
        break;
    default:
        return 0;
    }
    return 1;
}

/* The rows of Sound. 1 if the row is one of them. */
static int act_sound(int row, int how, home_do_t *d)
{
    switch (row) {
    case R_VOLUME: {
        int v = audio_volume() + (how ? how : 1);
        if (how == 0 && v > AUDIO_VOLUME_MAX)
            v = 0;                              /* A goes round */
        audio_set_volume(v);
        config_save();
        audio_note(0, 880, 70, 4, 36);          /* a soft beep at the new volume */
        ksnprintf(d->note, sizeof d->note, "volume: %d / %d", audio_volume(), AUDIO_VOLUME_MAX);
        break;
    }
    case R_AUDIO:
        if (how == 0) text(d, x_audio, 1, 0);
        break;
    case R_STYLE: {
        int retro = !audio_retro_on();
        audio_retro(AUDIO_RETRO_USER, retro);
        config_set("sound", retro ? "8bit" : "hifi");
        config_save();
        audio_play_sound(0, &au_presets[au_preset_find(retro ? "chip" : "epiano")].s, 72, 220, 300);
        ksnprintf(d->note, sizeof d->note, "sound: %s", retro ? "8-bit" : "hi-fi");
        break;
    }
    case R_DEPTH: {
        unsigned b = audio_depth();             /* 16 -> 24 -> 32 -> 16 (left: the other way) */
        if (how < 0)
            b = b == 16 ? 32 : b == 24 ? 16 : 24;
        else
            b = b == 16 ? 24 : b == 24 ? 32 : 16;
        audio_set_depth(b);
        char v[4];
        ksnprintf(v, sizeof v, "%u", b);
        config_set("sound_depth", v);
        config_save();
        audio_play_sound(0, &au_presets[au_preset_find("epiano")].s, 72, 220, 300);
        ksnprintf(d->note, sizeof d->note, "depth: %u bits", b);
        break;
    }
    default:
        return 0;
    }
    return 1;
}

void home_act(int id, int row, int how, home_do_t *d)
{
    memset(d, 0, sizeof *d);
    d->what = HOME_STAY;
    (void)id;
    if (row >= R_UTIL_BASE && row < R_UTIL_BASE + HOME_ROWS_MAX) {
        if (how == 0 && row - R_UTIL_BASE < home_utils())
            home_util_start(row - R_UTIL_BASE, d);
        return;
    }
    (void)(act_open(row, d) || act_controls(row, how, d) || act_screen(row, how, d) || act_network(row, how, d) || act_system(row, how, d) || act_updates(row, how, d) || act_sound(row, how, d));
}

void home_report(const home_do_t *d)
{
    if (!d->note[0])
        return;
    kprintf("menu: %s\n", d->note);
    if (d->fail)
        notice_fail(d->note);
}
