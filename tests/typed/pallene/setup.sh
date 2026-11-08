#!/bin/sh
# Builds Pallene (the typed Lua that compiles to C) for the trial of docs/design/typed-native.md.
#
#   setup.sh WORKDIR
#
# Everything goes into WORKDIR (outside the repository); WORKDIR/prefix/bin/pallenec is the compiler.
# What it does, and why:
#   1. clones Pallene, the mirror of Lua at the tag v5.4.7 and the mirror of LPeg (a public Git read; the site lua.org is
#      not reachable from the build machine of the session, the mirrors are);
#   2. applies Pallene's patch `expose-internal-apis.patch` to a copy of Lua 5.4.7 (the Makefile parts of the patch do not
#      apply to the mirror: they are not needed, the script makes luacore.h itself);
#   3. builds `plua` (that Lua), LPeg and installs the layout that `pallenec` expects.
set -e
W=$(realpath -m "${1:?usage: setup.sh WORKDIR}")
mkdir -p "$W"
cd "$W"
export GIT_LFS_SKIP_SMUDGE=1
[ -d pallene ] || git clone -q --depth 1 https://github.com/pallene-lang/pallene pallene
[ -d lua-git ] || git clone -q --depth 1 --branch v5.4.7 https://github.com/lua/lua lua-git 2>/dev/null
[ -d lpeg ]    || git clone -q --depth 1 https://github.com/roberto-ieru/LPeg lpeg

rm -rf lua-5.4.7 prefix
mkdir -p lua-5.4.7/src
cp lua-git/*.c lua-git/*.h lua-5.4.7/src/
touch lua-5.4.7/Makefile lua-5.4.7/src/Makefile
cp pallene/deps/lua/patches/gen-lua-core-h.awk lua-5.4.7/src/
(cd lua-5.4.7 && patch -p1 --no-backup-if-mismatch < ../pallene/deps/lua/patches/expose-internal-apis.patch >/dev/null || true)
cd lua-5.4.7/src
CORE="llimits.h lctype.h lopcodes.h lmem.h lobject.h lzio.h ltable.h lfunc.h ltm.h lparser.h llex.h lundump.h lstate.h lcode.h ldebug.h lapi.h ldo.h lgc.h lvm.h lstring.h"
awk -f gen-lua-core-h.awk $CORE > luacore.h
for f in l*.c; do
    case $f in lua.c|luac.c) continue ;; esac
    gcc -O2 -DLUA_USE_LINUX -c "$f" -o "${f%.c}.o"
done
gcc -O2 -DLUA_USE_LINUX -c lua.c -o lua.o
gcc -o ../plua lua.o $(ls l*.o | grep -v '^lua.o$') -lm -ldl -Wl,-E
cd "$W"
(cd lpeg && make -s linux LUADIR="$W/lua-5.4.7/src" >/dev/null)

P="$W/prefix"
mkdir -p "$P/bin" "$P/include" "$P/share/lua/5.4" "$P/lib/lua/5.4"
cp lua-5.4.7/plua "$P/bin/"
cp lua-5.4.7/src/lua.h lua-5.4.7/src/luaconf.h lua-5.4.7/src/lualib.h lua-5.4.7/src/lauxlib.h lua-5.4.7/src/luacore.h "$P/include/"
cp pallene/deps/pallene-tracer/ptracer.h "$P/include/"
cp pallene/deps/argparse/src/argparse.lua "$P/share/lua/5.4/"
cp lpeg/re.lua "$P/share/lua/5.4/"
cp lpeg/lpeg.so "$P/lib/lua/5.4/"
cp -r pallene/src/pallene "$P/share/lua/5.4/pallene"
cp -r pallene/src/bin "$P/share/lua/5.4/bin"
cp pallene/pallenec "$P/bin/"
"$P/bin/plua" -v
echo "pallene: ready, $P/bin/pallenec"
