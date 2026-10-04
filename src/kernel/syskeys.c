/*
 * The system's keys (syskeys.h). Portable.
 */
#include "syskeys.h"

#include <string.h>

static const syskey_t keys[] = {
    { "f12", "hold: the keys", 1 },
    { "esc", "menu, or back", 0 },
    { "ctrl esc", "back to bm's menu (PS)", 1 },
    { "ctrl shift esc", "the monitor", 1 },
    { "f1 - f4", "the pages", 0 },
    { "f5 / ctrl r", "try the game", 0 },
    { "f6", "the assistant", 0 },
    { "f11", "performance overlay", 1 },
    { "ctrl s", "save", 0 },
    { "ctrl shift s", "save as", 0 },
    { "ctrl o", "open", 0 },
    { "ctrl n", "new", 0 },
    { "ctrl z", "undo", 0 },
    { "ctrl y", "redo", 0 },
    { "ctrl x", "cut", 0 },
    { "ctrl c", "copy", 0 },
    { "ctrl v", "paste", 0 },
    { "ctrl f", "find", 0 },
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

/* keys (one alternative) equal to one of the kernel's own */
static int kernel_key(const char *k, size_t kn)
{
    char one[32];
    if (kn >= sizeof one)
        return 0;
    memcpy(one, k, kn);
    one[kn] = 0;
    for (int i = 0; i < syskeys_count(); i++) {
        if (!keys[i].kernel)
            continue;
        for (const char *s = keys[i].keys;;) {
            const char *bar = strstr(s, " / ");
            size_t n = bar ? (size_t)(bar - s) : strlen(s);
            if (same(s, n, one))
                return 1;
            if (!bar)
                break;
            s = bar + 3;
        }
    }
    return 0;
}

int syskeys_reserved(const char *k)
{
    for (;;) {
        const char *bar = strstr(k, " / ");
        if (kernel_key(k, bar ? (size_t)(bar - k) : strlen(k)))
            return 1;
        if (!bar)
            return 0;
        k = bar + 3;
    }
}
