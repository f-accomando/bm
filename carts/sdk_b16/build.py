#!/usr/bin/env python3
"""
bm SDK (RGB30), the .b16 edition of the bm SDK, is two Lua files: its
layout module (layout.lua, which returns a table) and the SDK (main.lua).
A cartridge has one script and its require() has only the kernel's
libraries, so they are joined: the module becomes the local `layout` at
the top, main.lua follows unchanged.

  carts/sdk_b16/build.py OUT.lua

main.lua's line N is line N + OFFSET of OUT.lua (OFFSET is written in the
first line), to find the source line of an error message.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def read(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        text = f.read()
    return text if text.endswith("\n") else text + "\n"


def build():
    layout, main = read("layout.lua"), read("main.lua")
    head = "local layout = (function()   -- carts/sdk_b16/layout.lua\n"
    tail = "end)()\n"
    offset = 2 + head.count("\n") + layout.count("\n") + tail.count("\n")
    first = (f"-- bm SDK (RGB30), built by carts/sdk_b16/build.py: layout.lua, then main.lua "
             f"(its line N is line N + {offset})\n")
    return first + "\n" + head + layout + tail + main


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: build.py OUT.lua")
    out = sys.argv[1]
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    text = build()
    with open(out, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"{out}: {len(text)} bytes")


if __name__ == "__main__":
    main()
