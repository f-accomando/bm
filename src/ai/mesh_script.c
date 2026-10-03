/*
 * The part language of the 3D recipes (see mesh.h, mesh_script): a model
 * written as text, one primitive per line, with the same primitives the
 * recipes in C use (mesh_int.h). It is what tools/img2mesh.py gets back
 * from the vision model for an image, and what ai.script() builds on the
 * console. Lines:
 *
 *   # a comment
 *   mat M RRGGBB [flat]              material M (1-15): its colour; flat = one shade
 *   box x0 y0 z0 x1 y1 z1 M          a box from corner to corner
 *   bx x y z w h d M                 a box centred on x, z, standing on y
 *   tube ax ay az bx by bz ra rb n M [nocap]   from a to b, radii, n sides
 *   cyl x y z r h n M                a vertical cylinder standing on y
 *   ell x y z rx ry rz n M           an ellipsoid
 *   prism x|y|z w0 w1 M u v u v ...  a convex polygon extruded along the axis
 *   wedge x0 y0 z0 x1 z1 h0 h1 M     a box whose top slopes from h0 (z0) to h1 (z1)
 *   tf x y z rx ry rz                what follows is turned (degrees) then moved
 *   tfoff                            back to the model's frame
 *   bone NAME PARENT hx hy hz tx ty tz   PARENT: a bone's name, or - for a root
 *   use NAME                         the bone of the faces that follow
 *   side                             the left side starts here
 *   mirror [NAME ...]                mirror the bones named (.L -> .R, with
 *                                    their children) and the faces since side
 *   clip NAME length loop|once       an animation
 *   key t                            a keyframe at t seconds (the rest pose)
 *   turn NAME rx ry rz               the bone turned, in the last key
 *   shift NAME x y z                 the bone moved, in the last key
 *
 * Units: one unit is one block of bm Studio; the model faces -z and stands
 * on y = 0; the left side is +x. Numbers are plain decimals.
 */
#include "mesh_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXTOK 40

typedef struct {
    mc_t *c;
    int side;                   /* the face where the left side starts, or -1 */
    mesh_clip_t *clip;
    mesh_key_t *key;
    char err[128];
} ms_t;

static int fail(ms_t *s, int line, const char *msg)
{
    if (!s->err[0])
        snprintf(s->err, sizeof s->err, "line %d: %s", line, msg);
    return -1;
}

static int find_bone(const mc_t *c, const char *name)
{
    for (int i = 0; i < c->m->nbones; i++)
        if (!strcmp(c->m->bones[i].name, name))
            return i;
    return -1;
}

static int num(const char *t, float *v)
{
    char *end;
    *v = strtof(t, &end);
    return end != t && *end == 0;
}

/* n numbers from tok[i..]; 0 if one is missing or not a number */
static int nums(char **tok, int ntok, int i, int n, float *v)
{
    if (i + n > ntok)
        return 0;
    for (int k = 0; k < n; k++)
        if (!num(tok[i + k], &v[k]))
            return 0;
    return 1;
}

static int mat_index(const char *t)
{
    char *end;
    long m = strtol(t, &end, 10);
    return end != t && *end == 0 && m >= 1 && m < NMAT ? (int)m : -1;
}

