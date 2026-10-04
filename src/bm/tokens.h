#ifndef BM_TOKENS_H
#define BM_TOKENS_H

#include <stddef.h>

/*
 * The tokens of a cartridge's Lua code, for the dev kit (the performance
 * overlay, stat(11), code_tokens() in the SDK). Information, not a limit:
 * bm cartridges have no token cap, and the .b16 format will not have one
 * either (docs/B16.md 2.4).
 *
 * Counted as in the fantasy consoles, so the number means the same thing:
 * every name, keyword, number, string and operator is one token, except
 *   - comments and spaces;
 *   - , . : ; :: and the closing ) ] };
 *   - the keywords end and local;
 *   - a minus sign in front of a number (-1 is one token, as is the number).
 * Long strings and long comments ([[ ]], [==[ ]==]) are understood. Code
 * that does not parse is still counted, token by token.
 */
int lua_tokens(const char *src, size_t n);

#endif
