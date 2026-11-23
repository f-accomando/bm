#!/usr/bin/env python3
"""Make core/src/kernel/bm3d_tech.c from docs/reference/bm3d-techniques.txt.

  python core/scripts/bm3d_tech.py          write the C file
  python core/scripts/bm3d_tech.py --check  exit 1 if the C file is old
"""
import os
import sys
import textwrap

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "docs", "reference", "bm3d-techniques.txt")
OUT = os.path.join(ROOT, "core", "src", "kernel", "bm3d_tech.c")
WIDTH = 76
TARGETS = ("ARM", "V3D", "ARM+V3D")
COLOUR = {"ARM": "93", "V3D": "96", "ARM+V3D": "92"}


def parse():
    groups = []
    for n, raw in enumerate(open(SRC, encoding="utf-8"), 1):
        line = raw.rstrip("\n")
        if not line.strip() or line.startswith("#"):
            continue
        if line.startswith("["):
            groups.append((line.strip("[] "), []))
            continue
        f = [x.strip() for x in line.split("|")]
        if len(f) != 6 or f[1] not in TARGETS or not groups:
            sys.exit("%s:%d: bad line: %s" % (SRC, n, line))
        groups[-1][1].append(f)
    return groups


def text(groups):
    out = ["\033[97mbm3D: the techniques of the V3D driver (ARM = r3d on the CPU)\033[0m",
           "\033[90mversion  target  name; the key and what it does\033[0m", ""]
    for title, rows in groups:
        out.append("\033[93m== %s ==\033[0m" % title)
        for ver, tgt, date, name, key, what in rows:
            out.append("\033[%sm%-5s %-7s\033[0m %s" % (COLOUR[tgt], ver, tgt, name))
            body = what + (" [%s]" % key if key != "-" else "") + " (%s)" % date
            out.extend(textwrap.wrap(body, WIDTH, initial_indent="      ", subsequent_indent="      "))
        out.append("")
    return out


def c_file(lines):
    s = ['/* Made by core/scripts/bm3d_tech.py from docs/reference/bm3d-techniques.txt. Do not edit. */',
         '#include "bm3d_tech.h"', '', 'const char bm3d_tech_text[] =']
    for l in lines:
        s.append('    "%s\\n"' % l.replace("\\", "\\\\").replace('"', '\\"').replace("\033", "\\033"))
    s[-1] += ";"
    return "\n".join(s) + "\n"


def main():
    data = c_file(text(parse()))
    if "--check" in sys.argv:
        cur = open(OUT, encoding="utf-8", newline="").read() if os.path.exists(OUT) else ""
        if cur.replace("\r\n", "\n") != data:
            print("bm3d_tech.c is old: run core/scripts/bm3d_tech.py")
            sys.exit(1)
        return
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(data)


main()
