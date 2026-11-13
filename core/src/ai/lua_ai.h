#ifndef AI_LUA_AI_H
#define AI_LUA_AI_H

typedef struct lua_State lua_State;

/* the global table `ai` (the assistant) of a cartridge's Lua state */
void ai_lua_open(lua_State *L);

#endif
