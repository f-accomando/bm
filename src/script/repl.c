/*
 * Lua REPL, modelled on lua.c:
 *   - an expression is printed (tries "return <line>" first)
 *   - an incomplete statement continues on a ">>" line
 *   - errors are printed with a traceback and the REPL goes on
 * Line editing: Backspace, Ctrl-C (discard), Ctrl-D (exit on empty line).
 */
#include "repl.h"
#include "luavm.h"
#include "kernel/input.h"
#include "lib/printf.h"

#include <stdio.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"

#define LINE_MAX    256
#define CHUNK_MAX   4096
#define EOFMARK     "<eof>"

static int want_exit;

static int l_exit(lua_State *L)
{
    (void)L;
    want_exit = 1;
    return 0;
}

/* Returns the line length, or -1 for Ctrl-D on an empty line. */
static int read_line(const char *prompt, char *buf)
{
    int n = 0;

    kprintf("%s", prompt);
    for (;;) {
        char c = input_getc();
        if (c == '\r' || c == '\n') {
            kprintf("\n");
            buf[n] = '\0';
            return n;
        }
        if (c == 0x04 && n == 0) {              /* Ctrl-D */
            kprintf("\n");
            return -1;
        }
        if (c == 0x03) {                        /* Ctrl-C */
            kprintf("^C\n%s", prompt);
            n = 0;
            continue;
        }
        if (c == 0x7F || c == 0x08) {           /* Backspace */
            if (n > 0) {
                n--;
                kprintf("\b \b");
            }
            continue;
        }
        if ((unsigned char)c >= 0x20 && n < LINE_MAX - 1) {
            buf[n++] = c;
            kprintf("%c", c);
        }
    }
}

static int msghandler(lua_State *L)
{
    const char *msg = lua_tostring(L, 1);
    if (!msg)
        msg = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
    luaL_traceback(L, L, msg, 1);
    return 1;
}

static int incomplete(lua_State *L, int status)
{
    if (status == LUA_ERRSYNTAX) {
        size_t lmsg;
        const char *msg = lua_tolstring(L, -1, &lmsg);
        if (lmsg >= sizeof(EOFMARK) - 1 &&
            strcmp(msg + lmsg - (sizeof(EOFMARK) - 1), EOFMARK) == 0) {
            lua_pop(L, 1);
            return 1;
        }
    }
    return 0;
}

static void print_results(lua_State *L, int base)
{
    int n = lua_gettop(L) - base;
    if (n <= 0)
        return;
    luaL_checkstack(L, LUA_MINSTACK, "too many results to print");
    lua_getglobal(L, "print");
    lua_insert(L, base + 1);
    if (lua_pcall(L, n, 0, 0) != LUA_OK)
        kprintf("\x1b[91merror calling print (%s)\x1b[0m\n", lua_tostring(L, -1));
}

static void eval(lua_State *L, char *chunk)
{
    static char line[LINE_MAX];
    int base = lua_gettop(L);
    size_t len = strlen(chunk);
    int status;

    /* Expression? */
    lua_pushfstring(L, "return %s;", chunk);
    status = luaL_loadbuffer(L, lua_tostring(L, -1), lua_rawlen(L, -1), "=stdin");
    lua_remove(L, -2);
    if (status != LUA_OK) {
        lua_pop(L, 1);
        /* Statement, possibly spanning several lines. */
        for (;;) {
            status = luaL_loadbuffer(L, chunk, len, "=stdin");
            if (!incomplete(L, status))
                break;
            int n = read_line(">> ", line);
            if (n < 0 || len + 1 + (size_t)n >= CHUNK_MAX) {
                lua_settop(L, base);
                return;
            }
            chunk[len++] = '\n';
            memcpy(chunk + len, line, (size_t)n + 1);
            len += (size_t)n;
        }
    }

    if (status == LUA_OK) {
        lua_pushcfunction(L, msghandler);
        lua_insert(L, base + 1);
        status = lua_pcall(L, 0, LUA_MULTRET, base + 1);
        lua_remove(L, base + 1);
    }
    fflush(stdout);
    if (status == LUA_OK)
        print_results(L, base);
    else
        kprintf("\x1b[91m%s\x1b[0m\n", lua_tostring(L, -1));
    fflush(stdout);
    lua_settop(L, base);
}

void repl_run(void)
{
    static char chunk[CHUNK_MAX];
    lua_State *L = luavm_state();

    if (!L) {
        kprintf("Lua is not available\n");
        return;
    }
    lua_pushcfunction(L, l_exit);
    lua_setglobal(L, "exit");
    want_exit = 0;

    kprintf("%s - Ctrl-D or exit() to leave\n", LUA_COPYRIGHT);
    while (!want_exit) {
        int n = read_line("lua> ", chunk);
        if (n < 0)
            break;
        if (n > 0)
            eval(L, chunk);
    }
}
