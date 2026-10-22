#!/bin/sh
# Host tests of the bmweb cart (carts/bmweb/src): pure Lua, no network.
#   sh tests/web/run.sh            run all tests
#   sh tests/web/run.sh --update   rewrite tests/web/expected/*.txt
# The Lua interpreter is, in order: $LUAHOST, build/host/luahost, lua5.4, lua,
# or one built from third_party/lua into a temp folder (never into build/).
set -e
cd "$(dirname "$0")/../.."
if [ -n "$LUAHOST" ]; then LUA="$LUAHOST"
elif [ -x build/host/luahost ]; then LUA=build/host/luahost
elif command -v lua5.4 >/dev/null 2>&1; then LUA=lua5.4
elif command -v lua >/dev/null 2>&1; then LUA=lua
else
  T="${TMPDIR:-/tmp}/bmweb-luahost"
  LUA="$T/luahost"
  if [ ! -x "$LUA" ]; then
    mkdir -p "$T"
    ${CC:-cc} -O2 -w -Ithird_party/lua -o "$LUA" tests/kitchen/luahost.c third_party/lua/*.c -lm
  fi
fi
"$LUA" tests/web/run.lua "$@"
# The cart (stages 2b-5): the browser state, loader, downloads and view against a mock kernel.
J="${TMPDIR:-/tmp}/bmweb-joined-$$.lua"
python3 carts/bmweb/build/build.py "$J" 2>/dev/null || python carts/bmweb/build/build.py "$J"
BMWEB_MAIN="$J" "$LUA" tests/web/cart.lua
rc=$?
rm -f "$J"
exit $rc
