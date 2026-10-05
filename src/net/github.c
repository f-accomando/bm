/*
 * Publishing a game to the Market from the console (M25, step 6): a pull
 * request to the market's repository through GitHub's REST API, with the
 * user's personal token; and a file put on a branch (the reports,
 * src/kernel/reports.c). Portable: the HTTP client and a pause from the
 * caller (tests/net/test_github.c runs it against a fake API).
 */
#include "github.h"
#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    char *buf;
    size_t len, cap, max;
} body_t;

static int to_body(void *ctx, const uint8_t *d, size_t n)
{
    body_t *b = ctx;
    if (b->len + n > b->max)
        return 1;
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 4096;
        while (cap < b->len + n + 1)
            cap *= 2;
        char *p = realloc(b->buf, cap);
        if (!p)
            return 1;
        b->buf = p;
        b->cap = cap;
    }
    memcpy(b->buf + b->len, d, n);
    b->len += n;
    b->buf[b->len] = 0;
    return 0;
}

/* ---------------------------------------------------------------- JSON, the little needed */

/* The string value of the first "key" in js[0..end): 0, or -1 if none. */
static int json_get(const char *js, const char *end, const char *key, char *out, size_t n)
{
    size_t kl = strlen(key);
    for (const char *p = js; p + kl + 2 < end; p++) {
        if (*p != '"' || strncmp(p + 1, key, kl) != 0 || p[kl + 1] != '"')
            continue;
        const char *q = p + kl + 2;
        while (q < end && (*q == ' ' || *q == '\n' || *q == '\r' || *q == '\t'))
            q++;
        if (q >= end || *q != ':')
            continue;
        q++;
        while (q < end && (*q == ' ' || *q == '\n' || *q == '\r' || *q == '\t'))
            q++;
        if (q >= end || *q != '"')
            return -1;                  /* not a string (null, a number...) */
        size_t o = 0;
        for (q++; q < end && *q != '"'; q++) {
            char c = *q;
            if (c == '\\' && q + 1 < end) {
                c = *++q;
                c = c == 'n' ? '\n' : c == 't' ? '\t' : c == 'u' ? '?' : c;
                if (*q == 'u')
                    q += q + 4 < end ? 4 : 0;
            }
            if (o + 1 < n)
                out[o++] = c;
        }
        out[o] = 0;
        return 0;
    }
    return -1;
}

/* The number value of the first "key" in js[0..end): 0, or -1 if none. */
static int json_num(const char *js, const char *end, const char *key, unsigned long *out)
{
    size_t kl = strlen(key);
    for (const char *p = js; p + kl + 2 < end; p++) {
        if (*p != '"' || strncmp(p + 1, key, kl) != 0 || p[kl + 1] != '"')
            continue;
        const char *q = p + kl + 2;
        while (q < end && (*q == ' ' || *q == ':' || *q == '\n' || *q == '\r' || *q == '\t'))
            q++;
        if (q >= end || *q < '0' || *q > '9')
            return -1;
        unsigned long v = 0;
        for (; q < end && *q >= '0' && *q <= '9'; q++)
            v = v * 10 + (unsigned long)(*q - '0');
        *out = v;
        return 0;
    }
    return -1;
}

/* The next object of an array: [*start, *stop) and 1, or 0 at the end. */
static int json_next(const char **p, const char *end, const char **start, const char **stop)
{
    const char *q = *p;
    while (q < end && *q != '{')
        q++;
    if (q >= end)
        return 0;
    int depth = 0, str = 0;
    *start = q;
    for (; q < end; q++) {
        if (str) {
            if (*q == '\\') q++;
            else if (*q == '"') str = 0;
        } else if (*q == '"') {
            str = 1;
        } else if (*q == '{') {
            depth++;
        } else if (*q == '}' && --depth == 0) {
            *stop = ++q;
            *p = q;
            return 1;
        }
    }
    return 0;
}