static int statement(ms_t *s, int line, char **tok, int ntok)
{
    mc_t *c = s->c;
    float v[12];
    const char *w = tok[0];
    if (!strcmp(w, "mat")) {
        int m = ntok >= 3 ? mat_index(tok[1]) : -1;
        char *end;
        unsigned long rgb = ntok >= 3 ? strtoul(tok[2], &end, 16) : 0;
        if (m < 0 || ntok < 3 || end == tok[2] || *end)
            return fail(s, line, "mat M RRGGBB [flat]");
        if (ntok >= 4 && !strcmp(tok[3], "flat")) mat_flat(c, m, (uint32_t)rgb & 0xFFFFFF);
        else mat(c, m, (uint32_t)rgb & 0xFFFFFF);
        return 0;
    }
    if (!strcmp(w, "box")) {
        int m = ntok == 8 ? mat_index(tok[7]) : -1;
        if (m < 0 || !nums(tok, ntok, 1, 6, v)) return fail(s, line, "box x0 y0 z0 x1 y1 z1 M");
        box(c, v[0], v[1], v[2], v[3], v[4], v[5], m);
        return 0;
    }
    if (!strcmp(w, "bx")) {
        int m = ntok == 8 ? mat_index(tok[7]) : -1;
        if (m < 0 || !nums(tok, ntok, 1, 6, v)) return fail(s, line, "bx x y z w h d M");
        bx(c, v[0], v[1], v[2], v[3], v[4], v[5], m);
        return 0;
    }
    if (!strcmp(w, "tube")) {
        int m = ntok >= 11 ? mat_index(tok[10]) : -1;
        if (m < 0 || !nums(tok, ntok, 1, 9, v) || ntok > 12 || (ntok == 12 && strcmp(tok[11], "nocap")))
            return fail(s, line, "tube ax ay az bx by bz ra rb n M [nocap]");
        tube(c, v, v + 3, v[6], v[7], (int)v[8], m, ntok == 11);
        return 0;
    }
    if (!strcmp(w, "cyl")) {
        int m = ntok == 8 ? mat_index(tok[7]) : -1;
        if (m < 0 || !nums(tok, ntok, 1, 6, v)) return fail(s, line, "cyl x y z r h n M");
        cyl(c, v[0], v[1], v[2], v[3], v[4], (int)v[5], m);
        return 0;
    }
    if (!strcmp(w, "ell")) {
        int m = ntok == 9 ? mat_index(tok[8]) : -1;
        if (m < 0 || !nums(tok, ntok, 1, 7, v)) return fail(s, line, "ell x y z rx ry rz n M");
        ell(c, v[0], v[1], v[2], v[3], v[4], v[5], (int)v[6], m);
        return 0;
    }
    if (!strcmp(w, "prism")) {
        int axis = ntok >= 2 && tok[1][1] == 0 ? tok[1][0] - 'x' : -1;
        int m = ntok >= 5 ? mat_index(tok[4]) : -1;
        int n = (ntok - 5) / 2;
        float uv[32];
        if (axis < 0 || axis > 2 || m < 0 || !nums(tok, ntok, 2, 2, v) || n < 3 || n > 16 || (ntok - 5) % 2 ||
            !nums(tok, ntok, 5, n * 2, uv))
            return fail(s, line, "prism x|y|z w0 w1 M u v u v u v ... (3 to 16 corners)");
        prism(c, axis, uv, n, v[0], v[1], m);
        return 0;
    }
    if (!strcmp(w, "wedge")) {
        int m = ntok == 9 ? mat_index(tok[8]) : -1;
        if (m < 0 || !nums(tok, ntok, 1, 7, v)) return fail(s, line, "wedge x0 y0 z0 x1 z1 h0 h1 M");
        wedge(c, v[0], v[1], v[2], v[3], v[4], v[5], v[6], m);
        return 0;
    }
    if (!strcmp(w, "tf")) {
        if (!nums(tok, ntok, 1, 6, v) || ntok != 7) return fail(s, line, "tf x y z rx ry rz");
        tf_set(c, v[0], v[1], v[2], v[3], v[4], v[5]);
        return 0;
    }
    if (!strcmp(w, "tfoff")) {
        tf_off(c);
        return 0;
    }
    if (!strcmp(w, "bone")) {
        if (ntok != 9 || !nums(tok, ntok, 3, 6, v) || strlen(tok[1]) > 15)
            return fail(s, line, "bone NAME PARENT hx hy hz tx ty tz (a name of up to 15 letters)");
        int parent = strcmp(tok[2], "-") ? find_bone(c, tok[2]) : -1;
        if (parent < 0 && strcmp(tok[2], "-")) return fail(s, line, "bone: no such parent");
        if (find_bone(c, tok[1]) >= 0) return fail(s, line, "bone: that name is taken");
        if (c->m->nbones >= MESH_MAX_BONES) return fail(s, line, "too many bones");
        bone(c, tok[1], parent, v[0], v[1], v[2], v[3], v[4], v[5]);
        return 0;
    }
    if (!strcmp(w, "use")) {
        int b = ntok == 2 ? find_bone(c, tok[1]) : -1;
        if (b < 0) return fail(s, line, "use NAME: no such bone");
        use(c, b);
        return 0;
    }
    if (!strcmp(w, "side")) {
        s->side = c->m->nfaces;
        return 0;
    }
    if (!strcmp(w, "mirror")) {
        for (int i = 1; i < ntok; i++) {
            int b = find_bone(c, tok[i]);
            if (b < 0) return fail(s, line, "mirror: no such bone");
            if (c->bmir[b] != b) return fail(s, line, "mirror: already mirrored");
            bone_mirror(c, b);
        }
        if (s->side >= 0) mirror_x(c, s->side);
        s->side = -1;
        return 0;
    }
    if (!strcmp(w, "clip")) {
        if (ntok != 4 || !num(tok[2], v) || v[0] <= 0 || strlen(tok[1]) > 15 ||
            (strcmp(tok[3], "loop") && strcmp(tok[3], "once")))
            return fail(s, line, "clip NAME length loop|once");
        if (c->m->nclips >= MESH_MAX_CLIPS) return fail(s, line, "too many animations");
        s->clip = clip(c, tok[1], v[0], !strcmp(tok[3], "loop"));
        s->key = NULL;
        return 0;
    }
    if (!strcmp(w, "key")) {
        if (ntok != 2 || !num(tok[1], v) || !s->clip) return fail(s, line, "key t (after clip)");
        if (s->clip->nkeys >= MESH_MAX_KEYS) return fail(s, line, "too many keys in the animation");
        s->key = key(s->clip, v[0]);
        return 0;
    }
    if (!strcmp(w, "turn") || !strcmp(w, "shift")) {
        int b = ntok == 5 ? find_bone(c, tok[1]) : -1;
        if (b < 0 || !nums(tok, ntok, 2, 3, v) || !s->key) return fail(s, line, "turn|shift NAME x y z (after key)");
        if (w[0] == 't') turn(s->key, b, v[0], v[1], v[2]);
        else shift(s->key, b, v[0], v[1], v[2]);
        return 0;
    }
    return fail(s, line, "unknown statement");
}

