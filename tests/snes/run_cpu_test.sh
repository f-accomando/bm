#!/bin/sh
# Builds a standalone Lua interpreter from third_party/lua (if missing) and runs the CPU tests.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=${TMPDIR:-/tmp}/bm-snes-test
mkdir -p "$out"
if [ ! -x "$out/minilua" ]; then
  cp "$root"/third_party/lua/*.c "$root"/third_party/lua/*.h "$out"/
  cat > "$out/minilua.c" <<'C'
#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
int main(int argc, char **argv) {
  lua_State *L = luaL_newstate(); luaL_openlibs(L);
  if (luaL_dofile(L, argv[1])) { fprintf(stderr, "%s\n", lua_tostring(L, -1)); return 1; }
  return 0;
}
C
  (cd "$out" && gcc -O2 -DLUA_USE_POSIX -o minilua minilua.c $(ls l*.c | grep -v '^lua.c$') -lm)
fi
# The CPU file wraps its locals in a do-block; the test needs them, so the block is opened up.
python3 - "$root" "$out" <<'PY'
import sys
root, out = sys.argv[1:3]
spc = open(root + "/carts/snes/src/00_spc.lua").read()
core = spc + "\n" + open(root + "/carts/snes/src/01_core.lua").read()
cpu = open(root + "/carts/snes/src/02_cpu.lua").read()
cpu = cpu.replace("\ndo\n", "\n", 1).rstrip()
assert cpu.endswith("end")
cpu = cpu[:-3]
test = open(root + "/tests/snes/cpu_test.lua").read()
open(out + "/cpu_all.lua", "w").write(core + "\n" + cpu + "\n" + test)
PY
"$out/minilua" "$out/cpu_all.lua"
python3 - "$root" "$out" <<'PY'
import sys
root, out = sys.argv[1:3]
open(out + "/spc_all.lua", "w").write(open(root + "/carts/snes/src/00_spc.lua").read() + "\n" + open(root + "/tests/snes/spc_test.lua").read())
PY
"$out/minilua" "$out/spc_all.lua"