/* s as a JSON string's content (no quotes) */
static size_t json_escape(char *out, size_t n, const char *s)
{
    size_t o = 0;
    for (; *s; s++) {
        char e = *s == '"' ? '"' : *s == '\\' ? '\\' : *s == '\n' ? 'n' : *s == '\t' ? 't' : 0;
        if ((unsigned char)*s < 32 && !e)
            continue;
        if (e) {
            if (o + 2 < n) { out[o++] = '\\'; out[o++] = e; }
        } else if (o + 1 < n) {
            out[o++] = *s;
        }
    }
    if (n)
        out[o < n ? o : n - 1] = 0;
    return o;
}

static size_t base64(char *out, const uint8_t *d, size_t n)
{
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        out[o++] = t[v >> 18 & 63];
        out[o++] = t[v >> 12 & 63];
        out[o++] = i + 1 < n ? t[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? t[v & 63] : '=';
    }
    out[o] = 0;
    return o;
}

/* ---------------------------------------------------------------- the API */

typedef struct {
    const char *api, *token;
    void (*progress)(const char *step);
    char *err;
    size_t err_len;
    body_t body;                /* the last answer */
    int status;                 /* its HTTP status, -1 none */
} gh_t;

/* A call: the status (the answer in g->body), or -1 with the error. */
static int call(gh_t *g, const char *method, const char *path, const char *json, size_t json_len)
{
    char url[384], head[512];
    snprintf(url, sizeof url, "%s%s", g->api, path);
    snprintf(head, sizeof head,
             "Authorization: Bearer %s\r\nAccept: application/vnd.github+json\r\n"
             "X-GitHub-Api-Version: 2022-11-28\r\n%s",
             g->token, json ? "Content-Type: application/json\r\n" : "");
    free(g->body.buf);
    memset(&g->body, 0, sizeof g->body);
    g->body.max = 1 << 20;
    http_req_t req = { .method = method, .headers = head, .body = json, .body_len = json_len,
                       .timeout_ms = json_len > 65536 ? 90000 : 20000, .any_status = 1 };
    http_info_t info;
    int st = http_request(url, &req, to_body, &g->body, &info);
    g->status = st;
    if (st < 0) {
        snprintf(g->err, g->err_len, "%s %s: %s", method, path, info.error);
        return -1;
    }
    if (!g->body.buf)
        to_body(&g->body, (const uint8_t *)"", 0);
    return st;
}

/* an answer that is not the one expected: GitHub's message */
static int fail(gh_t *g, const char *what, int st)
{
    char msg[160] = "";
    if (g->body.buf)
        json_get(g->body.buf, g->body.buf + g->body.len, "message", msg, sizeof msg);
    /* one line: GitHub's "Invalid request.\n\n\"sha\" wasn't supplied."
     * showed only as "Invalid request." on the console */
    size_t o = 0;
    for (size_t i = 0; msg[i]; i++) {
        char c = msg[i] == '\n' || msg[i] == '\r' || msg[i] == '\t' ? ' ' : msg[i];
        if (c != ' ' || (o && msg[o - 1] != ' '))
            msg[o++] = c;
    }
    msg[o] = 0;
    if (st == 401)
        snprintf(g->err, g->err_len, "the token is not valid (github_token in bm/config.txt)");
    else
        snprintf(g->err, g->err_len, "%s: HTTP %d%s%s", what, st, msg[0] ? ", " : "", msg);
    return -1;
}

static void say(gh_t *g, const char *fmt, const char *a)
{
    if (g->progress) {
        char line[160];
        snprintf(line, sizeof line, fmt, a);
        g->progress(line);
    }
}

/* where a file of the game is now in the branch: its blob's sha, "" if none;
 * and the .bm already there (the update keeps its name) */
typedef struct {
    char bm_name[64], bm_sha[48], info_sha[48];
} folder_t;