int mesh_script(const char *text, mesh_model_t *out, char *err, int errlen)
{
    static mc_t c;
    static ms_t s;
    static char line[512];
    memset(&c, 0, sizeof c);
    memset(&s, 0, sizeof s);
    memset(out, 0, sizeof *out);
    c.m = out;
    c.seed = 1;
    c.rng = 12345;
    c.tall = c.wide = 1;
    c.rig = 1;
    mesh_reset_materials(&c);
    s.c = &c;
    s.side = -1;
    int n = 1;
    const char *p = text;
    int result = 0;
    while (*p && result == 0) {
        size_t len = strcspn(p, "\n");
        if (len >= sizeof line) {
            result = fail(&s, n, "line too long");
            break;
        }
        memcpy(line, p, len);
        line[len] = 0;
        p += len + (p[len] == '\n');
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *tok[MAXTOK];
        int ntok = 0;
        for (char *t = strtok(line, " \t\r"); t && ntok < MAXTOK; t = strtok(NULL, " \t\r"))
            tok[ntok++] = t;
        if (ntok > 0)
            result = statement(&s, n, tok, ntok);
        n++;
    }
    if (result == 0 && out->nfaces == 0)
        result = fail(&s, n, "no faces");
    if (result == 0)
        mesh_finish(out, 1, 1, 1, 1);
    if (err && errlen > 0) {
        strncpy(err, s.err, (size_t)errlen - 1);
        err[errlen - 1] = 0;
    }
    return result;
}
