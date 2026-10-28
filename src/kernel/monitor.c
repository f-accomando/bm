#include "monitor.h"
#include "monitor_cmd.h"
#include "bm/runtime.h"
#include "upload.h"
#include "update.h"
#include "b3dpi.h"
#include "home.h"
#include "input.h"
#include "script/repl.h"
#include "sysinfo.h"
#include "syskeys.h"
#include "testpattern.h"
#include "drivers/fb.h"
#include "gfx/console.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "drivers/watchdog.h"
#include "lib/crc32.h"
#include "lib/printf.h"
#include "monitor_fs.h"
#include "usb/hid.h"
#include "usb/usb.h"
#include "usb/smsc95xx.h"
#include "carts.h"
#include "config.h"
#include "bt/bt.h"
#include "wifi/wifi.h"
#include "net/net.h"
#include "net/wifi_auto.h"
#include "net/http.h"
#include "net/netxfer.h"
#include "net/netcon.h"
#include "pager.h"
#include "reports.h"
#include "audio/audio.h"
#include "crumbs.h"
#include "drivers/watchdog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The Monitor is a table of commands (monitor_cmd.h). A row has the word,
 * its arguments, one line of help, the single key that is its shortcut and
 * its handler. The help, the Tab completion and the keys come from the
 * table: to add a command, add one row to `commands` below.
 *
 * from_key is 1 while a handler runs for a single key. Then it may ask
 * (prompt for an address, wait for a second key) and show its pages; in
 * a line it takes its words from the arguments and never waits.
 */
static int from_key;
static int want_exit;                   /* the 'exit' command: leave the bm> prompt */

#define MON_FOCUS "the monitor ('h' for help, M the menu)"

#define CMD(f) static int f(int argc __attribute__((unused)), char **argv __attribute__((unused)))

static const mcmd_t *cmd_table(void);

static void __attribute__((noinline)) trigger_undef(void)
{
    __asm__ volatile(".word 0xe7f000f0");       /* permanently undefined */
}

static void __attribute__((noinline)) trigger_dabt(void)
{
    uint32_t sctlr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr |= 1u << 1;                           /* A: alignment checking */
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" : : "r"(sctlr));
    volatile uint32_t *p = (volatile uint32_t *)0x8001;
    (void)*p;
}

static void __attribute__((noinline)) trigger_pabt(void)
{
    __asm__ volatile("bkpt #0x33");
}

static void __attribute__((noinline)) trigger_swi(void)
{
    __asm__ volatile("svc #0x42");
}

/* 'G' and "net get": a GET, to try the network from the monitor (M19). With an address
 * it does not ask; without one it asks (Enter: the last one) */
static int net_get_test(const char *given)
{
    static char url[256] = "https://example.com/";
    if (given && *given) {
        snprintf(url, sizeof url, "%s%s", strstr(given, "://") ? "" : "https://", given);
    } else {
        kprintf("address (Enter: %s): ", url);
        char line[240];
        int n = input_read_line(line, sizeof line, 0);
        if (n < 0)
            return 0;
        if (n > 0)
            snprintf(url, sizeof url, "%s%s", strstr(line, "://") ? "" : "https://", line);
    }
    uint32_t t0 = timer_ticks();
    uint8_t *data;
    size_t len;
    http_info_t info;
    int st = http_get_buffer(url, NULL, 8u << 20, &data, &len, &info);
    uint32_t ms = (timer_ticks() - t0) / 1000;
    int ret = 0;
    if (st < 0) {
        kprintf("\x1b[91mget: %s\x1b[0m\n", info.error);
        ret = -1;
    } else {
        kprintf("get: %d, %lu bytes in %lu ms (%lu KB/s), %s\n", st, (unsigned long)len, ms,
                ms ? (unsigned long)(len * 1000 / 1024 / ms) : 0, info.type[0] ? info.type : "no type");
        if (strcmp(info.url, url) != 0)
            kprintf("     from %s\n", info.url);
        if (info.error[0])
            kprintf("     %s\n", info.error);
        size_t show = len < 300 ? len : 300;
        for (size_t i = 0; i < show; i++) {
            char c = (char)data[i];
            kprintf("%c", (c == '\n' || (c >= 32 && c < 127)) ? c : '.');
        }
        if (show)
            kprintf("%s\n", len > show ? "..." : "");
    }
    kprintf("net: time %s\n", net_time_text());
    free(data);
    return ret;
}

/* ---------------------------------------------------------------- handlers */

