/* See appdata.h. */
#include "appdata.h"
#include "bm/bm.h"
#include "fs/fat.h"
#include "lib/printf.h"

#include <stdlib.h>
#include <string.h>

static struct {
    char app[16];
    uint8_t *file;
    size_t len;
} held;

static const uint8_t *load(const char *app)
{
    if (held.file && !strcmp(held.app, app))
        return held.file;
    const char *alt = !strcmp(app, "assistant") ? "assistan" : NULL;      /* (8.3: the old short name) */
    char path[40];
    for (int i = 0; i < 2; i++) {
        const char *name = i ? alt : app;
        if (!name)
            continue;
        ksnprintf(path, sizeof path, "/bm/%s.bm", name);
        fat_entry_t e;
        uint8_t *d = NULL;
        size_t n = 0;
        if (fat_find(path, &e) == 0 && fat_load(&e, &d, &n) == 0) {
            free(held.file);
            held.file = d;
            held.len = n;
            ksnprintf(held.app, sizeof held.app, "%s", app);
            return d;
        }
    }
    return NULL;
}

const uint8_t *appdata_get(const char *app, const char *blob, uint32_t *len)
{
    const uint8_t *f = load(app);
    const uint8_t *p;
    if (!f || bm_data_find(f, held.len, blob, &p, len) != 0)
        return NULL;
    return p;
}
