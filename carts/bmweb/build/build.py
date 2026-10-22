#!/usr/bin/env python3
"""bm Web is written in several Lua modules (carts/bmweb/src/*.lua) and an entry file
(carts/bmweb/main.lua). A cartridge has one script, so this joins them: each module becomes a
function in a table, and a local `require` gives the module its text (any other name goes to the
kernel's own require). The host tests load the same modules from src/ with package.path.

  carts/bmweb/build/build.py OUT.lua

`dump.lua` (a test aid) stays out.
"""
import glob
import os
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP = {"dump"}

HEAD = """-- bm Web: joined by carts/bmweb/build/build.py. Edit carts/bmweb/src and carts/bmweb/main.lua.
local __src, __done, __kernel_require = {}, {}, require
local function require(name)
  local m = __done[name]
  if m ~= nil then return m end
  local f = __src[name]
  if not f then return __kernel_require(name) end
  m = f(name)
  if m == nil then m = true end
  __done[name] = m
  return m
end
"""


def build():
    out = [HEAD]
    for path in sorted(glob.glob(os.path.join(HERE, "src", "*.lua"))):
        name = os.path.basename(path)[:-4]
        if name in SKIP:
            continue
        text = open(path, encoding="utf-8").read()
        if not text.endswith("\n"):
            text += "\n"
        out.append('__src["' + name + '"] = function(...) -- ' + name + ".lua\n" + text + "end\n")
    main = open(os.path.join(HERE, "main.lua"), encoding="utf-8").read()
    out.append("-- main.lua\n" + main)
    return "".join(out)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    os.makedirs(os.path.dirname(os.path.abspath(sys.argv[1])), exist_ok=True)
    with open(sys.argv[1], "w", encoding="utf-8", newline="\n") as f:
        f.write(build())


if __name__ == "__main__":
    main()
