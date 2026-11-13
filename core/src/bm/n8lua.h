/*
 * The Lua library "n8": the nano8 machine (n8.h), its cartridges
 * (n8cart.h) and its sound (audio/n8snd.h) for the nano8 cartridge. The
 * functions with the carts' names (spr, map, print, peek...) take the
 * carts' arguments; the others load, start and show a cart.
 */
#ifndef N8LUA_H
#define N8LUA_H

#include <stddef.h>
#include <stdint.h>

#include "gfx16.h"
#include "lua.h"

typedef struct {
    /* a whole file (malloc'd; the caller frees). 0 = read. */
    int (*load)(const char *path, uint8_t **data, size_t *len);
    /* the frame being drawn (the cartridge's screen) */
    g16_t *(*target)(void);
} n8lua_io_t;

void n8lua_set_io(const n8lua_io_t *io);
/* Pushes the library table. */
int  luaopen_n8(lua_State *L);
/* Stops the sound and frees the machine (the cartridge closes). */
void n8lua_close(void);

#endif