CMD(c_help)
{
    static char text[9216];
    static const char head[] =
        "commands (games: arrows/wasd, space = A, Esc their menu; Ctrl+Esc or q leaves):\n"
        "  A key in the first column runs its command. A word does the same: ':' and\n"
        "  Enter open the bm> prompt (Tab completes, Up repeats, exit or Esc leaves).\n"
        "  'help NAME' tells more about one command.\n";
    static const char foot[] =
        ":  a line of commands, ';' between them, run one after the other:\n"
        "       :bench 3d tests=match,quad_tex profiles=GPU,FS2; report send\n"
        "       quotes keep spaces: wifi join \"my net\" \"a password\"\n"
        "     crc: the CRC32 of the page on screen\n"
        "       (crc: WxH bpp=D pitch=P crc32=0x........)\n"
        "     (from the menu too: the ':' takes the line to the monitor; from the PC\n"
        "     tools/bm_net.py IP --line \"...\" or easy_install.sh line \"...\")\n"
        "     from the PC: tools/bm_net.py IP (console, --send/--play a cart, --kernel)\n";
    if (argc > 1) {
        if (!mcmd_help_one(cmd_table(), argc - 1, argv + 1, text, sizeof text)) {
            char sug[96];
            kprintf("help: no command '%s'", argv[1]);
            if (mcmd_suggest(cmd_table(), argv[1], sug, sizeof sug))
                kprintf("; did you mean: %s", sug);
            kprintf("\n");
            return -1;
        }
    } else {
        mcmd_help_all(cmd_table(), head, foot, text, sizeof text);
    }
    pager_show(text);
    return 0;
}

CMD(c_menu) { carts_menu(console_framebuffer()); return 0; }
CMD(c_lua) { repl_run(); return 0; }
CMD(c_upload) { upload_and_play(console_framebuffer()); return 0; }
CMD(c_carts_list) { carts_list(); return 0; }
CMD(c_carts_rescan) { carts_init(); carts_list(); return 0; }

CMD(c_info) { sysinfo_print(); return 0; }
CMD(c_heap) { sysinfo_print_heap(); return 0; }
CMD(c_clear) { console_clear(); return 0; }
CMD(c_dmesg) { pager_show(klog_text()); return 0; }
CMD(c_usb) { usb_init(); usb_print(); return 0; }
CMD(c_input) { usb_live_test(5); input_live_test(10); return 0; }
CMD(c_audio) { audio_test(); return 0; }

CMD(c_layout)
{
    if (argc > 1 && strcmp(argv[1], "it") && strcmp(argv[1], "us")) {
        kprintf("layout: it or us\n");
        return -1;
    }
    hid_set_layout(argc > 1 ? argv[1] : hid_layout()[0] == 'i' ? "us" : "it");
    kprintf("keyboard layout: %s\n", hid_layout());
    config_save();
    return 0;
}

CMD(c_via_ram)
{
    if (argc > 1 && strcmp(argv[1], "on") && strcmp(argv[1], "off")) {
        kprintf("via-ram: on or off\n");
        return -1;
    }
    bm_set_via_ram(argc > 1 ? !strcmp(argv[1], "on") : !bm_via_ram());
    kprintf(".bm carts draw %s\n", bm_via_ram() ? "via a RAM buffer" : "directly on screen");
    config_save();
    return 0;
}

CMD(c_pattern)
{
    framebuffer_t *fb = console_framebuffer();
    console_suspend(1);
    draw_test_pattern(fb);
    kprintf("test pattern shown, press any key\n");
    input_getc();
    console_suspend(0);
    return 0;
}

/* X: two keys, so that a stray key never halts the console. In a line: the kind as a word */
CMD(c_crash)
{
    char t;
    if (argc > 1) {
        static const char *const kinds[] = { "undef", "dabt", "pabt", "svc", "freeze", "freeze-irq" };
        static const char letters[] = "uabsfg";
        t = 0;
        for (int i = 0; i < 6; i++)
            if (!strcmp(argv[1], kinds[i]) || (argv[1][1] == 0 && argv[1][0] == letters[i]))
                t = letters[i];
        if (!t) {
            kprintf("crash: undef, dabt, pabt, svc, freeze or freeze-irq\n");
            return -1;
        }
    } else {
        kprintf("crash test: u undefined insn, a data abort, b prefetch abort, "
                "s SVC,\n  f freeze (the watchdog restarts the Pi in 3 s),\n"
                "  g freeze with IRQs on (the guard restarts it in about 23 s); other keys cancel\n");
        t = input_getc();
    }
    if (t == 'u') trigger_undef();
    else if (t == 'a') trigger_dabt();
    else if (t == 'b') trigger_pabt();
    else if (t == 's') trigger_swi();
    else if (t == 'f') {
        crumb("freeze test (X f)", NULL);
        __asm__ volatile("cpsid i" ::: "memory");
        for (;;)
            ;
    }
    else if (t == 'g') {
        crumb("freeze test (X g, IRQs on)", NULL);
        for (;;)
            ;
    }
    else kprintf("cancelled\n");
    return 0;
}

