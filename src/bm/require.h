#ifndef BM_REQUIRE_H
#define BM_REQUIRE_H

typedef struct lua_State lua_State;

/* require(name) for the Lua libraries built into the kernel (M30: "assist",
 * the assistant's panel). Only those: the sandbox still loads no files. */
void bm_require_open(lua_State *L);

#endif