static int read_folder(gh_t *g, const char *target, const char *branch, const char *id, folder_t *f)
{
    memset(f, 0, sizeof *f);
    char path[256];
    snprintf(path, sizeof path, "/repos/%s/contents/games/%s?ref=%s", target, id, branch);
    int st = call(g, "GET", path, NULL, 0);
    if (st == 404)
        return 0;                       /* a new game */
    if (st != 200)
        return st < 0 ? -1 : fail(g, "the game's folder", st);
    const char *p = g->body.buf, *end = p + g->body.len, *a, *b;
    while (json_next(&p, end, &a, &b)) {
        char name[64], sha[48];
        if (json_get(a, b, "name", name, sizeof name) || json_get(a, b, "sha", sha, sizeof sha))
            continue;
        size_t n = strlen(name);
        if (n > 3 && !strcasecmp(name + n - 3, ".bm") && !f->bm_name[0]) {
            snprintf(f->bm_name, sizeof f->bm_name, "%s", name);
            snprintf(f->bm_sha, sizeof f->bm_sha, "%s", sha);
        } else if (!strcmp(name, "info.txt")) {
            snprintf(f->info_sha, sizeof f->info_sha, "%s", sha);
        }
    }
    return 0;
}

/* a file into a branch (contents API): `file` is its path in the
 * repository, `sha` the blob it replaces ("" for a new file) */
static int put_contents(gh_t *g, const char *target, const char *branch, const char *file,
                        const uint8_t *data, size_t len, const char *sha, const char *message)
{
    const char *name = strrchr(file, '/') ? strrchr(file, '/') + 1 : file;
    char esc[200];
    json_escape(esc, sizeof esc, message);
    size_t cap = (len + 2) / 3 * 4 + 512;
    char *json = malloc(cap);
    if (!json) {
        snprintf(g->err, g->err_len, "out of memory for %s", name);
        return -1;
    }
    size_t o = (size_t)snprintf(json, cap, "{\"message\":\"%s\",\"branch\":\"%s\",%s%s%s\"content\":\"",
                                esc, branch, sha[0] ? "\"sha\":\"" : "", sha, sha[0] ? "\"," : "");
    o += base64(json + o, data, len);
    o += (size_t)snprintf(json + o, cap - o, "\"}");
    char path[384];
    snprintf(path, sizeof path, "/repos/%s/contents/%s", target, file);
    say(g, "sending %s", name);
    int st = call(g, "PUT", path, json, o);
    free(json);
    if (st == 200 || st == 201)
        return 0;
    return st < 0 ? -1 : fail(g, name, st);
}

static int put_file(gh_t *g, const gh_publish_t *p, const char *target, const char *name,
                    const uint8_t *data, size_t len, const char *sha)
{
    char msg[160], file[256];
    snprintf(msg, sizeof msg, "%s %s: %s", p->title, p->version, name);
    snprintf(file, sizeof file, "games/%s/%s", p->id, name);
    return put_contents(g, target, p->branch, file, data, len, sha, msg);
}

