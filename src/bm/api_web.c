/*
 * api_web.c: the web (web_get, web_poll, web_info, image_load, image_file, sheet_png, sheet_gif, file_write, file_state).
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

/* ---- the web (bm3D 7.x): web_get, image_load, file_write for a browser cart.
 * The rules are in webpolicy.c, the request in webget.c, the pictures in webimg.c;
 * docs/api/system.md "The web". ---- */

static int web_nil(lua_State *L, const char *why)
{
    lua_pushnil(L);
    lua_pushstring(L, why);
    return 2;
}

/* web_get(url, [{max = bytes, timeout = ms, resume = "NAME.EXT"}]) -> id, or nil and why. https only.
 * resume: a bare 8.3 name in bm/web; a request that failed or was cancelled leaves name.prt there
 * and the next web_get for the same address with the same resume name asks the server for the rest. */
int l_web_get(lua_State *L)
{
    const char *url = luaL_checkstring(L, 1);
    size_t cap = web_cap_bytes(config_get("web_max_kib"), WEB_MAX_KIB_DEFAULT, WEB_MAX_KIB_LIMIT);
    uint32_t timeout = 15000;
    char resume[40] = "";
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "resume");
        if (lua_type(L, -1) == LUA_TSTRING)
            snprintf(resume, sizeof resume, "%s", lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, 2, "max");
        if (lua_isnumber(L, -1) && lua_tonumber(L, -1) >= 1 && (lua_Number)cap > lua_tonumber(L, -1))
            cap = (size_t)lua_tonumber(L, -1);      /* a cartridge asks for less, never more */
        lua_getfield(L, 2, "timeout");
        if (lua_isnumber(L, -1)) {
            const lua_Number t = lua_tonumber(L, -1);
            timeout = (uint32_t)(t < 1000 ? 1000 : t > 60000 ? 60000 : t);
        }
        lua_pop(L, 2);
    }
    char err[200];
    if (web_url_check(url, NULL, 0, err, sizeof err))
        return web_nil(L, err);
    if (!cartnet_ip())
        return web_nil(L, "no network");
    if (!perm_allowed(PERM_NET))
        return web_nil(L, "the player did not allow this game the network");
    const int id = web_start_resume(url, cap, timeout, resume[0] ? resume : NULL, err, sizeof err);
    if (id < 0)
        return web_nil(L, err);
    lua_pushinteger(L, id);
    return 1;
}

static const char *const web_state_names[] = { "free", "wait", "busy", "done", "error" };

/* web_poll(id) -> "wait" (the pause per host), "busy", "done" or "error", the bytes so far
 * and the Content-Length (nil if unknown); nil and why for an id that is not there. Call it
 * once a frame: it works the request for about 4 ms. */
int l_web_poll(lua_State *L)
{
    const int id = (int)luaL_checkinteger(L, 1);
    const web_state_t st = web_poll(id);
    if (st == WEB_FREE)
        return web_nil(L, "no such request");
    const web_req_t *r = web_find(id);
    lua_pushstring(L, web_state_names[st]);
    lua_pushinteger(L, (lua_Integer)r->size);
    if (r->length >= 0)
        lua_pushinteger(L, (lua_Integer)r->length);
    else
        lua_pushnil(L);
    return 3;
}

/* web_info(id) -> { state =, status =, type =, length =, location =, url =, size =, error = }
 * (the fields that are not known are left out); nil and why if there is no such request */
int l_web_info(lua_State *L)
{
    const web_req_t *r = web_find((int)luaL_checkinteger(L, 1));
    if (!r)
        return web_nil(L, "no such request");
    lua_createtable(L, 0, 8);
    lua_pushstring(L, web_state_names[r->state]);
    lua_setfield(L, -2, "state");
    if (r->status) {
        lua_pushinteger(L, r->status);
        lua_setfield(L, -2, "status");
    }
    if (r->type[0]) {
        lua_pushstring(L, r->type);
        lua_setfield(L, -2, "type");
    }
    if (r->length >= 0) {
        lua_pushinteger(L, (lua_Integer)r->length);
        lua_setfield(L, -2, "length");
    }
    if (r->location[0]) {
        lua_pushstring(L, r->location);
        lua_setfield(L, -2, "location");
    }
    lua_pushstring(L, r->url);
    lua_setfield(L, -2, "url");
    lua_pushinteger(L, (lua_Integer)r->size);
    lua_setfield(L, -2, "size");
    if (r->resumed) {
        lua_pushinteger(L, (lua_Integer)r->resumed);
        lua_setfield(L, -2, "resumed");         /* bytes that came from the part file */
    }
    if (r->error[0]) {
        lua_pushstring(L, r->error);
        lua_setfield(L, -2, "error");
    }
    return 1;
}

