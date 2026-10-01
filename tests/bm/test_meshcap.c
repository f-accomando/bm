/*
 * Host test of the mesh capture (src/bm/meshcap.c, cart_meshes() of bm
 * Mesh): the meshes a cartridge builds in its code, with their names.
 *
 *   test_meshcap src/bm/runtime.c CART.bm name,name,... [CART.bm names ...]
 *
 * The names of bm's functions come from the api[] table of runtime.c (all
 * of them get stand-ins, as on the console). Each cartridge must run with
 * no error and give at least the meshes named (a name followed by
 * ":faces" checks the number of faces too; "" none; "!text," first: the
 * code stops with an error containing text, at a line of main.lua); the
 * skeleton of each of its models must fit the model (the files bm Mesh
 * writes).
 */
#include "bm.h"
#include "meshcap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lualib.h"

static int checks, fails;

static void check(int ok, const char *what, const char *cart)
{
    checks++;
    if (!ok) {
        fails++;
        printf("FAIL %s: %s\n", cart, what);
    }
}

lua_State *bm_meshcap_newstate(void)
{
    return luaL_newstate();
}

static uint8_t *load(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return b;
}

static int dummy(lua_State *L) { (void)L; return 0; }

/* the names in `{ "name", l_name }` of runtime.c's api[] */
static luaL_Reg *api_names(const char *runtime)
{
    size_t len;
    char *src = (char *)load(runtime, &len);
    if (!src)
        return NULL;
    src[len] = 0;
    char *p = strstr(src, "static const luaL_Reg api[]");
    char *end = p ? strstr(p, "{ NULL, NULL }") : NULL;
    luaL_Reg *api = calloc(256, sizeof *api);
    int n = 0;
    while (p && end && p < end && n < 255) {
        char *q = strstr(p, "{ \"");
        if (!q || q > end)
            break;
        q += 3;
        char *e = strchr(q, '"');
        api[n].name = strndup(q, (size_t)(e - q));
        api[n].func = dummy;
        n++;
        p = e;
    }
    free(src);
    return api;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: test_meshcap runtime.c CART.bm names [CART.bm names ...]\n");
        return 2;
    }
    luaL_Reg *api = api_names(argv[1]);
    int napi = 0;
    while (api && api[napi].name)
        napi++;
    check(napi > 80 && api[napi - 1].name, "the names of bm's functions from runtime.c", argv[1]);
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    for (int a = 2; a + 1 < argc; a += 2) {
        const char *cart = argv[a];
        size_t len;
        uint8_t *data = load(cart, &len);
        bm_cart_t c;
        char err[64];
        check(data && bm_parse(data, len, &c, err, sizeof err) == 0, "the cartridge reads", cart);
        if (fails)
            break;
        /* the models (bm Mesh writes them): each skeleton fits its model */
        bm_model_t md;
        for (int i = 0; c.mesh && bm_mesh_model(c.mesh, c.mesh_size, i, &md) == 0; i++) {
            bm_rig_t r;
            if (c.anim && bm_anim_rig(c.anim, c.anim_size, md.name, &r) == 0) {
                char what[96];
                snprintf(what, sizeof what, "the skeleton of %s fits its %d vertices", md.name, md.nverts);
                check(r.nverts == md.nverts && r.nbones > 0, what, cart);
            }
        }
        int top = lua_gettop(L);
        bm_mesh_capture(L, c.lua, c.lua_size, c.width, c.height, api, c.mesh, c.mesh_size);
        const char *msg = lua_tostring(L, -1);
        char what[200];
        const char *names = argv[a + 1];
        if (names[0] == '!') {                  /* "!": it stops with an error, at a line */
            char text[64];
            snprintf(text, sizeof text, "%.*s", (int)strcspn(names + 1, ","), names + 1);
            snprintf(what, sizeof what, "the error of its code, with the line: %s", msg ? msg : "none");
            check(msg && strstr(msg, "main.lua:") && strstr(msg, text), what, cart);
            names = strchr(names, ',') ? strchr(names, ',') + 1 : "";
        } else {
            snprintf(what, sizeof what, "the code runs with no error: %s", msg ? msg : "");
            check(!msg, what, cart);
        }
        lua_pop(L, 1);
        int n = (int)luaL_len(L, -1);
        printf("%s: %d meshes:", cart, n);
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(L, -1, i);
            lua_getfield(L, -1, "name");
            lua_getfield(L, -2, "faces");
            lua_getfield(L, -3, "verts");
            lua_getfield(L, -4, "uv");
            printf(" %s(%dv %df%s)", lua_tostring(L, -4), (int)luaL_len(L, -2) / 3, (int)luaL_len(L, -3) / 4,
                   lua_istable(L, -1) ? " uv" : "");
            lua_pop(L, 5);
        }
        printf("\n");
        /* the names asked for */
        char *want = strdup(names);
        for (char *w = strtok(want, ","); w; w = strtok(NULL, ",")) {
            char *colon = strchr(w, ':');
            int faces = -1;
            if (colon) {
                *colon = 0;
                faces = atoi(colon + 1);
            }
            int found = 0;
            for (int i = 1; i <= n && !found; i++) {
                lua_rawgeti(L, -1, i);
                lua_getfield(L, -1, "name");
                lua_getfield(L, -2, "faces");
                if (!strcmp(lua_tostring(L, -2), w) && (faces < 0 || luaL_len(L, -1) / 4 == faces))
                    found = 1;
                lua_pop(L, 3);
            }
            snprintf(what, sizeof what, "a mesh named %s%s", w, faces >= 0 ? " with those faces" : "");
            check(found, what, cart);
        }
        free(want);
        lua_settop(L, top);
        free(data);
    }
    lua_close(L);
    printf("meshcap: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
