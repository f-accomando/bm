/*
 * ai.music and ai.recipes("music") for Lua (lua_music.c): the music
 * assistant's recipes, with or without the assistant's network.
 */
#ifndef AI_LUA_MUSIC_H
#define AI_LUA_MUSIC_H

#include "lua.h"

int ai_lua_music(lua_State *L);
int ai_lua_music_recipes(lua_State *L);

/* the network's choice of a recipe for the words (NULL: the words alone) */
extern const char *(*ai_music_pick)(lua_State *L, const char *request);

#endif
