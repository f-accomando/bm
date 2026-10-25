/*
 * api_perm.c: permissions (what a game may do out of the console: the questions of the system over a game).
 * Moved from runtime.c (docs/NEXT.md, item 0b step 5). It shares the run
 * state through rt_internal.h.
 */
#ifdef BM_RGB30
#include "display.h"            /* fb_init_game */
#endif
#include "runtime.h"
#include "bm.h"
#include "gfx16.h"
#include "r3d.h"
#include "world3d.h"
#include "map3d.h"
#include "decimate.h"
#include "glb.h"
#include "cutout.h"
#include "platform.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "fs/fat.h"
#include "fs/sdlock.h"
#include "vid.h"
#include "rec.h"
#include "lib/crc32.h"
#include "gfx/console.h"
#include "gfx/font.h"
#include "lib/printf.h"
#include "script/luavm.h"
#include "loading.h"
#include "audio/audio.h"
#include "audio/player.h"
#include "audio/synth.h"
#include "audio/presets.h"
#include "audio/lua_tone.h"
#include "drivers/dma.h"
#include "gpu/gpu3d.h"
#include "gpu/version3d.h"
#include "arch/cache.h"
#include "n8lua.h"
#include "shot.h"
#include "webfile.h"
#include "webget.h"
#include "webimg.h"
#include "webpolicy.h"
#include "require.h"
#include "meshcap.h"
#include "tokens.h"
#include "profile.h"
#include "project.h"
#include "rt_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

/* ---------------------------------------------------------------- permissions
 * What a game may do out of the console (decision of the user, 2026-10-05):
 * the network (online games: any address, the internet too) and the reports
 * (sent to GitHub with the console's github_token). The first time a game
 * asks for one, the game stops and the player answers; the answer stays in
 * bm/config.txt, allow_<its save name>=net=yes,report=no (Title): remove
 * the line to be asked again. bm's own tools are not asked, nor is anyone
 * when the kernel has not turned the question on (bmhost, the tests on the
 * PC: as before). */
static int perm_ask;                    /* the kernel asks (bm_permissions) */

void bm_permissions(int on) { perm_ask = on; }

static const char *const perm_names[PERM_COUNT] = { "net=", "report=", "docs=" };

static void perm_key(char *key, size_t n)
{
    char stem[16];
    ksnprintf(stem, sizeof stem, "%s", rt.save_name);
    char *dot = strchr(stem, '.');
    if (dot)
        *dot = 0;
    for (char *c = stem; *c; c++)               /* the key keeps its capitals: cards of before */
        if (*c >= 'a' && *c <= 'z')
            *c = (char)(*c - 32);
    ksnprintf(key, n, "allow_%s", stem);
}

/* the answer kept: 1 yes, 0 no, -1 never asked */
static int perm_get(int what)
{
    char key[32];
    perm_key(key, sizeof key);
    const char *v = config_get(key), *p = v ? strstr(v, perm_names[what]) : NULL;
    if (!p)
        return -1;
    return strncmp(p + strlen(perm_names[what]), "yes", 3) == 0;
}

static void perm_set(int what, int yes)
{
    char key[32], v[120];
    perm_key(key, sizeof key);
    int o = 0;
    v[0] = 0;
    for (int p = 0; p < PERM_COUNT; p++) {          /* "net=yes,report=no,docs=yes (title)" */
        const int a = p == what ? yes : perm_get(p);
        if (a >= 0)
            o += ksnprintf(v + o, sizeof v - (size_t)o, "%s%s%s", o ? "," : "", perm_names[p], a ? "yes" : "no");
    }
    ksnprintf(v + o, sizeof v - (size_t)o, " (%s)", perm_title);
    config_set(key, v);
    config_save();
}

/* A question of the system over the game, stopped in its call (a
 * permission, the editable copy of a game): the lines in a box, the first
 * the title and the last one dim, with the hints of the yes and of the back
 * in their words. 1 yes, 0 no. */