CMD(c_reboot)
{
    kprintf("rebooting...\n");
    crumbs_restart(0);
}

CMD(c_tool_editor) { carts_editor(console_framebuffer()); return 0; }
CMD(c_tool_sound) { carts_sound_editor(console_framebuffer()); return 0; }
CMD(c_tool_studio) { carts_studio(console_framebuffer()); return 0; }
CMD(c_tool_mesh) { carts_mesh(console_framebuffer()); return 0; }
CMD(c_tool_pixel) { carts_pixel(console_framebuffer()); return 0; }
CMD(c_tool_animator) { carts_animator(console_framebuffer()); return 0; }
CMD(c_tool_code) { carts_code(console_framebuffer(), NULL); return 0; }
CMD(c_tool_assistant) { home_assistant(console_framebuffer()); return 0; }

CMD(c_bt_pad) { bt_scan(8); return 0; }
CMD(c_bt_kbd) { bt_pair_keyboard(15); return 0; }
CMD(c_bt_mouse) { bt_pair_mouse(10); return 0; }

CMD(c_bt_forget)
{
    int yes = argc > 1 && !strcmp(argv[1], "yes");
    if (!yes) {
        kprintf("forget all Bluetooth pads, the keyboard and the mouse (keys removed from\n"
                "bm/config.txt)? y = yes\n");
        input_flush();                          /* only a key pressed after the question */
        char k = input_getc();
        yes = k == 'y' || k == 'Y';
    }
    if (yes) {
        int n = bt_forget_all();
        kprintf("bt: %d device%s forgotten; pair again with T (DS4: Share + PS),\n"
                "    K (keyboard) or O (mouse)\n", n, n == 1 ? "" : "s");
    } else {
        kprintf("cancelled\n");
    }
    return 0;
}

/* W: start the radio, scan, join the saved network (or ask which) and start the stack */
static int wifi_flow(int save)
{
    wifi_auto_stop();                           /* a try of the menu's left paused: not two at once */
    if (wifi_start() == 0 && wifi_scan() > 0 && wifi_connect() == 0) {
        if (save)
            config_save();
        if (net_start(&net_wifi) == 0)
            net_wait_ip(15000);
        return 0;
    }
    return -1;
}

CMD(c_wifi)
{
    wifi_flow(0);
    return 0;
}

CMD(c_wifi_list)
{
    wifi_auto_stop();
    if (wifi_start() != 0)
        return -1;
    return wifi_scan() < 0 ? -1 : 0;
}

/* wifi join NAME [PASSWORD]: the network is saved in bm/config.txt once it is joined. A
 * network that is not in range makes wifi_connect ask for a number, as W does. */
CMD(c_wifi_join)
{
    char old[3][128];
    const char *const keys[3] = { "wifi_ssid", "wifi_psk", "wifi_security" };
    int had[3];

    if (argc < 2) {
        kprintf("wifi join NAME [PASSWORD]\n");
        return -1;
    }
    for (int i = 0; i < 3; i++) {
        const char *v = config_get(keys[i]);
        had[i] = v != NULL;
        snprintf(old[i], sizeof old[i], "%s", v ? v : "");
    }
    config_set("wifi_ssid", argv[1]);
    config_set("wifi_psk", argc > 2 ? argv[2] : "");
    config_unset("wifi_security");
    if (wifi_flow(1) == 0)
        return 0;
    for (int i = 0; i < 3; i++) {               /* not joined: the network saved before stays */
        if (had[i])
            config_set(keys[i], old[i]);
        else
            config_unset(keys[i]);
    }
    return -1;
}

CMD(c_wifi_forget)
{
    config_unset("wifi_ssid");
    config_unset("wifi_psk");
    config_unset("wifi_security");
    config_save();
    kprintf("wifi: the saved network is forgotten (bm/config.txt)\n");
    return 0;
}

