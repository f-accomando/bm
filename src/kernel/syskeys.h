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

#endif
