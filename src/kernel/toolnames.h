/*
 * The file names under which bm runs an SD file as one of its own tools, with a tool's rights (write access
 * everywhere, no permission questions): the ONE list. Used by the Pi's menu (carts.c tool_id_of), the RGB30's
 * (rgb30/ui.c, which keeps them out of the Games tab) and the runtime (reserved_tool_name: a game must not create
 * such a file). scripts/consistency.py checks it against the Market's list of apps (src/net/catalog.c,
 * scripts/mkmarket.py).
 */
#ifndef TOOLNAMES_H
#define TOOLNAMES_H

#include <stddef.h>
#include <string.h>
#include <strings.h>

typedef struct {
    const char *stem;           /* the file name without its extension, any case */
    const char *id;             /* the tool: "sdk", "sound", "studio", "animator", "mesh", "pixel", "code", "assistant" */
} toolname_t;

static const toolname_t bm_toolnames[] = {
    { "sdk", "sdk" }, { "editor", "sdk" }, { "sdk_b16", "sdk" },
    { "sound", "sound" }, { "studio", "studio" }, { "animator", "animator" }, { "mesh", "mesh" },
    { "pixel", "pixel" }, { "code", "code" },
    { "assistant", "assistant" }, { "assistan", "assistant" },      /* (8.3: the old short name) */
};
#define BM_TOOLNAMES (sizeof bm_toolnames / sizeof bm_toolnames[0])

/* The tool a file name ("Studio.BM", "sdk_b16.b16") stands for, or NULL: only the .bm and .b16 extensions. */
static inline const char *bm_tool_id_of_file(const char *name)
{
    const char *dot = name ? strrchr(name, '.') : NULL;
    if (!dot || (strcasecmp(dot, ".bm") != 0 && strcasecmp(dot, ".b16") != 0))
        return NULL;
    const size_t k = (size_t)(dot - name);
    for (size_t i = 0; i < BM_TOOLNAMES; i++)
        if (strlen(bm_toolnames[i].stem) == k && strncasecmp(name, bm_toolnames[i].stem, k) == 0)
            return bm_toolnames[i].id;
    return NULL;
}

#endif