/* web_body(id) -> the body (a string) of a finished request, or nil and why */
int l_web_body(lua_State *L)
{
    const web_req_t *r = web_find((int)luaL_checkinteger(L, 1));
    if (!r)
        return web_nil(L, "no such request");
    if (r->state != WEB_DONE)
        return web_nil(L, r->state == WEB_ERROR ? r->error : "the request is not finished");
    lua_pushlstring(L, r->body ? (const char *)r->body : "", r->size);
    return 1;
}

/* web_cancel(id): stops the request if it runs and frees its result. true if it was there. */
int l_web_cancel(lua_State *L)
{
    const int id = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, web_find(id) != NULL);
    web_cancel(id);
    return 1;
}

/* the bytes of argument idx: a string, or the id of a finished web_get (its body, not copied) */
static const uint8_t *web_bytes(lua_State *L, int idx, size_t *len, const char **why)
{
    if (lua_type(L, idx) == LUA_TNUMBER) {
        const web_req_t *r = web_find((int)luaL_checkinteger(L, idx));
        if (!r || r->state != WEB_DONE) {
            *why = !r ? "no such request" : "the request is not finished";
            return NULL;
        }
        *len = r->size;
        return r->body ? r->body : (const uint8_t *)"";
    }
    return (const uint8_t *)luaL_checklstring(L, idx, len);
}

/* image_load(bytes_or_id, sx, sy, [maxw, maxh]) -> { x =, y =, w =, h = }, or nil and why.
 * Decodes a JPEG (baseline), PNG or WebP into the sprite sheet with its top left corner at
 * (sx, sy); what does not fit the sheet or maxw x maxh is cut at the right and the bottom.
 * Draw it with sspr(img.x, img.y, img.w, img.h, ...). */
int l_image_load(lua_State *L)
{
    size_t len = 0;
    const char *why = NULL;
    const uint8_t *data = web_bytes(L, 1, &len, &why);
    if (!data)
        return web_nil(L, why);
    const int sx = ival(L, 2), sy = ival(L, 3);
    if (!rt.sheet.px || sx < 0 || sy < 0 || sx >= rt.sheet.w || sy >= rt.sheet.h)
        return web_nil(L, "outside the sprite sheet (cart_sheet(w, h) makes it bigger)");
    int maxw = oval(L, 4, 0), maxh = oval(L, 5, 0);
    if (maxw <= 0 || maxw > rt.sheet.w - sx)
        maxw = rt.sheet.w - sx;
    if (maxh <= 0 || maxh > rt.sheet.h - sy)
        maxh = rt.sheet.h - sy;
    webimg_t im;
    char err[96];
    if (webimg_decode(data, len, maxw, maxh, &im, err, sizeof err) != 0)
        return web_nil(L, err);
    later_paint();                      /* the frame held's sprites as they were (bm3d 6.9) */
    sync3d();                           /* the waiting 3D may use the sheet as texture */
    for (int y = 0; y < im.h; y++)
        for (int x = 0; x < im.w; x++)
            g16_sheet_set(&rt.sheet, sx + x, sy + y, im.px[(size_t)y * im.w + x], im.alpha[(size_t)y * im.w + x]);
    const int per = rt.sheet.w / G16_CELL;
    for (int cy = sy / G16_CELL; cy <= (sy + im.h - 1) / G16_CELL; cy++)
        for (int cx = sx / G16_CELL; cx <= (sx + im.w - 1) / G16_CELL; cx++)
            rt.cell_dirty[cy * per + cx] = 1;
    rt.sheet_dirty = 1;
    proj_sheet_drawn = 1;
    sheet_commit();
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, sx);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, sy);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, im.w);
    lua_setfield(L, -2, "w");
    lua_pushinteger(L, im.h);
    lua_setfield(L, -2, "h");
    webimg_free(&im);
    return 1;
}

