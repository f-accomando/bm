/*
 * Small integer networks for the cartridges (see net.h): the blob is
 * checked and copied into the userdata (4-byte aligned rows for the SIMD
 * sums of nn.c), then every run is a few dense layers of INT8.
 */
#include "net.h"
#include "nn.h"

#include <string.h>

#include "lauxlib.h"

#define NET_MT "bm.nnet"
#define MAX_LAYERS 8
#define MAX_WIDTH 256

typedef struct {
    int nin, nin4, nout, relu, shift;
    int32_t mult;
    const int32_t *bias;
    const int8_t *w;
} layer_t;

typedef struct {
    int nin, nout, nl;
    float in_scale, out_scale;
    layer_t l[MAX_LAYERS];
    /* biases and weights follow (4-byte aligned) */
} nnet_t;

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }
static float rdf(const uint8_t *p)
{
    uint32_t u = rd32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static int l_nnet(lua_State *L)
{
    size_t len;
    const uint8_t *b = (const uint8_t *)luaL_checklstring(L, 1, &len);
    if (len < 16 || memcmp(b, "BMNN", 4) || b[4] != 1)
        return luaL_error(L, "nnet: not a network (BMNN 1)");
    int nl = b[5], nin = (int)rd16(b + 6);
    if (nl < 1 || nl > MAX_LAYERS || nin < 1 || nin > MAX_WIDTH)
        return luaL_error(L, "nnet: %d layers of %d inputs", nl, nin);
    /* first pass: the sizes */
    size_t off = 16, data = 0;
    int width = nin;
    for (int i = 0; i < nl; i++) {
        if (off + 8 > len)
            return luaL_error(L, "nnet: cut short");
        int nout = (int)rd16(b + off), relu = b[off + 2], shift = b[off + 3];
        int nin4 = (width + 3) & ~3;
        if (nout < 1 || nout > MAX_WIDTH || relu > 1 || (relu && (shift < 1 || shift > 40)) ||
            (relu == 0 && i != nl - 1) || (relu && i == nl - 1))
            return luaL_error(L, "nnet: layer %d is wrong", i + 1);
        size_t sz = (size_t)nout * 4 + (size_t)nout * (size_t)nin4;
        if (off + 8 + sz > len)
            return luaL_error(L, "nnet: cut short");
        off += 8 + sz;
        data += sz;
        width = nout;
    }
    if (off != len)
        return luaL_error(L, "nnet: %d bytes too many", (int)(len - off));
    nnet_t *n = (nnet_t *)lua_newuserdatauv(L, sizeof(nnet_t) + data, 0);
    uint8_t *mem = (uint8_t *)(n + 1);
    n->nin = nin;
    n->nl = nl;
    n->in_scale = rdf(b + 8);
    n->out_scale = rdf(b + 12);
    off = 16;
    width = nin;
    for (int i = 0; i < nl; i++) {
        layer_t *ly = &n->l[i];
        ly->nout = (int)rd16(b + off);
        ly->relu = b[off + 2];
        ly->shift = b[off + 3];
        ly->mult = (int32_t)rd32(b + off + 4);
        ly->nin = width;
        ly->nin4 = (width + 3) & ~3;
        off += 8;
        int32_t *bias = (int32_t *)(void *)mem;
        for (int k = 0; k < ly->nout; k++)
            bias[k] = (int32_t)rd32(b + off + (size_t)k * 4);
        off += (size_t)ly->nout * 4;
        mem += (size_t)ly->nout * 4;
        size_t wsz = (size_t)ly->nout * (size_t)ly->nin4;
        memcpy(mem, b + off, wsz);
        ly->bias = bias;
        ly->w = (const int8_t *)mem;
        off += wsz;
        mem += (wsz + 3) & ~(size_t)3;
        width = ly->nout;
    }
    n->nout = width;
    luaL_setmetatable(L, NET_MT);
    return 1;
}

/* the outputs for the inputs at stack index 2, into out[] */
static void forward(lua_State *L, const nnet_t *n, float *out)
{
    static int8_t x[MAX_WIDTH] __attribute__((aligned(4)));
    static int32_t acc[MAX_WIDTH];
    luaL_checktype(L, 2, LUA_TTABLE);
    memset(x, 0, sizeof x);
    for (int i = 0; i < n->nin; i++) {
        lua_rawgeti(L, 2, i + 1);
        float v = (float)lua_tonumber(L, -1) * n->in_scale;
        lua_pop(L, 1);
        int q = (int)(v + (v < 0 ? -0.5f : 0.5f));
        x[i] = (int8_t)(q < -127 ? -127 : q > 127 ? 127 : q);
    }
    for (int i = 0; i < n->nl; i++) {
        const layer_t *ly = &n->l[i];
        nn_dense(ly->w, ly->bias, x, ly->nin4, ly->nout, acc);
        if (ly->relu) {
            memset(x, 0, sizeof x);
            nn_relu_q(acc, ly->nout, ly->mult, ly->shift, x);
        }
    }
    for (int k = 0; k < n->nout; k++)
        out[k] = (float)acc[k] * n->out_scale;
}

static int l_run(lua_State *L)
{
    const nnet_t *n = (const nnet_t *)luaL_checkudata(L, 1, NET_MT);
    float out[MAX_WIDTH];
    forward(L, n, out);
    if (lua_istable(L, 3))
        lua_pushvalue(L, 3);
    else
        lua_createtable(L, n->nout, 0);
    for (int k = 0; k < n->nout; k++) {
        lua_pushnumber(L, out[k]);
        lua_rawseti(L, -2, k + 1);
    }
    return 1;
}

static int l_pick(lua_State *L)
{
    const nnet_t *n = (const nnet_t *)luaL_checkudata(L, 1, NET_MT);
    float out[MAX_WIDTH];
    forward(L, n, out);
    const int mask = lua_istable(L, 3);
    int best = -1;
    for (int k = 0; k < n->nout; k++) {
        if (mask) {
            lua_rawgeti(L, 3, k + 1);
            int ok = lua_isnil(L, -1) || lua_toboolean(L, -1);
            lua_pop(L, 1);
            if (!ok)
                continue;
        }
        if (best < 0 || out[k] > out[best])
            best = k;
    }
    if (best < 0) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, best + 1);
    lua_pushnumber(L, out[best]);
    return 2;
}

static int l_size(lua_State *L)
{
    const nnet_t *n = (const nnet_t *)luaL_checkudata(L, 1, NET_MT);
    lua_pushinteger(L, n->nin);
    lua_pushinteger(L, n->nout);
    return 2;
}

void nnet_lua_open(lua_State *L)
{
    static const luaL_Reg methods[] = { { "run", l_run }, { "pick", l_pick }, { "size", l_size }, { NULL, NULL } };
    luaL_newmetatable(L, NET_MT);
    luaL_newlib(L, methods);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
    lua_pushcfunction(L, l_nnet);
    lua_setglobal(L, "nnet");
}