CMD(c_net_status)
{
    kprintf("net: wifi %s, ethernet %s, IP %s, time %s\n",
            wifi_linked() ? "linked" : wifi_up() ? "up, not joined" : "off",
            eth_present() ? "present" : "none", net_ip_text(), net_time_text());
    return 0;
}

CMD(c_net_eth)
{
    eth_diag();
    if (eth_present())
        kprintf("net: IP %s, time %s\n", net_ip_text(), net_time_text());
    return 0;
}

CMD(c_net_get) { return net_get_test(argc > 1 ? argv[1] : NULL); }

/* j: the 3D Bench with its pages. In a line: all or a part (tests=a,b profiles=P), no waiting on pages */
CMD(c_bench_3d)
{
    static char tests[160], profiles[160];
    const char *t = NULL, *p = NULL;

    if (from_key) {
        bm_bench3d(console_framebuffer());
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "tests=", 6)) {
            snprintf(tests, sizeof tests, "%s", argv[i] + 6);
            t = tests;
        } else if (!strncmp(argv[i], "profiles=", 9)) {
            snprintf(profiles, sizeof profiles, "%s", argv[i] + 9);
            p = profiles;
        } else {
            kprintf("b3d: '%s' is not tests=... or profiles=... (left out)\n", argv[i]);
        }
    }
    if (bm_bench3d_part(console_framebuffer(), t, p, 1) != 0) {
        kprintf("b3d: stopped; the rest of the line is not run\n");
        return -1;
    }
    return 0;
}


CMD(c_report_send)
{
    const int left = reports_send_pending();
    kprintf("%s; %d waiting on the SD card\n", reports_last(), left);
    return 0;
}

CMD(c_report_log)
{
    reports_text("log", klog_text(), strlen(klog_text()));
    return 0;
}

CMD(c_update) { update_monitor(); return 0; }

/* the keys of bm/config.txt that Tab completes after "set" (docs/spec/config-keys.toml; a key
 * that is not here is set all the same) */
static const char *const set_keys[] = {
    "assist_lang=", "battery_icon=", "battery_log=","bm_scale=", "bm_smooth=", "confirm=", "controls=", "dev=",
    "dma_zclear=", "draw=", "game_buttons=", "game_intro=", "gpu3d=", "gpu3d_2d=", "gpu3d_aa=",
    "gpu3d_attr=", "gpu3d_etc1=", "gpu3d_filter=", "gpu3d_fs2=", "gpu3d_later=", "gpu3d_merge=",
    "gpu3d_qpu=", "gpu3d_queue=", "gpu3d_sort=", "gpu3d_tex16=", "gpu3d_vs=", "gpu3d_wc=", "layout=",
    "mali3d=", "market_delay=", "menu_scale=", "monitor_keys=", "mouse=", "name=", "perf=",
    "prompts=", "r3d_fast=", "r3d_impostor=", "rec=", "rec_fps=", "rec_format=", "report_upload=",
    "session_report=", "sound=", "sound_depth=", "v3d_clock=", "volume=",
    "web_max_kib=", "wifi_boot=",
};

static const char *cand_set(int i)
{
    return i >= 0 && i < (int)(sizeof set_keys / sizeof set_keys[0]) ? set_keys[i] : NULL;
}

static const char *cand_help(int i)
{
    for (const mcmd_t *r = cmd_table(); r->name; r++)
        if (i-- == 0)
            return r->name;
    return NULL;
}

/* key=value ... until a restart (save keeps it); no words: the settings as they are */
CMD(c_set)
{
    if (argc < 2) {
        static char list[2048];
        config_list(list, sizeof list);
        kprintf("%s", list);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *w = argv[i];
        char *eq = strchr(w, '=');
        if (eq && eq > w) {
            *eq = 0;
            config_set(w, eq + 1);
            kprintf("set %s=%s (until a restart; save keeps it)\n", w, eq + 1);
        } else {
            kprintf("set: '%s' is not key=value\n", w);
            return -1;
        }
    }
    return 0;
}

CMD(c_save)
{
    config_save();
    kprintf("bm/config.txt saved\n");
    return 0;
}

/* a game by name, after the set of its flags (overbit_bench=...) */
CMD(c_play)
{
    char name[96];
    int n = 0;
    for (int i = 1; i < argc; i++)
        n += snprintf(name + n, sizeof name - (size_t)n, "%s%s", i > 1 ? " " : "", argv[i]);
    if (argc < 2 || carts_play_title(console_framebuffer(), name) != 0) {
        kprintf("play: no game '%s' on this console\n", argc < 2 ? "" : name);
        return -1;
    }
    return 0;
}

