/*
 * The game library shared by the Pi menu (carts.c) and the RGB30 menu (src/rgb30/ui.c).
 * See library.h.
 */
#include "library.h"

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

int lib_text_cmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        const int x = lower((unsigned char)*a), y = lower((unsigned char)*b);
        if (x != y || !x)
            return x - y;
    }
}

int lib_same_text(const char *a, const char *b)
{
    return lib_text_cmp(a, b) == 0;
}