/* sheet_png(x, y, w, h) or sheet_png({ {x, y, w, h}, ... }) -> PNG bytes (a string, RGBA: a transparent pixel of
 * the sheet has alpha 0), or nil and why. The list puts the rectangles side by side, left to right; all have
 * the same h. The total size is at most 4096 a side. The pixels are copied to a buffer that is freed before
 * the call returns. */
int l_sheet_png(lua_State *L)
{
    int r[64][4], n = 0;
    if (lua_istable(L, 1)) {
        const int cnt = (int)lua_rawlen(L, 1);
        if (cnt < 1 || cnt > 64)
            return web_nil(L, "the list has 1 to 64 rectangles");
        for (int i = 0; i < cnt; i++) {
            lua_rawgeti(L, 1, i + 1);
            if (!lua_istable(L, -1))
                return luaL_error(L, "sheet_png: item %d is not {x, y, w, h}", i + 1);
            for (int k = 0; k < 4; k++) {
                lua_rawgeti(L, -1, k + 1);
                r[n][k] = (int)luaL_checkinteger(L, -1);
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
            n++;
        }
    } else {
        for (int k = 0; k < 4; k++)
            r[0][k] = ival(L, k + 1);
        n = 1;
    }
    if (!rt.sheet.px)
        return web_nil(L, "no sprite sheet");
    long long total = 0;
    for (int i = 0; i < n; i++) {
        const int x = r[i][0], y = r[i][1], w = r[i][2], h = r[i][3];
        if (w < 1 || h < 1 || x < 0 || y < 0 || x > rt.sheet.w - w || y > rt.sheet.h - h)
            return web_nil(L, "outside the sprite sheet");
        if (h != r[0][3])
            return web_nil(L, "the rectangles must have the same height");
        total += w;
    }
    if (total > 4096 || r[0][3] > 4096)
        return web_nil(L, "more than 4096 pixels a side");
    const int tw = (int)total, th = r[0][3];
    uint16_t *px = malloc((size_t)tw * (size_t)th * sizeof *px);
    uint8_t *al = malloc((size_t)tw * (size_t)th);
    if (!px || !al) {
        free(px);
        free(al);
        return web_nil(L, "no memory");
    }
    int ox = 0;
    for (int i = 0; i < n; i++) {
        for (int y = 0; y < th; y++) {
            const size_t s = (size_t)(r[i][1] + y) * (size_t)rt.sheet.w + (size_t)r[i][0];
            memcpy(px + (size_t)y * (size_t)tw + (size_t)ox, rt.sheet.px + s, (size_t)r[i][2] * sizeof *px);
            memcpy(al + (size_t)y * (size_t)tw + (size_t)ox, rt.sheet.alpha + s, (size_t)r[i][2]);
        }
        ox += r[i][2];
    }
    size_t len = 0;
    uint8_t *png = shot_png_rgba(px, al, tw, tw, th, &len);
    free(px);
    free(al);
    if (!png)
        return web_nil(L, "no memory");
    lua_pushlstring(L, (const char *)png, len);
    free(png);
    return 1;
}

/* sheet_gif({ {x, y, w, h}, ... }, delay_cs) -> the bytes of an animated GIF (a string), or nil and why.
 * One frame a rectangle, in the order given; all the same size; a transparent pixel of the sheet stays
 * transparent. delay_cs: hundredths of a second a frame (default 10). The pixels are copied to buffers
 * freed before the call returns; nothing runs until the call. */
int l_sheet_gif(lua_State *L)
{
    int r[64][4], n = 0;
    luaL_checktype(L, 1, LUA_TTABLE);
    const int cnt = (int)lua_rawlen(L, 1);
    if (cnt < 1 || cnt > 64)
        return web_nil(L, "the list has 1 to 64 rectangles");
    for (int i = 0; i < cnt; i++) {
        lua_rawgeti(L, 1, i + 1);
        if (!lua_istable(L, -1))
            return luaL_error(L, "sheet_gif: item %d is not {x, y, w, h}", i + 1);
        for (int k = 0; k < 4; k++) {
            lua_rawgeti(L, -1, k + 1);
            r[n][k] = (int)luaL_checkinteger(L, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        n++;
    }
    const int delay = oval(L, 2, 10);
    if (!rt.sheet.px)
        return web_nil(L, "no sprite sheet");
    for (int i = 0; i < n; i++) {
        const int x = r[i][0], y = r[i][1], w = r[i][2], h = r[i][3];
        if (w < 1 || h < 1 || x < 0 || y < 0 || x > rt.sheet.w - w || y > rt.sheet.h - h)
            return web_nil(L, "outside the sprite sheet");
        if (w != r[0][2] || h != r[0][3])
            return web_nil(L, "the rectangles must have the same size");
    }
    const int fw = r[0][2], fh = r[0][3];
    if (fw > 4096 || fh > 4096 || (long long)fw * fh * n > 4096LL * 64)
        return web_nil(L, "too big: at most 262144 pixels in all the frames");
    const size_t fpix = (size_t)fw * (size_t)fh;
    uint16_t *px = malloc(fpix * (size_t)n * sizeof *px);
    uint8_t *al = malloc(fpix * (size_t)n);
    if (!px || !al) {
        free(px);
        free(al);
        return web_nil(L, "no memory");
    }
    for (int i = 0; i < n; i++)
        for (int y = 0; y < fh; y++) {
            const size_t s = (size_t)(r[i][1] + y) * (size_t)rt.sheet.w + (size_t)r[i][0];
            const size_t d = (size_t)i * fpix + (size_t)y * (size_t)fw;
            memcpy(px + d, rt.sheet.px + s, (size_t)fw * sizeof *px);
            memcpy(al + d, rt.sheet.alpha + s, (size_t)fw);
        }
    size_t len = 0;
    const char *why = NULL;
    uint8_t *gif = shot_gif(px, al, fw, fh, n, delay, &len, &why);
    free(px);
    free(al);
    if (!gif)
        return web_nil(L, why ? why : "no memory");
    lua_pushlstring(L, (const char *)gif, len);
    free(gif);
    return 1;
}

/* image_file(path, sx, sy, [fitw, fith, mix, bg]) -> { x =, y =, w =, h =, sw =, sh = }, or nil and why.
 * Reads a JPEG (baseline), PNG or WebP file of the SD card (up to 16 MB, 4 megapixels), makes it fit
 * fitw x fith (default 256 x 256; the picture keeps its shape and is never made bigger; a box average)
 * and puts it on the sprite sheet with its top left corner at (sx, sy). mix (0 to 100, default 100) is the
 * share of the picture kept: the rest is the colour bg (0xRRGGBB, default black), so a reference
 * picture can be drawn pale. w, h: the pixels used; sw, sh: the size of the file's picture. The decoded
 * picture is freed before the call returns: only the small copy stays in the sheet. */
int l_image_file(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    const int sx = ival(L, 2), sy = ival(L, 3);
    int fw = oval(L, 4, 256), fh = oval(L, 5, 256);
    int mix = oval(L, 6, 100);
    const uint32_t bg = (uint32_t)luaL_optinteger(L, 7, 0);
    if (!rt.sheet.px || sx < 0 || sy < 0 || sx >= rt.sheet.w || sy >= rt.sheet.h)
        return web_nil(L, "outside the sprite sheet (cart_sheet(w, h) makes it bigger)");
    if (mix < 0)
        mix = 0;
    if (mix > 100)
        mix = 100;
    if (fw < 1 || fw > rt.sheet.w - sx)
        fw = rt.sheet.w - sx;
    if (fh < 1 || fh > rt.sheet.h - sy)
        fh = rt.sheet.h - sy;
    fat_entry_t e;
    uint8_t *data = NULL;
    size_t len = 0;
    memset(&e, 0, sizeof e);
    if (fat_find(path, &e) != 0 || e.is_dir) {
        lua_pushnil(L);
        lua_pushfstring(L, "%s: %s", path, e.is_dir ? "a directory" : fat_error());
        return 2;
    }
    if (e.size > (16u << 20)) {
        lua_pushnil(L);
        lua_pushfstring(L, "%s: more than 16 MB", path);
        return 2;
    }
    if (fat_load(&e, &data, &len) != 0) {
        lua_pushnil(L);
        lua_pushfstring(L, "%s: %s", path, fat_error());
        return 2;
    }
    webimg_t im;
    char err[96];
    const int rc = webimg_decode(data, len, 0, 0, &im, err, sizeof err);
    free(data);
    if (rc != 0)
        return web_nil(L, err);
    int dw = im.w, dh = im.h;
    if (dw > fw || dh > fh) {
        if ((long long)fw * dh <= (long long)fh * dw) {
            dh = (int)((long long)dh * fw / dw);
            dw = fw;
        } else {
            dw = (int)((long long)dw * fh / dh);
            dh = fh;
        }
        if (dw < 1)
            dw = 1;
        if (dh < 1)
            dh = 1;
    }
    later_paint();
    sync3d();
    const int br = (int)((bg >> 16) & 255), bgc = (int)((bg >> 8) & 255), bb = (int)(bg & 255);
    for (int y = 0; y < dh; y++) {
        const int y0 = (int)((long long)y * im.h / dh);
        int y1 = (int)((long long)(y + 1) * im.h / dh);
        if (y1 <= y0)
            y1 = y0 + 1;
        for (int x = 0; x < dw; x++) {
            const int x0 = (int)((long long)x * im.w / dw);
            int x1 = (int)((long long)(x + 1) * im.w / dw);
            if (x1 <= x0)
                x1 = x0 + 1;
            int r = 0, g = 0, b = 0, n = 0, on = 0;
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++) {
                    const size_t k = (size_t)yy * im.w + xx;
                    const uint16_t c = im.px[k];
                    r += ((c >> 11) & 31) << 3;
                    g += ((c >> 5) & 63) << 2;
                    b += (c & 31) << 3;
                    on += im.alpha[k] ? 1 : 0;
                    n++;
                }
            r = (r / n * mix + br * (100 - mix)) / 100;
            g = (g / n * mix + bgc * (100 - mix)) / 100;
            b = (b / n * mix + bb * (100 - mix)) / 100;
            g16_sheet_set(&rt.sheet, sx + x, sy + y, (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)),
                          on * 2 >= n);
        }
    }
    const int per = rt.sheet.w / G16_CELL;
    for (int cy = sy / G16_CELL; cy <= (sy + dh - 1) / G16_CELL; cy++)
        for (int cx = sx / G16_CELL; cx <= (sx + dw - 1) / G16_CELL; cx++)
            rt.cell_dirty[cy * per + cx] = 1;
    rt.sheet_dirty = 1;
    proj_sheet_drawn = 1;
    sheet_commit();
    lua_createtable(L, 0, 6);
    lua_pushinteger(L, sx);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, sy);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, dw);
    lua_setfield(L, -2, "w");
    lua_pushinteger(L, dh);
    lua_setfield(L, -2, "h");
    lua_pushinteger(L, im.w);
    lua_setfield(L, -2, "sw");
    lua_pushinteger(L, im.h);
    lua_setfield(L, -2, "sh");
    webimg_free(&im);
    return 1;
}

