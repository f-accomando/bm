/*
 * The system's keys (syskeys.h). Portable.
 */
#include "syskeys.h"

#include <string.h>

static const syskey_t keys[] = {
    { "f12", "hold: the keys" },
    { "esc", "menu, or back" },
    { "ctrl esc", "back to bm's menu (PS)" },
    { "ctrl shift esc", "the monitor" },
    { "f1 - f4", "the pages" },
    { "f5 / ctrl r", "try the game" },
    { "f6", "the assistant" },
    { "f11", "performance overlay" },
    { "ctrl s", "save" },
    { "ctrl shift s", "save as" },
    { "ctrl o", "open" },
    { "ctrl n", "new" },
    { "ctrl z", "undo" },
    { "ctrl y", "redo" },
    { "ctrl x", "cut" },
    { "ctrl c", "copy" },
    { "ctrl v", "paste" },
    { "ctrl f", "find" },
};

int syskeys_count(void)
{
    return (int)(sizeof keys / sizeof keys[0]);
}

const syskey_t *syskey(int i)
{
    return i >= 0 && i < syskeys_count() ? &keys[i] : NULL;
}

/* one alternative of an entry ("ctrl r") equal to keys, spaces aside */
static int same(const char *a, size_t an, const char *b)
{
    while (an && *a == ' ') a++, an--;
    while (an && a[an - 1] == ' ') an--;
    while (*b == ' ') b++;
    size_t bn = strlen(b);
    while (bn && b[bn - 1] == ' ') bn--;
    if (an != bn)
        return 0;
    for (size_t i = 0; i < an; i++) {
        char x = a[i] >= 'A' && a[i] <= 'Z' ? (char)(a[i] + 32) : a[i];
        char y = b[i] >= 'A' && b[i] <= 'Z' ? (char)(b[i] + 32) : b[i];
        if (x != y)
            return 0;
    }
    return 1;
}

int syskeys_reserved(const char *k)
{
    for (int i = 0; i < syskeys_count(); i++) {
        const char *s = keys[i].keys;
        if (strstr(s, " - "))                   /* the pages: the tools name them */
            continue;
        for (;;) {
            const char *bar = strstr(s, " / ");
            size_t n = bar ? (size_t)(bar - s) : strlen(s);
            if (same(s, n, k))
                return 1;
            if (!bar)
                break;
            s = bar + 3;
        }
    }
    return 0;
}