static int sys_ask(const char *const *lines, int n, const char *yes, const char *no)
{
    later_flush();                          /* the frame held first: the question turns the pages */
    sync3d();                               /* nothing of the GPU's left to land on the page */
    static const uint8_t usage[4] = { 0x28, 0x2C, 0x29, 0x2A };   /* Enter, Space; Esc, Backspace */
    uint32_t prev = ~0u;                    /* what is held now is not an answer */
    uint8_t kprev = 0xFF;
    int answer = 0;
    rt.leave_ask = 1;                       /* Esc: no (poll_keys), not Start */
    rt.leave_no = 0;
    const int text = rt.text_mode;          /* an app that types (keyp): the serial keys */
    rt.text_mode = 0;                       /* are buttons for the answer */
    while (!answer) {
        const int q = poll_keys();
        uint32_t held = rt.raw_all;
        for (int p = 0; p < INPUT_PLAYERS; p++)
            held |= rt.praw[p];
        uint8_t keys = 0;
        for (int i = 0; i < 4; i++)
            if (hid_usage_held(usage[i]))
                keys |= (uint8_t)(1u << i);
        const uint32_t hit = held & ~prev;
        const uint8_t khit = keys & (uint8_t)~kprev;
        if ((hit & input_ok_bit(0)) || (khit & 3))
            answer = 1;
        else if ((q & QUIT_FORCE) || rt.leave_no || (hit & input_ok_bit(1)) || (khit & 12))
            answer = -1;
        prev = held;
        kprev = keys;
        box_yes = yes;
        box_no = no;
        sys_box(lines, n, 1, 1, -1);
        if (perm_fb)
            bm_video_present(perm_fb, &rt.g);
        audio_idle();
        timer_delay_ms(16);
        rt.leave_held = held;               /* the button of the answer is not the game's */
    }
    rt.leave_ask = 0;
    rt.leave_no = 0;
    /* back in the game's call, in the middle of its frame: the polls of the
     * question moved the buttons, so the rest of the frame sees them held
     * as they are, without the answer's and with nothing pressed */
    {
        const uint32_t m = rt.leave_held;
        rt.now = rt.prev = hid_to_btn(rt.raw_all & ~m);
        for (int p = 0; p < INPUT_PLAYERS; p++) {
            rt.praw[p] &= ~m;
            rt.praw_prev[p] = rt.praw[p];
            rt.pnow[p] = rt.pprev[p] = hid_to_btn(rt.praw[p]);
        }
        rt.ptr.pressed = rt.ptr.released = 0;
    }
    rt.text_mode = text;
    if (text)
        while (hid_getc() >= 0)
            ;                               /* Enter or Esc of the answer: not typed for the app */
    return answer > 0;
}

/* The question of a permission: 1 allowed */
static int perm_question(int what)
{
    const char *lines[5];
    lines[0] = perm_title[0] ? perm_title : "This game";
    if (what == PERM_NET) {
        lines[1] = "wants to use the network:";
        lines[2] = "online games, with other consoles";
        lines[3] = "or a server on the internet.";
    } else if (what == PERM_DOCS) {
        lines[1] = "wants to read and write your";
        lines[2] = "documents: the files in /docs on";
        lines[3] = "the SD card, shared by the apps.";
    } else {
        lines[1] = "wants to send a report to your";
        lines[2] = "GitHub repository, with the";
        lines[3] = "console's github_token.";
    }
    lines[4] = "The answer stays in bm/config.txt";
    kprintf("bm: %s %s (ok: allow, back: no)\n", lines[0],
            what == PERM_NET ? "asks for the network" : what == PERM_DOCS ? "asks for the documents"
                                                                         : "asks to send a report");
    const int answer = sys_ask(lines, 5, "Allow", "No");
    kprintf("bm: %s\n", answer ? "allowed" : "not allowed");
    return answer;
}

int perm_allowed(int what)
{
    if (tool_mode || !perm_ask)
        return 1;
    int s = perm_get(what);
    if (s < 0) {
        s = perm_question(what);
        perm_set(what, s);
    }
    return s;
}

/* A tool saving a game (a .bm): games are read only (project.h), so the
 * question of its editable copy, a project in Dev. 1 yes */
int copy_question(const char *game, const char *copy)
{
    const char *g = strrchr(game, '/');
    char title[48], last[64];
    ksnprintf(title, sizeof title, "%s is a game", g ? g + 1 : game);
    ksnprintf(last, sizeof last, "Save an editable copy: %s?", copy);     /* with its folder: /bme */
    const char *lines[5] = { title, "A game is read only: the tools", "read it and take its parts, they",
                             "do not change it.", last };
    kprintf("bm: %s is read only: save an editable copy as %s? (ok: copy, back: no)\n", game, copy);
    const int answer = sys_ask(lines, 5, "Copy", "No");
    kprintf("bm: %s\n", answer ? "copied" : "not copied");
    return answer;
}