/* A model file (a name that ends in .bmm) does not go to bm/web: it goes to /bmm/NAME.bmm, where the
 * editors list it, as res_write puts the models of bm's tools. The bytes are checked first as the kernel loads
 * a model file (bm_parse_any: BMRES, kind 1, a MESH section that bm_mesh_check accepts); a file that fails
 * is refused and nothing is written, not even in bm/web. The name is the one web_file_name gave (1 to 8 of
 * a-z 0-9 _ -, lower case). A file that is there is never replaced (res_write without replace). It is always
 * written at once (a model is small), so file_state has nothing for it. No question to the player: the
 * file is data with no code, it takes no name that is there, and the cap and the checks above hold. */
static int file_write_model(lua_State *L, const char *name, const uint8_t *data, size_t len, size_t cap)
{
    char err[96] = "", path[32];
    if (len > cap)
        return web_nil(L, "the file is over the cap (file_write_max_kib in bm/config.txt)");
    bm_cart_t c;
    if (bm_parse_any(data, len, &c, err, sizeof err) != 0)
        return web_nil(L, err[0] ? err : "not a model file (BMRES)");
    if (c.kind != BM_RES_MODEL || !c.mesh || bm_mesh_check(c.mesh, c.mesh_size) < 0)
        return web_nil(L, "a .bmm file is a BMRES model file (kind 1) with a MESH section");
    ksnprintf(path, sizeof path, "/bmm/%s", name);
    fat_entry_t e;
    memset(&e, 0, sizeof e);
    if (fat_find(path, &e) == 0)
        return web_nil(L, e.is_dir ? "that name is a folder" : "a model of that name is there already");
    if (sd_write_file("file_write", "/bmm", name, data, len) != 0)
        return web_nil(L, fat_error());
    lua_pushboolean(L, 1);
    lua_pushstring(L, path + 1);
    return 2;
}

