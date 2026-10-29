/* ai_ask for cartridges: the JSON of a chat request and of its answer; see aichat.h. */
#include "aichat.h"

#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void say(char *err, size_t en, const char *what)
{
    if (en)
        snprintf(err, en, "%s", what);
}

int ai_role_ok(const char *role)
{
    return role && (!strcmp(role, "system") || !strcmp(role, "user") || !strcmp(role, "assistant")) ? 0 : -1;
}

int ai_model_ok(const char *name)
{
    size_t n = name ? strlen(name) : 0;
    if (n < 1 || n > AI_MODEL_MAX)
        return -1;
    for (size_t i = 0; i < n; i++) {
        const char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("-_.:/@", c)))
            return -1;
    }
    return 0;
}

int ai_endpoint(const char *base, char *out, size_t n, char *err, size_t en)
{
    static const char tail[] = "/chat/completions";
    size_t bl = base ? strlen(base) : 0;
    while (bl && base[bl - 1] == '/')
        bl--;
    if (!bl) {
        say(err, en, "no AI server: put ai_url in bm/config.txt");
        return -1;
    }
    const size_t tl = sizeof tail - 1;
    const int has = bl >= tl && !memcmp(base + bl - tl, tail, tl);
    if ((size_t)snprintf(out, n, "%.*s%s", (int)bl, base, has ? "" : tail) >= n) {
        say(err, en, "ai_url is too long");
        return -1;
    }
    return 0;
}

/* ---- the request: a growing buffer and JSON strings ---- */

typedef struct {
    char *p;
    size_t len, cap;
    int bad;
} buf_t;

static void put(buf_t *b, const char *s, size_t n)
{
    if (b->bad)
        return;
    if (b->len + n + 1 > b->cap) {
        size_t c = b->cap ? b->cap : 1024;
        while (b->len + n + 1 > c)
            c *= 2;
        char *q = realloc(b->p, c);
        if (!q) {
            b->bad = 1;
            return;
        }
        b->p = q;
        b->cap = c;
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
}

static void puts_(buf_t *b, const char *s)
{
    put(b, s, strlen(s));
}

/* s as a JSON string: quotes, backslash and control bytes escaped; other bytes (UTF-8) kept */
static void put_str(buf_t *b, const char *s, size_t n)
{
    put(b, "\"", 1);
    size_t run = 0;
    for (size_t i = 0; i < n; i++) {
        const unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c != '"' && c != '\\')
            continue;
        put(b, s + run, i - run);
        run = i + 1;
        char e[8];
        if (c == '"' || c == '\\')
            snprintf(e, sizeof e, "\\%c", c);
        else if (c == '\n')
            snprintf(e, sizeof e, "\\n");
        else if (c == '\t')
            snprintf(e, sizeof e, "\\t");
        else if (c == '\r')
            snprintf(e, sizeof e, "\\r");
        else
            snprintf(e, sizeof e, "\\u%04x", c);
        puts_(b, e);
    }
    put(b, s + run, n - run);
    put(b, "\"", 1);
}