CMD(c_err)
{
    const char *e = bm_last_error();
    if (e && *e)
        kprintf("err: %s\n", e);
    else
        kprintf("err: none\n");
    char title[64];
    if (bm_suspended(title, sizeof title) && title[0])
        kprintf("running: %s\n", title);
    else
        kprintf("running: menu\n");
    return 0;
}

/* the CRC32 of the page on screen (zlib.crc32 of its bytes): a golden to keep from the
 * same Pi, compared by the PC (R-NET7) */
CMD(c_crc)
{
    const framebuffer_t *f = console_framebuffer();
    const uint8_t *page = f->mem + (size_t)f->shown * f->height * f->pitch;
    kprintf("crc: %lux%lu bpp=%lu pitch=%lu crc32=0x%08lx\n", (unsigned long)f->width,
            (unsigned long)f->height, (unsigned long)f->depth, (unsigned long)f->pitch,
            (unsigned long)crc32(page, f->height * f->pitch));
    return 0;
}

CMD(c_ls) { return mfs_ls(argc, argv); }
CMD(c_cd) { return mfs_cd(argc, argv); }
CMD(c_pwd) { return mfs_pwd(argc, argv); }
CMD(c_cat) { return mfs_cat(argc, argv); }
CMD(c_cp) { return mfs_cp(argc, argv); }
CMD(c_rm) { return mfs_rm(argc, argv); }
CMD(c_mkdir) { return mfs_mkdir(argc, argv); }

CMD(c_exit)
{
    want_exit = 1;
    return 0;
}

/* single keys run commands (the key loop); Enter or Esc there goes back to the prompt */
static int hot;                         /* 1: the key loop; 0: the bm> prompt */
static int hot_sticky;                  /* no monitor_keys=0: the key loop is the way in */
CMD(c_hotkey)
{
    hot = 1;
    want_exit = 1;                      /* the prompt ends; the key loop runs */
    kprintf("hotkeys on: a key runs its command; Enter or Esc: back to the prompt\n");
    return 0;
}

/* ------------------------------------------------------------------ table */

static const mcmd_t carts_sub[] = {
    { .name = "list", .help = "list the cartridges", .key = 'f', .fn = c_carts_list },
    { .name = "rescan", .help = "re-read the SD card", .key = 'F', .fn = c_carts_rescan },
    { NULL }
};

static const mcmd_t sys_sub[] = {
    { .name = "info", .help = "system information", .key = 'i', .fn = c_info },
    { .name = "heap", .help = "heap usage", .key = 'm', .fn = c_heap },
    { .name = "clear", .help = "clear the screen", .key = 'c', .fn = c_clear },
    { .name = "dmesg", .help = "everything printed since boot (pager)", .key = 'o', .fn = c_dmesg },
    { .name = "usb", .help = "USB: scan the port and the hub", .key = 'y', .fn = c_usb },
    { .name = "input", .help = "input test: USB, then each player (10 s)", .key = 'Y', .fn = c_input },
    { .name = "layout", .args = "[it|us]", .help = "keyboard layout (no word: the other one)", .key = 'L', .fn = c_layout },
    { .name = "via-ram", .args = "[on|off]", .help = ".bm drawing: via RAM / direct", .key = 'V', .fn = c_via_ram },
    { .name = "audio", .help = "HDMI sound status and a test tune", .key = 'a', .fn = c_audio },
    { .name = "test-pattern", .help = "HDMI test pattern (any key returns)", .key = 't', .fn = c_pattern },
    { .name = "crash", .args = "[KIND]", .help = "crash test: undef dabt pabt svc freeze ...", .key = 'X', .fn = c_crash },
    { .name = "reboot", .help = "reboot (the chainloader asks for a kernel)", .key = 'r', .fn = c_reboot },
    { NULL }
};

static const mcmd_t tool_sub[] = {
    { .name = "editor", .help = "editor: code, sprites and map of a .bm", .key = 'e', .fn = c_tool_editor },
    { .name = "sound", .help = "sound editor: sounds, effects and music", .key = 'A', .fn = c_tool_sound },
    { .name = "studio", .help = "bm Studio: the 3D models of a .bm", .key = '3', .fn = c_tool_studio },
    { .name = "mesh", .help = "bm Mesh: the meshes of a .bm to edit", .key = '4', .fn = c_tool_mesh },
    { .name = "pixel", .help = "bm Pixel: sprite sheet, palette, animation", .key = '5', .fn = c_tool_pixel },
    { .name = "animator", .help = "bm Animator: skeletons, animations", .key = '6', .fn = c_tool_animator },
    { .name = "code", .help = "bm Code: the code editor (tabs, small font)", .key = 'C', .fn = c_tool_code },
    { .name = "assistant", .help = "assistant: how to write code (F6 in tools)", .key = 'I', .fn = c_tool_assistant },
    { NULL }
};

