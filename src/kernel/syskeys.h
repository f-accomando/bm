/*
 * The system's keys (the user's decision, 2026-10-04): the same in bm's
 * menu, in every tool and in every game. A tool's or a game's own keys
 * never use them for something else; F1-F4 are the pages, named by each
 * tool. The list is shown, with the keys' pictures, while F12 is held
 * (src/bm/runtime.c for the cartridges, src/kernel/menu_ui.c for the menu).
 * On a controller Esc is Start and Ctrl+Esc is PS.
 */
#ifndef SYSKEYS_H
#define SYSKEYS_H

typedef struct {
    const char *keys;           /* the keys' names (prompt()), a space between: "ctrl shift s";
                                 * "/" between alternatives, "-" a range ("f1 - f4") */
    const char *what;
    int kernel;                 /* 1: the kernel's own (F11, F12, Ctrl+Esc...), never the tool's;
                                 * 0: each tool does it, with this meaning (Esc, Ctrl+S...) */
} syskey_t;

/* the list, in the order it is shown */
int syskeys_count(void);
const syskey_t *syskey(int i);

/* 1 if one of keys ("f11", "ctrl c / ctrl esc") is a key the kernel keeps
 * for itself, that never reaches a tool: a tool's list of keys must not name
 * it. The others (Esc, Ctrl+S...) a tool lists when it says what they do there. */
int syskeys_reserved(const char *keys);

/* The rendering tests (3D Bench, Render bench, Stress test, Texture Room;
 * 2026-10-10) stop on the keys that leave a game: Start+Select (or
 * Ctrl+Shift+Esc), Ctrl+Esc, the PS button. No key of their own: these.
 * syskeys_test_begin() at the start of a test (reports_begin's callers and
 * the menu do it); syskeys_test_stop() reads the controls, once a frame or
 * a step, and says 1 from the press on; syskeys_test_stopped() says the same
 * without reading them (after the test: its report is dropped). A test that
 * runs a cartridge (bm_play) and sees it left says so with _set_stopped. */
void syskeys_test_begin(void);
int syskeys_test_stop(void);
int syskeys_test_stopped(void);
void syskeys_test_set_stopped(void);

#endif
