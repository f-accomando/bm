#!/usr/bin/env python3
"""Read-only inventory of the input each cartridge in carts/ reads, against the
games' input standard (docs/design/games-input-standard.md).

  python scripts/input_inventory.py [--games]

One markdown table: cart | kind | confirm | back | pause | act1 | act2 | shoulder |
declared exceptions. A cell is `std` (a logical name, or lib.input() without an
override), `exc:<button>` (an override of lib.input), `raw:<i or letter>` (a
button number or letter read directly) or `-`. --games keeps the games and
leaves out the tools. Exit 0 always; nothing is edited.
"""
import argparse
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
TOOLS = {"animator", "assistant", "code", "editor", "mesh", "pixel", "sdk_b16", "sound", "studio", "write"}

CALL = re.compile(r'\bbtn[pr]?\(\s*("([a-z0-9_]+)"|(\d+))')
INPUT = re.compile(r'\blib\.input\(\s*(\{([^}]*)\})?\s*\)')
KEYS = re.compile(r'([A-Za-z_][A-Za-z_0-9]*)\s*=\s*"([a-z0-9]+)"')
NUM_TO_ACTION = {4: "act1", 5: "act2", 8: "pause"}
LETTER_TO_ACTION = {"a": "act1", "b": "act2"}
STD_NAMES = {"ok": "confirm", "confirm": "confirm", "back": "back", "cancel": "back", "start": "pause",
             "pause": "pause", "act1": "act1", "act2": "act2", "l1": "shoulder", "r1": "shoulder",
             "shoulder_l": "shoulder", "shoulder_r": "shoulder"}
COLS = ["confirm", "back", "pause", "act1", "act2", "shoulder"]


def lua_files(d):
    for base, _, names in os.walk(d):
        for n in sorted(names):
            if n.endswith(".lua"):
                yield os.path.join(base, n)


def scan(d):
    cells = {c: "-" for c in COLS}
    exceptions = {}
    used_input = False
    for path in lua_files(d):
        text = open(path, encoding="utf-8", errors="replace").read()
        text = re.sub(r"--[^\n]*", "", text)                  # comments
        for m in INPUT.finditer(text):
            used_input = True
            if m.group(2):
                exceptions.update(KEYS.findall(m.group(2)))
        for m in CALL.finditer(text):
            name, num = m.group(2), m.group(3)
            if name in STD_NAMES:
                cells[STD_NAMES[name]] = "std"
            elif name in LETTER_TO_ACTION:
                a = LETTER_TO_ACTION[name]
                if cells[a] != "std":
                    cells[a] = "raw:" + name
            elif num is not None and int(num) in NUM_TO_ACTION:
                a = NUM_TO_ACTION[int(num)]
                if cells[a] != "std":
                    cells[a] = "raw:" + num
    if used_input:
        for c in cells:
            if c != "shoulder" and cells[c].startswith("raw:"):
                continue                                      # a raw read stays visible: it is a deviation
            if c in ("confirm", "back", "pause", "act1", "act2"):
                cells[c] = "std"
    for action, button in exceptions.items():
        if action in cells:
            cells[action] = "exc:" + button
    return cells, sorted(exceptions)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--games", action="store_true", help="only the games, not the tools")
    args = ap.parse_args()
    carts = os.path.join(ROOT, "carts")
    print("| cart | kind | " + " | ".join(COLS) + " | declared exceptions |")
    print("|---|---|" + "---|" * len(COLS) + "---|")
    for name in sorted(os.listdir(carts)):
        d = os.path.join(carts, name)
        if not os.path.isdir(d):
            continue
        kind = "tool" if name in TOOLS else "game"
        if args.games and kind != "game":
            continue
        cells, exc = scan(d)
        print(f"| {name} | {kind} | " + " | ".join(cells[c] for c in COLS) + f" | {', '.join(exc) or '-'} |")
    return 0


if __name__ == "__main__":
    sys.exit(main())
