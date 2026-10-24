/* Host test: luavm_run loads text only. A binary chunk is refused (mode "t"),
 * a text chunk runs. Build: make test-luavm-text. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "lua.h"
#include "script/luavm.h"

static char last[512];

/* stub: the bm library of the kernel scripts is not needed here */
int luaopen_bm(lua_State *L)
{
    lua_newtable(L);
    return 1;
}

int kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(last, sizeof last, fmt, ap);
    va_end(ap);
    return 0;
}

int main(void)
{
    static const char bin[] = "\x1bLua\x54\x00\x19\x93\r\n\x1a\n junk";
    const char *ok = "x = 1";
    int fails = 0;

    if (!luavm_init()) {
        puts("luavm_init failed");
        return 1;
    }
    if (luavm_run(ok, strlen(ok), "=text") != LUA_OK) {
        puts("FAIL: text chunk did not run");
        fails++;
    }
    last[0] = 0;
    if (luavm_run(bin, sizeof bin - 1, "=bin") == LUA_OK || !strstr(last, "attempt to load a binary chunk")) {
        printf("FAIL: binary chunk not refused (%s)\n", last);
        fails++;
    }
    if (!fails)
        puts("luavm-text: 2/2 checks passed");
    return fails ? 1 : 0;
}
