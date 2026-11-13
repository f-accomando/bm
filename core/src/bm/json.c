#include "json.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p, *end;
    char *err;
    size_t errlen;
    int depth;
} jp_t;

static void fail(jp_t *s, const char *what)
{
    if (s->err && s->errlen && !s->err[0]) {
        strncpy(s->err, what, s->errlen - 1);
        s->err[s->errlen - 1] = 0;
    }
}

static void ws(jp_t *s)
{
    while (s->p < s->end && (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' || *s->p == '\r'))
        s->p++;
}

static json_t *node(json_type_t t)
{
    json_t *j = calloc(1, sizeof *j);
    if (j)
        j->type = t;
    return j;
}

static int hex(int c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static int utf8(char *o, unsigned cp)
{
    if (cp < 0x80) {
        o[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        o[0] = (char)(0xC0 | cp >> 6);
        o[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        o[0] = (char)(0xE0 | cp >> 12);
        o[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        o[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    o[0] = (char)(0xF0 | cp >> 18);
    o[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    o[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* a string at s->p (on the opening quote): malloc'd, unescaped */
static char *string(jp_t *s)
{
    if (s->p >= s->end || *s->p != '"') {
        fail(s, "a string was expected");
        return NULL;
    }
    s->p++;
    const char *start = s->p;
    size_t n = 0;
    for (const char *q = start; q < s->end && *q != '"'; q++, n++)
        if (*q == '\\')
            q++;
    char *out = malloc(n * 3 + 1), *o = out;
    if (!out) {
        fail(s, "no memory");
        return NULL;
    }
    while (s->p < s->end && *s->p != '"') {
        if (*s->p != '\\') {
            *o++ = *s->p++;
            continue;
        }
        s->p++;
        if (s->p >= s->end)
            break;
        char c = *s->p++;
        switch (c) {
        case 'n': *o++ = '\n'; break;
        case 't': *o++ = '\t'; break;
        case 'r': *o++ = '\r'; break;
        case 'b': *o++ = '\b'; break;
        case 'f': *o++ = '\f'; break;
        case 'u': {
            unsigned cp = 0;
            for (int i = 0; i < 4; i++) {
                int h = s->p < s->end ? hex(*s->p) : -1;
                if (h < 0) {
                    free(out);
                    fail(s, "a bad \\u escape");
                    return NULL;
                }
                cp = cp << 4 | (unsigned)h;
                s->p++;
            }
            if (cp >= 0xD800 && cp < 0xDC00 && s->end - s->p >= 6 && s->p[0] == '\\' && s->p[1] == 'u') {
                unsigned lo = 0;
                for (int i = 2; i < 6; i++)
                    lo = lo << 4 | (unsigned)(hex(s->p[i]) < 0 ? 0 : hex(s->p[i]));
                if (lo >= 0xDC00 && lo < 0xE000) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    s->p += 6;
                }
            }
            o += utf8(o, cp);
            break;
        }
        default: *o++ = c;
        }
    }
    if (s->p >= s->end) {
        free(out);
        fail(s, "a string without its closing quote");
        return NULL;
    }
    s->p++;
    *o = 0;
    return out;
}

static json_t *value(jp_t *s);

static json_t *container(jp_t *s, json_type_t t)
{
    char close = t == JSON_OBJECT ? '}' : ']';
    if (++s->depth > 64) {
        fail(s, "nested too deep");
        return NULL;
    }
    json_t *j = node(t), *last = NULL;
    if (!j) {
        fail(s, "no memory");
        return NULL;
    }
    s->p++;
    ws(s);
    if (s->p < s->end && *s->p == close) {
        s->p++;
        s->depth--;
        return j;
    }
    for (;;) {
        ws(s);
        char *key = NULL;
        if (t == JSON_OBJECT) {
            key = string(s);
            if (!key)
                goto bad;
            ws(s);
            if (s->p >= s->end || *s->p != ':') {
                free(key);
                fail(s, "a ':' was expected");
                goto bad;
            }
            s->p++;
        }
        json_t *v = value(s);
        if (!v) {
            free(key);
            goto bad;
        }
        v->key = key;
        if (last)
            last->next = v;
        else
            j->child = v;
        last = v;
        j->count++;
        ws(s);
        if (s->p < s->end && *s->p == ',') {
            s->p++;
            continue;
        }
        if (s->p < s->end && *s->p == close) {
            s->p++;
            s->depth--;
            return j;
        }
        fail(s, t == JSON_OBJECT ? "a ',' or '}' was expected" : "a ',' or ']' was expected");
        goto bad;
    }
bad:
    json_free(j);
    return NULL;
}

static json_t *value(jp_t *s)
{
    ws(s);
    if (s->p >= s->end) {
        fail(s, "the text ends too soon");
        return NULL;
    }
    char c = *s->p;
    if (c == '{')
        return container(s, JSON_OBJECT);
    if (c == '[')
        return container(s, JSON_ARRAY);
    if (c == '"') {
        char *str = string(s);
        if (!str)
            return NULL;
        json_t *j = node(JSON_STRING);
        if (!j) {
            free(str);
            fail(s, "no memory");
            return NULL;
        }
        j->str = str;
        return j;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        char *e;
        double d = strtod(s->p, &e);
        if (e == s->p) {
            fail(s, "a bad number");
            return NULL;
        }
        s->p = e;
        json_t *j = node(JSON_NUMBER);
        if (j)
            j->num = d;
        else
            fail(s, "no memory");
        return j;
    }
    static const struct { const char *w; json_type_t t; double v; } words[] = {
        { "true", JSON_BOOL, 1 }, { "false", JSON_BOOL, 0 }, { "null", JSON_NULL, 0 }
    };
    for (int i = 0; i < 3; i++) {
        size_t n = strlen(words[i].w);
        if ((size_t)(s->end - s->p) >= n && memcmp(s->p, words[i].w, n) == 0) {
            s->p += n;
            json_t *j = node(words[i].t);
            if (j)
                j->num = words[i].v;
            else
                fail(s, "no memory");
            return j;
        }
    }
    fail(s, "an unexpected character");
    return NULL;
}

json_t *json_parse(const char *text, size_t len, char *err, size_t errlen)
{
    jp_t s = { text, text + len, err, errlen, 0 };
    if (err && errlen)
        err[0] = 0;
    json_t *j = value(&s);
    if (!j)
        return NULL;
    ws(&s);
    if (s.p != s.end) {
        json_free(j);
        fail(&s, "text after the value");
        return NULL;
    }
    return j;
}

void json_free(json_t *j)
{
    while (j) {
        json_t *next = j->next;
        json_free(j->child);
        free(j->key);
        free(j->str);
        free(j);
        j = next;
    }
}

const json_t *json_get(const json_t *obj, const char *key)
{
    if (!obj || obj->type != JSON_OBJECT)
        return NULL;
    for (const json_t *c = obj->child; c; c = c->next)
        if (c->key && strcmp(c->key, key) == 0)
            return c;
    return NULL;
}

const json_t *json_at(const json_t *arr, int i)
{
    if (!arr || arr->type != JSON_ARRAY || i < 0)
        return NULL;
    const json_t *c = arr->child;
    while (c && i-- > 0)
        c = c->next;
    return c;
}

double json_num(const json_t *obj, const char *key, double dflt)
{
    const json_t *v = key ? json_get(obj, key) : obj;
    return v && (v->type == JSON_NUMBER || v->type == JSON_BOOL) ? v->num : dflt;
}

const char *json_str(const json_t *obj, const char *key, const char *dflt)
{
    const json_t *v = key ? json_get(obj, key) : obj;
    return v && v->type == JSON_STRING ? v->str : dflt;
}