static const mcmd_t bt_pair_sub[] = {
    { .name = "pad", .help = "pair a controller (DS4: Share + PS)", .key = 'T', .fn = c_bt_pad },
    { .name = "kbd", .help = "pair a LE keyboard (hold Easy-Switch)", .key = 'K', .fn = c_bt_kbd },
    { .name = "mouse", .help = "pair a mouse (LE or classic, no code)", .key = 'O', .fn = c_bt_mouse },
    { NULL }
};

static const mcmd_t bt_sub[] = {
    { .name = "pair", .help = "pair a device", .sub = bt_pair_sub },
    { .name = "forget", .args = "[yes]", .help = "forget all pads, keyboard, mouse", .key = 'P', .fn = c_bt_forget },
    { NULL }
};

static const mcmd_t wifi_sub[] = {
    { .name = "list", .help = "start the radio, list the networks", .fn = c_wifi_list },
    { .name = "join", .args = "NAME [PASSWORD]", .help = "join a network, save it", .fn = c_wifi_join },
    { .name = "forget", .help = "forget the saved network", .fn = c_wifi_forget },
    { NULL }
};

static const mcmd_t net_sub[] = {
    { .name = "status", .help = "WiFi, Ethernet, IP address, time", .fn = c_net_status },
    { .name = "eth", .help = "Ethernet (Pi 1 B / B+): link, counters", .key = 'E', .fn = c_net_eth },
    { .name = "get", .args = "URL", .help = "get a web address: status, size, speed", .key = 'G', .fn = c_net_get },
    { NULL }
};

static const mcmd_t bench_sub[] = {
    { .name = "3d", .args = "[tests=A,B profiles=P]", .help = "3D Bench, bars and a saved report", .key = 'j', .fn = c_bench_3d },
    { NULL }
};

static const mcmd_t report_sub[] = {
    { .name = "send", .help = "send the reports in bm/reports", .key = 'z', .fn = c_report_send },
    { .name = "log", .help = "the log since boot as a report", .key = 'Z', .fn = c_report_log },
    { NULL }
};

static const mcmd_t commands[] = {
    { .name = "help", .args = "[COMMAND]", .help = "this list, or one command", .key = 'h', .fn = c_help, .cand = cand_help },
    { .name = "menu", .help = "cartridge menu, or PS on a pad", .key = 'M', .fn = c_menu },
    { .name = "carts", .help = "the cartridges (SD: / and /bm)", .sub = carts_sub },
    { .name = "lua", .help = "Lua REPL (Esc, Ctrl-D, exit() return)", .key = 'l', .fn = c_lua },
    { .name = "upload", .help = "receive a cartridge over serial, play it", .key = 'U', .fn = c_upload },
    { .name = "sys", .help = "the system", .sub = sys_sub },
    { .name = "tool", .help = "the tools of a .bm cartridge", .sub = tool_sub },
    { .name = "bt", .help = "Bluetooth", .sub = bt_sub },
    { .name = "wifi", .help = "start, scan, join (saved in config.txt)", .key = 'W', .fn = c_wifi, .sub = wifi_sub },
    { .name = "net", .help = "the network", .sub = net_sub },
    { .name = "bench", .help = "benchmarks", .sub = bench_sub },
    { .name = "report", .help = "the reports of the tests", .sub = report_sub },
    { .name = "update", .help = "update from GitHub (asks first)", .key = 'u', .fn = c_update },
    { .name = "set", .args = "[KEY=VALUE ...]", .help = "bm/config.txt keys until a restart", .fn = c_set, .cand = cand_set },
    { .name = "save", .help = "write bm/config.txt", .fn = c_save },
    { .name = "play", .args = "NAME", .help = "play a game by its title", .fn = c_play },
    { .name = "err", .help = "the last runtime error, the game running", .fn = c_err },
    { .name = "crc", .help = "CRC32 of the page on screen", .fn = c_crc },
    { .name = "ls", .args = "[PATH]", .help = "list a folder of the SD card", .fn = c_ls },
    { .name = "cd", .args = "[PATH]", .help = "go to a folder (no path: the root)", .fn = c_cd },
    { .name = "pwd", .help = "the current folder", .fn = c_pwd },
    { .name = "cat", .args = "FILE", .help = "show a text file (the first 16 KB)", .fn = c_cat },
    { .name = "cp", .args = "FILE TO", .help = "copy a file (a new name is 8.3)", .fn = c_cp },
    { .name = "rm", .args = "FILE", .help = "remove a file", .fn = c_rm },
    { .name = "mkdir", .args = "PATH", .help = "make a folder (8.3 names)", .fn = c_mkdir },
    { .name = "exit", .help = "leave the bm> prompt (to the menu)", .fn = c_exit },
    { .name = "hotkey", .help = "single keys run commands (the keys of the first column of help)", .fn = c_hotkey },
    /* the words of before: same syntax, same output */
    { .name = "b3d", .alias = "bench 3d" },
    { .name = "log", .alias = "report log" },
    { .name = "send", .alias = "report send" },
    { .name = "reboot", .alias = "sys reboot" },
    { .name = "get", .alias = "net get" },
    { NULL }
};