int github_publish(const gh_publish_t *p, char *url, size_t url_len, char *err, size_t err_len)
{
    gh_t g = { p->api, p->token, p->progress, err, err_len, { 0 }, -1 };
    char login[64], owner[64], path[256], target[128], sha[48], json[1024];
    url[0] = 0;
    err[0] = 0;
    int st, r = -1;
    const char *slash = strchr(p->repo, '/');
    if (!slash || slash == p->repo || (size_t)(slash - p->repo) >= sizeof owner) {
        snprintf(err, err_len, "bad repository %s", p->repo);
        return -1;
    }
    memcpy(owner, p->repo, (size_t)(slash - p->repo));
    owner[slash - p->repo] = 0;

    say(&g, "%s", "who the token belongs to");
    if ((st = call(&g, "GET", "/user", NULL, 0)) != 200) {
        if (st >= 0) fail(&g, "the token", st);
        goto out;
    }
    if (json_get(g.body.buf, g.body.buf + g.body.len, "login", login, sizeof login)) {
        snprintf(err, err_len, "the token: no login in the answer");
        goto out;
    }
    say(&g, "token of %s", login);

    /* the owner opens a branch in the repository; anyone else in a fork */
    int own = !strcasecmp(login, owner);
    if (own) {
        snprintf(target, sizeof target, "%s", p->repo);
    } else {
        say(&g, "fork of %s", p->repo);
        snprintf(path, sizeof path, "/repos/%s/forks", p->repo);
        if ((st = call(&g, "POST", path, "{}", 2)) != 202 && st != 200) {
            if (st >= 0) fail(&g, "the fork", st);
            goto out;
        }
        if (json_get(g.body.buf, g.body.buf + g.body.len, "full_name", target, sizeof target)) {
            snprintf(err, err_len, "the fork: no name in the answer");
            goto out;
        }
        /* GitHub makes the fork in the background: wait for its main */
        snprintf(path, sizeof path, "/repos/%s/git/ref/heads/%s", target, p->base);
        for (int i = 0; (st = call(&g, "GET", path, NULL, 0)) != 200; i++) {
            if (st < 0)
                goto out;
            if (i == 30) {
                fail(&g, "the fork is not ready", st);
                goto out;
            }
            say(&g, "%s", "waiting for the fork");
            if (p->pause)
                p->pause(2000);
        }
        /* up to date with the market (an old fork) */
        snprintf(path, sizeof path, "/repos/%s/merge-upstream", target);
        snprintf(json, sizeof json, "{\"branch\":\"%s\"}", p->base);
        if ((st = call(&g, "POST", path, json, strlen(json))) < 0)
            goto out;
        if (st != 200 && st != 409)
            say(&g, "%s", "the fork could not catch up with the market: going on");
    }

    snprintf(path, sizeof path, "/repos/%s/git/ref/heads/%s", target, p->base);
    if ((st = call(&g, "GET", path, NULL, 0)) != 200) {
        if (st >= 0) fail(&g, "the main branch", st);
        goto out;
    }
    if (json_get(g.body.buf, g.body.buf + g.body.len, "sha", sha, sizeof sha)) {
        snprintf(err, err_len, "the main branch: no commit in the answer");
        goto out;
    }
    say(&g, "branch %s", p->branch);
    snprintf(path, sizeof path, "/repos/%s/git/refs", target);
    snprintf(json, sizeof json, "{\"ref\":\"refs/heads/%s\",\"sha\":\"%s\"}", p->branch, sha);
    if ((st = call(&g, "POST", path, json, strlen(json))) != 201) {
        if (st >= 0) fail(&g, "the branch", st);
        goto out;
    }

    folder_t f;
    if (read_folder(&g, target, p->branch, p->id, &f) != 0)
        goto out;
    /* an update keeps the name of the .bm already there: one .bm per folder */
    const char *name = f.bm_name[0] ? f.bm_name : p->name;
    if (put_file(&g, p, target, name, p->cart, p->cart_len, f.bm_sha) != 0 ||
        put_file(&g, p, target, "info.txt", (const uint8_t *)p->info, strlen(p->info), f.info_sha) != 0)
        goto out;

    say(&g, "%s", "pull request");
    char title[160], body[640], et[200], eb[700];
    snprintf(title, sizeof title, "%s %s %s", f.bm_name[0] ? "Update" : "Add", p->title, p->version);
    snprintf(body, sizeof body,
             "%s (`games/%s/`), version %s, sent from a bm console.\n\n%s",
             p->title, p->id, p->version, p->info);
    json_escape(et, sizeof et, title);
    json_escape(eb, sizeof eb, body);
    char *pr = malloc(strlen(et) + strlen(eb) + 256);
    if (!pr) {
        snprintf(err, err_len, "out of memory");
        goto out;
    }
    sprintf(pr, "{\"title\":\"%s\",\"head\":\"%s%s%s\",\"base\":\"%s\",\"body\":\"%s\"}",
            et, own ? "" : login, own ? "" : ":", p->branch, p->base, eb);
    snprintf(path, sizeof path, "/repos/%s/pulls", p->repo);
    st = call(&g, "POST", path, pr, strlen(pr));
    free(pr);
    if (st != 201) {
        if (st >= 0) fail(&g, "the pull request", st);
        goto out;
    }
    if (json_get(g.body.buf, g.body.buf + g.body.len, "html_url", url, url_len))
        snprintf(url, url_len, "https://github.com/%s/pulls", p->repo);
    r = 0;
out:
    free(g.body.buf);
    return r;
}