/* file_write(name, bytes_or_id, [async]) -> true and the path ("bm/web/name.ext"), or nil and why.
 * Writes a file in bm/web on the SD card (made if it is not there): an 8.3 name, no other
 * folder, never .bm, .bme or .b16, at most file_write_max_kib (8 MB) and 128 files in the
 * folder. A name that ends in .bmm is a model: file_write_model (it goes to /bmm, checked, never
 * replaced; the path is "bmm/name.bmm"). Another name that is there is replaced. Without async the call waits for the card: a big
 * file stops the frame for the time of the write. With async = true the data is copied and
 * queued: the call returns at once with true, and the file goes to the card in pieces while the
 * cartridge polls (web_poll, file_state, about 3-4 ms each); file_state(name) says when. One
 * SD writer at a time (sdlock.h): another writer pauses the queued one, which starts again. */
int l_file_write(lua_State *L)
{
    char name[13], err[96];
    if (web_file_name(luaL_checkstring(L, 1), name, err, sizeof err) != 0)
        return web_nil(L, err);
    size_t len = 0;
    const char *why = NULL;
    const uint8_t *data = web_bytes(L, 2, &len, &why);
    if (!data)
        return web_nil(L, why);
    const size_t cap = web_cap_bytes(config_get("file_write_max_kib"), FILE_MAX_KIB_DEFAULT, FILE_MAX_KIB_LIMIT);
    const size_t nl = strlen(name);
    if (nl > 4 && strcmp(name + nl - 4, ".bmm") == 0)
        return file_write_model(L, name, data, len, cap);
    const int rc = lua_toboolean(L, 3) ? webfile_queue(name, data, len, cap, err, sizeof err)
                                       : webfile_write(name, data, len, cap, err, sizeof err);
    if (rc != 0)
        return web_nil(L, err);
    char path[32];
    ksnprintf(path, sizeof path, "%s/%s", WEB_FILES_DIR, name);
    lua_pushboolean(L, 1);
    lua_pushstring(L, path + 1);
    return 2;
}

/* file_state(name) -> "queued", "writing" (and the bytes on the card), "done" (and the size) or
 * "error" (and why) for the last async file_write of that name; nil if there is none. It also
 * works the queue for about 3 ms. */
int l_file_state(lua_State *L)
{
    char name[13], err[96] = "";
    if (web_file_name(luaL_checkstring(L, 1), name, err, sizeof err) != 0)
        return web_nil(L, err);
    webfile_pump(3000);
    size_t done = 0;
    static const char *const names[] = { "", "queued", "writing", "done", "error" };
    const wf_state_t st = webfile_state(name, err, sizeof err, &done);
    if (st == WF_NONE)
        return web_nil(L, "no such write");
    lua_pushstring(L, names[st]);
    if (st == WF_ERROR)
        lua_pushstring(L, err);
    else
        lua_pushinteger(L, (lua_Integer)done);
    return 2;
}
