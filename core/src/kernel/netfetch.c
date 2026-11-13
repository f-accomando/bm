/* "net fetch": what to fetch and where it goes; see netfetch.h. */
#include "netfetch.h"

#include "bm/webpolicy.h"

#include <stdio.h>
#include <string.h>

static int fail(char *err, size_t en, const char *why)
{
    if (en)
        snprintf(err, en, "%s", why);
    return -1;
}

static int gh_part(const char *s, size_t n)
{
    if (!n)
        return 0;
    for (size_t i = 0; i < n; i++) {
        const char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
              c == '.'))
            return 0;
    }
    return !(n <= 2 && s[0] == '.');            /* not "." or ".." */
}

/* gh:OWNER/REPO/PATH[@REF] -> https://raw.githubusercontent.com/OWNER/REPO/REF/PATH */
static int gh_url(const char *src, char *out, size_t on, char *err, size_t en)
{
    const char *s = src + 3, *at = strrchr(s, '@');
    const char *slash1 = strchr(s, '/'), *slash2 = slash1 ? strchr(slash1 + 1, '/') : NULL;
    const size_t end = at ? (size_t)(at - s) : strlen(s);
    if (!slash1 || !slash2 || (size_t)(slash2 - s) >= end || !gh_part(s, (size_t)(slash1 - s)) ||
        !gh_part(slash1 + 1, (size_t)(slash2 - slash1 - 1)))
        return fail(err, en, "gh: is gh:OWNER/REPO/PATH, or gh:OWNER/REPO/PATH@BRANCH");
    const char *path = slash2 + 1;
    const size_t pl = end - (size_t)(path - s);
    if (!pl || path[pl - 1] == '/' || strstr(path, ".."))
        return fail(err, en, "gh: the PATH names a file");
    const char *ref = at ? at + 1 : "HEAD";
    for (const char *c = ref; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || strchr("-_./", *c)))
            return fail(err, en, "gh: the branch has letters, digits and - _ . /");
    if (!*ref)
        return fail(err, en, "gh: nothing after @");
    if ((size_t)snprintf(out, on, "https://" FETCH_GH_HOST "/%.*s/%s/%.*s", (int)(slash2 - s), s, ref, (int)pl, path) >= on)
        return fail(err, en, "the address is too long");
    return 0;
}

int fetch_plan(const char *src, const char *name, fetch_plan_t *p, char *err, size_t en)
{
    memset(p, 0, sizeof *p);
    if (!src || !*src)
        return fail(err, en, "net fetch URL [NAME] [replace]: an https address or gh:OWNER/REPO/PATH");
    if (!strncmp(src, "gh:", 3)) {
        if (gh_url(src, p->url, sizeof p->url, err, en))
            return -1;
    } else if ((size_t)snprintf(p->url, sizeof p->url, "%s", src) >= sizeof p->url) {
        return fail(err, en, "the address is too long");
    }
    char host[128];
    if (web_url_check(p->url, host, sizeof host, err, en))
        return -1;                                  /* https only, no user name, no fragment */
    p->github = !strcmp(host, FETCH_GH_HOST);
    /* the name: given, or the last part of the path without the query */
    char n[64];
    if (name && *name) {
        snprintf(n, sizeof n, "%s", name);
    } else {
        const char *path = strchr(p->url + 8, '/');
        size_t pl = path ? strcspn(path, "?") : 0;
        const char *last = path;
        for (size_t i = 0; i < pl; i++)
            if (path[i] == '/')
                last = path + i + 1;
        const size_t ll = path ? (size_t)(path + pl - last) : 0;
        if (!ll || ll >= sizeof n)
            return fail(err, en, "the address names no file: give the NAME (pong.bm)");
        memcpy(n, last, ll);
        n[ll] = 0;
    }
    char *dot = strrchr(n, '.');
    size_t bl = dot ? (size_t)(dot - n) : strlen(n);
    for (char *c = n; *c; c++)
        if (*c >= 'A' && *c <= 'Z')
            *c = (char)(*c - 'A' + 'a');
    const int bme = dot && !strcmp(dot, ".bme");
    const int bmk = dot && !strcmp(dot, ".bmk");
    const int bmm = dot && !strcmp(dot, ".bmm");
    if (!dot || (strcmp(dot, ".bm") && !bme && !bmk && !bmm))
        return fail(err, en, "a .bm cartridge, a .bme project, a .bmk kit or a .bmm model only (give NAME.bm, NAME.bme, NAME.bmk or NAME.bmm)");
    if (bl < 1 || bl > 8)
        return fail(err, en, "the name has 1 to 8 letters before the extension (an 8.3 name: give a NAME)");
    for (size_t i = 0; i < bl; i++)
        if (!((n[i] >= 'a' && n[i] <= 'z') || (n[i] >= '0' && n[i] <= '9') || n[i] == '_' || n[i] == '-'))
            return fail(err, en, "the name has a-z, 0-9, _ and - only (give a NAME)");
    memcpy(p->name, n, strlen(n) + 1);             /* at most 8 + ".bme" + NUL: 13 bytes */
    snprintf(p->dir, sizeof p->dir, "%s", bme ? "/bme" : bmk ? "/bmk" : bmm ? "/bmm" : "/bm");
    p->res = bmk ? FETCH_KIT : bmm ? FETCH_MODEL : 0;
    snprintf(p->path, sizeof p->path, "%s/%s", p->dir, p->name);
    return 0;
}