char *ai_request_json(const char *model, const ai_msg_t *m, int n, int max_tokens, double temperature,
                      size_t *len, char *err, size_t en)
{
    if (ai_model_ok(model)) {
        say(err, en, "no model, or a bad one: put ai_model in bm/config.txt");
        return NULL;
    }
    if (n < 1 || n > AI_MSGS_MAX) {
        say(err, en, "1 to 64 messages");
        return NULL;
    }
    size_t total = 0;
    for (int i = 0; i < n; i++) {
        if (ai_role_ok(m[i].role)) {
            say(err, en, "a message's role is \"system\", \"user\" or \"assistant\"");
            return NULL;
        }
        total += m[i].len;
    }
    if (total > AI_TEXT_MAX) {
        say(err, en, "the messages are over 192 KB");
        return NULL;
    }
    if (max_tokens < 1 || max_tokens > AI_TOKENS_LIMIT)
        max_tokens = AI_TOKENS_DEFAULT;
    buf_t b = { 0 };
    puts_(&b, "{\"model\":");
    put_str(&b, model, strlen(model));
    puts_(&b, ",\"stream\":false,\"messages\":[");
    for (int i = 0; i < n; i++) {
        puts_(&b, i ? ",{\"role\":\"" : "{\"role\":\"");
        puts_(&b, m[i].role);
        puts_(&b, "\",\"content\":");
        put_str(&b, m[i].content ? m[i].content : "", m[i].content ? m[i].len : 0);
        puts_(&b, "}");
    }
    char tail[64];
    snprintf(tail, sizeof tail, "],\"max_tokens\":%d", max_tokens);
    puts_(&b, tail);
    if (temperature >= 0) {
        const int t = (int)(temperature * 100 + 0.5);     /* hundredths: the kernel's printf has no %f */
        snprintf(tail, sizeof tail, ",\"temperature\":%d.%02d", (t > 200 ? 200 : t) / 100, (t > 200 ? 200 : t) % 100);
        puts_(&b, tail);
    }
    puts_(&b, "}");
    if (b.bad) {
        free(b.p);
        say(err, en, "not enough memory");
        return NULL;
    }
    *len = b.len;
    return b.p;
}

/* ---- the answer ---- */

/* the server's message: {"error":{"message":..}}, [{"error":{..}}] (Gemini), {"error":".."}, {"message":..} */
static const char *error_text(const json_t *j)
{
    if (j && j->type == JSON_ARRAY)
        j = json_at(j, 0);
    if (!j || j->type != JSON_OBJECT)
        return NULL;
    const json_t *e = json_get(j, "error");
    if (e && e->type == JSON_STRING)
        return e->str;
    if (e && e->type == JSON_OBJECT)
        return json_str(e, "message", NULL);
    return json_str(j, "message", json_str(j, "detail", NULL));
}

/* content is a string, or a list of parts {"type":"text","text":..} joined */
static char *content_text(const json_t *c)
{
    buf_t b = { 0 };
    if (c && c->type == JSON_STRING) {
        put(&b, "", 0);
        puts_(&b, c->str ? c->str : "");
        return b.bad ? (free(b.p), NULL) : b.p;
    }
    if (!c || c->type != JSON_ARRAY)
        return NULL;
    put(&b, "", 0);
    for (int i = 0; i < c->count; i++) {
        const char *t = json_str(json_at(c, i), "text", NULL);
        if (t)
            puts_(&b, t);
    }
    if (b.bad) {
        free(b.p);
        return NULL;
    }
    return b.p;
}

int ai_reply_parse(int status, const char *body, size_t len, ai_reply_t *out, char *err, size_t en)
{
    memset(out, 0, sizeof *out);
    out->prompt_tokens = out->completion_tokens = -1;
    char jerr[64] = "";
    json_t *j = body && len ? json_parse(body, len, jerr, sizeof jerr) : NULL;
    const json_t *msg = json_get(json_at(json_get(j, "choices"), 0), "message");
    char *text = status >= 200 && status < 300 && msg ? content_text(json_get(msg, "content")) : NULL;
    if (!text) {
        const char *why = error_text(j);
        if (why && why[0])
            snprintf(err, en, "HTTP %d: %s", status, why);
        else if (status < 200 || status >= 300)
            snprintf(err, en, "HTTP %d from the AI server", status);
        else
            snprintf(err, en, "the answer has no text%s%s", jerr[0] ? ": " : "", jerr);
        json_free(j);
        return -1;
    }
    out->text = text;
    const char *fin = json_str(json_at(json_get(j, "choices"), 0), "finish_reason", "");
    snprintf(out->finish, sizeof out->finish, "%s", fin);
    const json_t *u = json_get(j, "usage");
    if (u) {
        out->prompt_tokens = (int)json_num(u, "prompt_tokens", -1);
        out->completion_tokens = (int)json_num(u, "completion_tokens", -1);
    }
    json_free(j);
    return 0;
}

void ai_reply_free(ai_reply_t *r)
{
    free(r->text);
    r->text = NULL;
}