static const mcmd_t *cmd_table(void)
{
    return commands;
}

/* --------------------------------------------------------------- the line */

static medit_t ed;                      /* one editor: the history lasts until the reboot */
static int ed_ready;

static void con_out(const char *s)
{
    kprintf("%s", s);
}

/* a key from the keyboard, the serial line or the network console, as the editor reads it.
 * An ESC with nothing behind it for 100 ms is the Esc key (a terminal's arrow keys send the
 * rest at once); the USB keyboard gives its arrows as HID_KEY_*. */
#define EK_HOME (-2)                    /* the PS button */
#define EK_PLAY (-3)                    /* a cartridge arrived over the network */

static int edit_key(void)
{
    int c;
    if (medit_in_escape(&ed)) {
        uint32_t t0 = timer_ticks();
        while ((c = input_key()) < 0)
            if (timer_ticks() - t0 > 100000)
                return MEDIT_K_ESC;
    } else {
        c = input_getc_home();
    }
    if (c == INPUT_HOME)
        return EK_HOME;
    if (c == INPUT_NET_PLAY)
        return EK_PLAY;
    switch (c) {
    case HID_KEY_UP: return MEDIT_K_UP;
    case HID_KEY_DOWN: return MEDIT_K_DOWN;
    case HID_KEY_LEFT: return MEDIT_K_LEFT;
    case HID_KEY_RIGHT: return MEDIT_K_RIGHT;
    case HID_KEY_HOME: return MEDIT_K_HOME;
    case HID_KEY_END: return MEDIT_K_END;
    case HID_KEY_DEL: return MEDIT_K_DEL;
    default: return c < 0x80 ? c : 0;
    }
}

static void net_play(void)              /* bm_net.py --play */
{
    uint8_t *buf;
    size_t len;
    if (netxfer_take_play(&buf, &len)) {
        carts_play_buffer(console_framebuffer(), buf, len);
        free(buf);
    }
}

/* One edited line after the prompt: MEDIT_LINE (the text is in the editor), MEDIT_CANCEL,
 * MEDIT_EXIT, or EK_HOME */
static int edit_read(const char *prompt)
{
    if (!ed_ready) {
        medit_init(&ed, commands, con_out);
        ed_ready = 1;
    }
    medit_begin(&ed, prompt, 1);
    hid_text_mode(1);                           /* a USB keyboard's arrows, Home, End, Del as keys */
    for (;;) {
        int k = edit_key();
        if (k == EK_HOME) {
            kprintf("\n");
            hid_text_mode(0);
            return EK_HOME;
        }
        if (k == EK_PLAY) {
            hid_text_mode(0);
            net_play();
            hid_text_mode(1);
            medit_redraw(&ed);
            continue;
        }
        int r = medit_feed(&ed, k);
        if (r != MEDIT_NONE) {
            hid_text_mode(0);
            return r;
        }
    }
}

/* ':' a line of commands for the tests (2026-10-05: one string sent to the user instead of
 * many settings): ';' between commands; each says what it does, a word not known stops the
 * line. quiet: at the bm> prompt, where the line is on the screen already */
static int quiet_line;

static void line_before(int argc, char **argv)
{
    if (quiet_line)
        return;
    kprintf("\x1b[96m:");
    for (int i = 0; i < argc; i++)
        kprintf(" %s", argv[i]);
    kprintf("\x1b[0m\n");
}