/* the sha of a branch's last commit; 404 if there is no such branch */
static int branch_head(gh_t *g, const char *repo, const char *branch, char *sha, size_t n)
{
    char path[256];
    snprintf(path, sizeof path, "/repos/%s/git/ref/heads/%s", repo, branch);
    int st = call(g, "GET", path, NULL, 0);
    if (st == 200 && json_get(g->body.buf, g->body.buf + g->body.len, "sha", sha, n)) {
        snprintf(g->err, g->err_len, "%s: no commit in the answer", branch);
        return -1;
    }
    return st;
}

/* 1 if the branch has p's file already, with p's size (its answer in
 * g->body); 0 if not, or if GitHub cannot say */
static int already_there(gh_t *g, const gh_put_t *p)
{
    char path[384];
    unsigned long size;
    snprintf(path, sizeof path, "/repos/%s/contents/%s?ref=%s", p->repo, p->path, p->branch);
    if (call(g, "GET", path, NULL, 0) != 200 ||
        json_num(g->body.buf, g->body.buf + g->body.len, "size", &size) != 0 || size != p->len)
        return 0;
    say(g, "%s was there already", strrchr(p->path, '/') ? strrchr(p->path, '/') + 1 : p->path);
    return 1;
}

int github_put(const gh_put_t *p, char *url, size_t url_len, char *err, size_t err_len)
{
    gh_t g = { p->api, p->token, p->progress, err, err_len, { 0 }, -1 };
    char sha[48], json[256], base[96];
    url[0] = 0;
    err[0] = 0;
    int r = -1, st = branch_head(&g, p->repo, p->branch, sha, sizeof sha);
    if (st == 404) {
        /* the branch is new: made from the repository's main branch */
        char path[256];
        snprintf(path, sizeof path, "/repos/%s", p->repo);
        if ((st = call(&g, "GET", path, NULL, 0)) != 200) {
            if (st >= 0) fail(&g, "the repository", st);
            goto out;
        }
        if (json_get(g.body.buf, g.body.buf + g.body.len, "default_branch", base, sizeof base)) {
            snprintf(err, err_len, "the repository: no main branch in the answer");
            goto out;
        }
        if ((st = branch_head(&g, p->repo, base, sha, sizeof sha)) != 200) {
            if (st >= 0) fail(&g, "the main branch", st);
            goto out;
        }
        say(&g, "new branch %s", p->branch);
        snprintf(path, sizeof path, "/repos/%s/git/refs", p->repo);
        snprintf(json, sizeof json, "{\"ref\":\"refs/heads/%s\",\"sha\":\"%s\"}", p->branch, sha);
        st = call(&g, "POST", path, json, strlen(json));
        if (st != 201 && st != 422) {           /* 422: made meanwhile */
            if (st >= 0) fail(&g, "the branch", st);
            goto out;
        }
    } else if (st != 200) {
        if (st >= 0) fail(&g, "the branch", st);
        goto out;
    }
    if (put_contents(&g, p->repo, p->branch, p->path, p->data, p->len, "", p->message) != 0) {
        /* 422 ("sha" wasn't supplied): the file is there already. A send
         * that reached GitHub but whose answer did not reach the console
         * (the connection dropped, the menu stopped the work): it kept the
         * file and sends it again, refused every time, and the files after
         * it waited behind it. The names are unique (a random tag or the
         * second): the same name with the same size is this file, sent. */
        if (g.status != 422 || !already_there(&g, p))
            goto out;
        err[0] = 0;
    }
    if (json_get(g.body.buf, g.body.buf + g.body.len, "html_url", url, url_len))
        snprintf(url, url_len, "https://github.com/%s/blob/%s/%s", p->repo, p->branch, p->path);
    r = 0;
out:
    free(g.body.buf);
    return r;
}

int github_id_from_name(const char *file, char *id, size_t n)
{
    const char *base = strrchr(file, '/');
    base = base ? base + 1 : file;
    size_t o = 0;
    int dash = 0;
    for (const char *s = base; *s && *s != '.' && o + 1 < n && o < 23; s++) {
        char c = *s >= 'A' && *s <= 'Z' ? (char)(*s + 32) : *s;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            id[o++] = c;
            dash = 0;
        } else if (o && !dash) {
            id[o++] = '-';
            dash = 1;
        }
    }
    while (o && id[o - 1] == '-')
        o--;
    id[o] = 0;
    return o ? 0 : -1;
}
