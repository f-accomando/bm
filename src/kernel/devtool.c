/*
 * The session of a development tool built into the kernel (devtool.h): the
 * tool, the games it tries, the other tools it opens on its file. Shared by
 * the Pi (carts.c) and the RGB30 (src/rgb30/ui.c).
 */
#include "devtool.h"
#include "crumbs.h"
#include "bm/runtime.h"
#include "fs/fat.h"
#include "lib/printf.h"

#include <stdlib.h>
#include <string.h>

#define PLAY_SECS   (24u * 3600u)

/* Loads a tool from the SD card if installed from Market > Apps, or NULL to
 * use the copy built into the kernel. */
static uint8_t *load_sd_tool(const char *name, size_t *len)
{
    if (!name || !name[0])
        return NULL;
    const char *alt = !strcmp(name, "sdk") ? "editor"
                    : !strcmp(name, "assistant") ? "assistan" : NULL;
    char paths[6][40];
    int np = 0;
#ifdef BM_RGB30
    if (!strcmp(name, "sdk"))
        ksnprintf(paths[np++], sizeof paths[0], "/bm/sdk_b16.b16");
    ksnprintf(paths[np++], sizeof paths[0], "/bm/%s.b16", name);
    ksnprintf(paths[np++], sizeof paths[0], "/bm/%s.bm", name);
    if (alt) {
        ksnprintf(paths[np++], sizeof paths[0], "/bm/%s.b16", alt);
        ksnprintf(paths[np++], sizeof paths[0], "/bm/%s.bm", alt);
    }
#else
    ksnprintf(paths[np++], sizeof paths[0], "/bm/%s.bm", name);
    if (alt)
        ksnprintf(paths[np++], sizeof paths[0], "/bm/%s.bm", alt);
    ksnprintf(paths[np++], sizeof paths[0], "/bm/%s.b16", name);
    if (alt)
        ksnprintf(paths[np++], sizeof paths[0], "/bm/%s.b16", alt);
#endif
    for (int i = 0; i < np; i++) {
        fat_entry_t e;
        uint8_t *d = NULL;
        if (fat_find(paths[i], &e) == 0 && fat_load(&e, &d, len) == 0)
            return d;
    }
    return NULL;
}

/* the game a tool asked to publish (cart_publish), kept for the menu that opened the session */
static char publish_path[96];

int devtool_take_publish(char *path, size_t n)
{
    if (!publish_path[0])
        return 0;
    ksnprintf(path, n, "%s", publish_path);
    publish_path[0] = 0;
    return 1;
}

const char *devtool_session(framebuffer_t *fb, const devtools_t *set, const uint8_t *cart, size_t cart_len,
                            const char *what, const char *open)
{
    char path[64] = "", err[512] = "", tool[16], from[16] = "";
    int back = 0, tried = 0;
    bm_stats_t st;
    if (open)
        ksnprintf(path, sizeof path, "%s", open);
    for (;;) {
        const char *tname = NULL;
        for (size_t k = 0; k < set->n; k++)
            if (!strcmp(set->tools[k].what, what))
                tname = set->tools[k].name;
        size_t sd_len = 0;
        uint8_t *sd_cart = load_sd_tool(tname, &sd_len);
        const uint8_t *run_cart = sd_cart ? sd_cart : cart;
        size_t run_len = sd_cart ? sd_len : cart_len;
        if (!run_cart || run_len == 0) {
            free(sd_cart);
            kprintf("devtool: %s is not installed (get it from Market > Apps)\n", what);
            break;
        }
        crumb(what, NULL);
        bm_set_arg(path[0] ? path : NULL, err[0] ? err : NULL);
        bm_set_arg_back(back);
        bm_set_arg_run(tried ? &st : NULL);     /* the dev kit's numbers of the game tried */
        bm_set_arg_from(from[0] ? from : NULL);
        tried = 0;
        bm_set_tool(1);                 /* the tool saves where it is told */
        bm_play(fb, run_cart, run_len, PLAY_SECS, &st);
        free(sd_cart);
        bm_set_tool(0);                 /* the games it tries do not */
        bm_set_arg(NULL, NULL);
        bm_set_arg_run(NULL);
        bm_set_arg_from(NULL);
        if (set->after_run)
            set->after_run();
        if (bm_take_publish(publish_path, sizeof publish_path))
            break;                      /* cart_publish: the menu opens the Publish panel on that game */
        if (bm_take_tool(tool, sizeof tool)) {
            /* the tool that asks: the next one finds it in cart_arg().from */
            from[0] = 0;
            for (size_t k = 0; k < set->n; k++)
                if (!strcmp(set->tools[k].what, what))
                    ksnprintf(from, sizeof from, "%s", set->tools[k].name);
            size_t i = 0;
            while (i < set->n && strcmp(set->tools[i].name, tool))
                i++;
            path[0] = 0;
            bm_take_run(path, sizeof path);
            if (i == set->n)
                break;
            cart = set->tools[i].start;
            cart_len = (size_t)(set->tools[i].end - set->tools[i].start);
            what = set->tools[i].what;
            back = 0;
            err[0] = 0;
            continue;
        }
        if (!bm_take_run(path, sizeof path))
            break;
        back = 1;
        err[0] = 0;
        fat_entry_t e;
        uint8_t *data;
        size_t len;
        if (fat_find(path, &e) != 0 || fat_load(&e, &data, &len) != 0) {
            ksnprintf(err, sizeof err, "cannot read %s: %s", path, fat_error());
            continue;
        }
        crumb("trying", path);
        bm_play(fb, data, len, PLAY_SECS, &st);
        bm_print_stats(&st);
        tried = st.frames > 0;
        free(data);
        ksnprintf(err, sizeof err, "%s", bm_last_error());
        if (set->after_run)
            set->after_run();
    }
    return what;
}
