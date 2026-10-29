/* The AI settings in bm/ai.txt; see aiconf.h. */
#include "aiconf.h"

#include "fs/fat.h"
#include "fs/sdlock.h"
#include "kernel/config.h"
#include "lib/printf.h"

#include <stdlib.h>
#include <string.h>

#define VAL 128
static const char *const names[3] = { "url", "model", "key" };
static char vals[3][VAL];
static int loaded;

static int slot(const char *name)
{
    for (int i = 0; i < 3; i++)
        if (!strcmp(name, names[i]))
            return i;
    return -1;
}

static int save(void)
{
    char buf[3 * (VAL + 8) + 96];
    int n = ksnprintf(buf, sizeof buf, "# bm AI settings (bm Agent: /setup). Not for cartridges; see bm/config.txt for the system.\n");
    for (int i = 0; i < 3; i++)
        if (vals[i][0])
            n += ksnprintf(buf + n, sizeof buf - (size_t)n, "%s=%s\n", names[i], vals[i]);
    const int r = sd_write_file("ai", "/bm", AI_CONF_FILE, buf, (size_t)n);
    for (volatile char *p = buf; p < buf + sizeof buf; p++)
        *p = 0;                                     /* the key leaves no copy on the stack */
    return r ? -1 : 0;
}

/* the first version kept them in bm/config.txt: moved here, and out of it */
static void migrate(void)
{
    static const char *const old[3] = { "ai_url", "ai_model", "ai_key" };
    int any = 0;
    for (int i = 0; i < 3; i++) {
        const char *v = config_get(old[i]);
        if (v) {
            if (!vals[i][0])
                ksnprintf(vals[i], VAL, "%s", v);
            any = 1;
        }
    }
    if (!any || save())
        return;
    for (int i = 0; i < 3; i++)
        config_unset(old[i]);
    config_save();
    kprintf("ai: the AI settings moved from bm/config.txt to bm/ai.txt\n");
}

static void load(void)
{
    if (loaded)
        return;
    loaded = 1;
    fat_entry_t e;
    uint8_t *d = NULL;
    size_t n = 0;
    if (config_find_file(AI_CONF_FILE, &e) == 0 && fat_load(&e, &d, &n) == 0 && d) {
        size_t at = 0;
        while (at < n) {
            size_t end = at;
            while (end < n && d[end] != '\n')
                end++;
            size_t len = end;
            while (len > at && (d[len - 1] == '\r' || d[len - 1] == ' '))
                len--;
            const char *line = (const char *)d + at;
            const char *eq = memchr(line, '=', len - at);
            if (line[0] != '#' && eq) {
                char key[8];
                const size_t kl = (size_t)(eq - line);
                if (kl < sizeof key) {
                    memcpy(key, line, kl);
                    key[kl] = 0;
                    const int i = slot(key);
                    const size_t vl = len - at - kl - 1;
                    if (i >= 0 && vl < VAL) {
                        memcpy(vals[i], eq + 1, vl);
                        vals[i][vl] = 0;
                    }
                }
            }
            at = end + 1;
        }
        for (volatile uint8_t *p = d; p < d + n; p++)
            *p = 0;
        free(d);
    }
    migrate();
}

const char *ai_conf_get(const char *name)
{
    load();
    const int i = slot(name);
    return i >= 0 && vals[i][0] ? vals[i] : NULL;
}

int ai_conf_set(const char *url, const char *model, const char *key)
{
    load();
    const char *in[3] = { url, model, key };
    for (int i = 0; i < 3; i++)
        if (in[i])
            ksnprintf(vals[i], VAL, "%s", in[i]);
    return save();
}