static void line_error(const char *msg)
{
    kprintf("%s\n", msg);
}

static void line_run(const char *text, int quiet)
{
    static char line[MEDIT_LINE_MAX];
    static const mcmd_hooks_t hooks = { line_before, line_error };
    snprintf(line, sizeof line, "%s", text);
    crumb("monitor line", line);
    quiet_line = quiet;
    mcmd_run_line(commands, line, &hooks);
    quiet_line = 0;
}

/* the bm> prompt: lines until 'exit', Esc or Ctrl-D. 1 if the PS button asked for the menu */
static int prompt_mode(void)
{
    int home = 0;
    want_exit = 0;
    kprintf("the prompt stays: 'help' lists the commands; Tab completes, Up repeats, 'exit' or Esc leaves\n");
    for (;;) {
        console_suspend(0);                     /* (a stopped test left it hidden: bm_video_leave) */
        netcon_focus(MON_FOCUS, "bm> ");
        crumb("monitor, waiting for a line", NULL);
        int r = edit_read("bm> ");
        if (r == EK_HOME) {
            home = 1;
            break;
        }
        if (r == MEDIT_EXIT)
            break;
        if (r == MEDIT_CANCEL || !medit_text(&ed)[strspn(medit_text(&ed), " \t")])
            continue;
        line_run(medit_text(&ed), 1);
        if (want_exit)
            break;
    }
    want_exit = 0;
    netcon_focus(MON_FOCUS, "> ");
    return home;
}

/* ':' */
static int line_commands(void)
{
    int r = edit_read(":");
    if (r == EK_HOME)
        return 1;
    if (r != MEDIT_LINE)
        return 0;
    if (!medit_text(&ed)[strspn(medit_text(&ed), " \t")])
        return prompt_mode();                   /* ':' and Enter: the prompt stays */
    line_run(medit_text(&ed), 0);
    want_exit = 0;                              /* 'exit' in a one-shot line: nothing to leave */
    kprintf("the line is done\n");
    return 0;
}

/* a single key: its row of the table */
static int run_key(int c)
{
    char path[48], *av[2];
    const mcmd_t *r = mcmd_find_key(commands, c, path, sizeof path);
    if (!r || !r->fn)
        return 0;
    av[0] = (char *)r->name;
    av[1] = NULL;
    from_key = 1;
    r->fn(1, av);
    from_key = 0;
    return 1;
}

void monitor_run(void)
{
    const char *mk = config_get("monitor_keys");
    hot = hot_sticky = !(mk && mk[0] == '0');      /* monitor_keys=0: the bm> prompt is the way in */
    if (hot)
        kprintf("\ntype 'h' for help\n");
    else
        kprintf("\nthe bm> prompt: 'help' lists the commands, 'hotkey' makes a key run one, 'exit' leaves\n");

    for (;;) {
        if (!hot) {                             /* the bm> prompt is the way in */
            prompt_mode();
            if (!hot)
                carts_menu(console_framebuffer());      /* exit, Esc, Ctrl-D or PS: the menu */
            continue;
        }
        console_suspend(0);                     /* (a stopped test left it hidden: bm_video_leave) */
        netcon_focus(MON_FOCUS, "> ");
        kprintf("> ");
        crumb("monitor, waiting for a key", NULL);
        int c = input_getc_home();              /* PS on a controller: INPUT_HOME */
        if (c == 0x1B) {                /* a terminal's arrow key: not a command */
            if (input_skip_sequence()) {
                kprintf("\r");
                continue;
            }
        }
        if (c >= ' ' && c < 127)
            kprintf("%c", c);
        kprintf("\n");
        char cmd[2] = { c >= ' ' && c < 127 ? c : '?', 0 };
        crumb("monitor command", cmd);

        if (c == INPUT_HOME)
            c = 'M';
        else if (c == '?')
            c = 'h';
        else if (c == 'J')
            c = 'j';
        switch (c) {
        case '\r': case '\n':
        case 0x1B:                      /* Esc alone: already at the monitor; with the prompt as the way in, back to it */
            if (!hot_sticky)
                hot = 0;
            break;
        case ':':
            if (line_commands())
                carts_menu(console_framebuffer());
            break;
        case INPUT_NET_PLAY: net_play(); break;
        default:
            if (!run_key(c))
                kprintf("unknown command (0x%02x), 'h' for help\n", (unsigned char)c);
        }
    }
}
