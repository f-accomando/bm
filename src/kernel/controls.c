/* The player's remap of the game buttons: see controls.h. */
#include "kernel/controls.h"

#include <string.h>

#include "kernel/config.h"

static const struct {
    const char *code, *scheme, *action, *label, *def;
} act[CONTROLS_COUNT] = {
    { "c1", "scroller", "act1", "Action 1 (jump)", "b" },
    { "c2", "scroller", "act2", "Action 2 (attack)", "y" },
    { "c3", "scroller", "act3", "Action 3", "a" },
    { "c4", "scroller", "act4", "Action 4 (menu)", "x" },
    { "t1", "topdown", "act1", "Action 1 (attack)", "b" },
    { "t2", "topdown", "act2", "Action 2 (dodge)", "a" },
    { "t3", "topdown", "act3", "Action 3", "y" },
    { "t4", "topdown", "act4", "Action 4 (menu)", "x" },
    { "s1", "stick", "act1", "Action 1 (fire)", "r2" },
    { "s2", "stick", "act2", "Action 2", "l2" },
    { "s3", "stick", "act3", "Action 3", "l1" },
    { "s4", "stick", "act4", "Action 4", "r1" },
    { "s5", "stick", "act5", "Action 5", "b" },
    { "s6", "stick", "act6", "Action 6", "a" },
    { "s7", "stick", "act7", "Action 7", "y" },
    { "s8", "stick", "act8", "Action 8", "x" },
    { "pa", "system", "pause", "Pause", "st" },
    { "mo", "system", "menu_ok", "OK", "b" },
    { "mc", "system", "menu_cancel", "Cancel", "y" },
};

static const char *const buttons[] = { "a", "b", "x", "y", "l1", "r1", "l2", "r2", "st", "se" };

int controls_count(void) { return CONTROLS_COUNT; }
const char *controls_code(int i) { return i >= 0 && i < CONTROLS_COUNT ? act[i].code : NULL; }
const char *controls_scheme(int i) { return i >= 0 && i < CONTROLS_COUNT ? act[i].scheme : NULL; }
const char *controls_label(int i) { return i >= 0 && i < CONTROLS_COUNT ? act[i].label : NULL; }
const char *controls_default(int i) { return i >= 0 && i < CONTROLS_COUNT ? act[i].def : NULL; }

int controls_valid_button(const char *b)
{
    for (unsigned k = 0; b && k < sizeof buttons / sizeof buttons[0]; k++)
        if (!strcmp(b, buttons[k]))
            return 1;
    return 0;
}

/* the pairs of the config value: for code i the button (without its end) into out */
static const char *pair(const char *v, const char *code, char out[4])
{
    const size_t n = strlen(code);
    while (v && *v) {
        while (*v == ' ')
            v++;
        const char *e = v;
        while (*e && *e != ' ')
            e++;
        if ((size_t)(e - v) > n && !strncmp(v, code, n) && v[n] == ':') {
            const size_t len = (size_t)(e - v) - n - 1;
            if (len < 4) {
                memcpy(out, v + n + 1, len);
                out[len] = 0;
                return out;
            }
        }
        v = e;
    }
    return NULL;
}

const char *controls_get(int i)
{
    static char b[4];
    if (i < 0 || i >= CONTROLS_COUNT)
        return NULL;
    const char *v = config_get("controls");
    if (!v || !pair(v, act[i].code, b) || !controls_valid_button(b))
        return NULL;                        /* a button that is not valid is ignored: the default */
    return b;
}

static void store(const char *buttons_of[CONTROLS_COUNT])
{
    char v[128];
    size_t n = 0;
    for (int i = 0; i < CONTROLS_COUNT; i++) {
        if (!buttons_of[i])
            continue;
        const size_t add = strlen(act[i].code) + 1 + strlen(buttons_of[i]) + (n ? 1 : 0);
        if (n + add >= sizeof v)
            break;
        if (n)
            v[n++] = ' ';
        memcpy(v + n, act[i].code, strlen(act[i].code));
        n += strlen(act[i].code);
        v[n++] = ':';
        memcpy(v + n, buttons_of[i], strlen(buttons_of[i]));
        n += strlen(buttons_of[i]);
    }
    v[n] = 0;
    if (n)
        config_set("controls", v);
    else
        config_unset("controls");
    config_save();
}

int controls_set(int i, const char *button)
{
    if (i < 0 || i >= CONTROLS_COUNT)
        return -1;
    if (button && !strcmp(button, "default"))
        button = NULL;
    if (button && !controls_valid_button(button))
        return -1;
    static char keep[CONTROLS_COUNT][4];
    const char *now[CONTROLS_COUNT];
    for (int k = 0; k < CONTROLS_COUNT; k++) {
        const char *g = controls_get(k);
        if (g) {
            strcpy(keep[k], g);
            now[k] = keep[k];
        } else {
            now[k] = NULL;
        }
    }
    if (button) {
        strcpy(keep[i], button);
        now[i] = keep[i];
    } else {
        now[i] = NULL;
    }
    store(now);
    return 0;
}

void controls_reset(void)
{
    config_unset("controls");
    config_save();
}

const char *controls_lookup(const char *scheme, const char *action)
{
    for (int i = 0; scheme && action && i < CONTROLS_COUNT; i++)
        if (!strcmp(act[i].scheme, scheme) && !strcmp(act[i].action, action))
            return controls_get(i);
    return NULL;
}
