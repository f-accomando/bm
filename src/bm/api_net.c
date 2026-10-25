/*
 * api_net.c: the network for the games (M38.5): UDP sockets (src/net/cartnet.h).
 * Moved from runtime.c (docs/NEXT.md, item 0b step 5). It shares the run
 * state through rt_internal.h.
 */
#ifdef BM_RGB30
#include "display.h"            /* fb_init_game */
#endif
#include "runtime.h"
#include "bm.h"
#include "gfx16.h"
#include "r3d.h"
#include "world3d.h"
#include "map3d.h"
#include "decimate.h"
#include "glb.h"
#include "cutout.h"
#include "platform.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "fs/fat.h"
#include "fs/sdlock.h"
#include "vid.h"
#include "rec.h"
#include "lib/crc32.h"
#include "gfx/console.h"
#include "gfx/font.h"
#include "lib/printf.h"
#include "script/luavm.h"
#include "loading.h"
#include "audio/audio.h"
#include "audio/player.h"
#include "audio/synth.h"
#include "audio/presets.h"
#include "audio/lua_tone.h"
#include "drivers/dma.h"
#include "gpu/gpu3d.h"
#include "gpu/version3d.h"
#include "arch/cache.h"
#include "n8lua.h"
#include "shot.h"
#include "webfile.h"
#include "webget.h"
#include "webimg.h"
#include "webpolicy.h"
#include "require.h"
#include "meshcap.h"
#include "tokens.h"
#include "profile.h"
#include "project.h"
#include "rt_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

/* ---- the network for the games (M38.5): UDP sockets (src/net/cartnet.h).
 * Addresses as text, "192.168.1.23"; "*" is the broadcast of the LAN. */

static void push_ip(lua_State *L, uint32_t ip)
{
    char t[16];
    ksnprintf(t, sizeof t, "%u.%u.%u.%u", (unsigned)(ip >> 24), (unsigned)(ip >> 16 & 255),
              (unsigned)(ip >> 8 & 255), (unsigned)(ip & 255));
    lua_pushstring(L, t);
}

/* udp_open([port]): a socket, or nil and why */
int l_udp_open(lua_State *L)
{
    int port = (int)luaL_optinteger(L, 1, 0);
    luaL_argcheck(L, port >= 0 && port <= 65535, 1, "a port is 0..65535");
    if (!cartnet_ip()) {
        lua_pushnil(L);
        lua_pushstring(L, "no network");
        return 2;
    }
    if (!perm_allowed(PERM_NET)) {
        lua_pushnil(L);
        lua_pushstring(L, "the player did not allow this game the network");
        return 2;
    }
    int sk = cartnet_open((uint16_t)port);
    if (sk < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "no free socket");
        return 2;
    }
    lua_pushinteger(L, sk);
    lua_pushinteger(L, cartnet_port(sk));
    return 2;
}

/* udp_send(s, address, port, data): true when sent */
int l_udp_send(lua_State *L)
{
    int sk = (int)luaL_checkinteger(L, 1);
    const char *to = luaL_checkstring(L, 2);
    int port = (int)luaL_checkinteger(L, 3);
    size_t len;
    const char *d = luaL_checklstring(L, 4, &len);
    luaL_argcheck(L, len <= CARTNET_MAX, 4, "at most 1024 bytes");
    uint32_t ip = strcmp(to, "*") == 0 ? CARTNET_BROADCAST : cartnet_resolve(to);
    lua_pushboolean(L, ip && (ip != CARTNET_BROADCAST || strcmp(to, "*") == 0) &&
                           cartnet_send(sk, ip, (uint16_t)port, d, (int)len) == 0);
    return 1;
}

/* udp_recv(s): data, address, port of the next packet, or nil */
int l_udp_recv(lua_State *L)
{
    static char buf[CARTNET_MAX];
    int sk = (int)luaL_checkinteger(L, 1);
    uint32_t ip;
    uint16_t port;
    int n = cartnet_recv(sk, buf, sizeof buf, &ip, &port);
    if (n < 0) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, buf, (size_t)n);
    push_ip(L, ip);
    lua_pushinteger(L, port);
    return 3;
}

int l_udp_close(lua_State *L)
{
    cartnet_close((int)luaL_checkinteger(L, 1));
    return 0;
}

/* net_ip(): the console's address, or nil without network */
int l_net_ip(lua_State *L)
{
    uint32_t ip = cartnet_ip();
    if (ip)
        push_ip(L, ip);
    else
        lua_pushnil(L);
    return 1;
}

/* net_resolve(name): its address once known; nil while looking (ask
 * again), false if there is no such name */
int l_net_resolve(lua_State *L)
{
    uint32_t ip = cartnet_resolve(luaL_checkstring(L, 1));
    if (ip == CARTNET_BROADCAST)
        lua_pushboolean(L, 0);
    else if (!ip)
        lua_pushnil(L);
    else
        push_ip(L, ip);
    return 1;
}
