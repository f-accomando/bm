#ifndef LUAVM_H
#define LUAVM_H

#include <stddef.h>
#include <stdint.h>

typedef struct lua_State lua_State;

/* Creates the global Lua state with the standard libraries and `bm33`. */
lua_State *luavm_init(void);
lua_State *luavm_state(void);

/* A fresh, empty state sharing the kernel's allocator and memory limit
 * (used for sandboxed cartridges). Close it with lua_close(). */
lua_State *luavm_newstate(void);

/* Compiles and runs a chunk in protected mode. Errors are printed with a
 * traceback (in red on the console) and never propagate. Returns 0 on
 * success. */
int luavm_run(const char *code, size_t len, const char *chunkname);

/* Bytes currently allocated by Lua, and the peak. */
size_t luavm_mem(void);
size_t luavm_mem_peak(void);

/* Registers the `bm33` module (script/lib_bm33.c). */
int luaopen_bm33(lua_State *L);

#endif
