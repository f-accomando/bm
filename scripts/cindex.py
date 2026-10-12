#!/usr/bin/env python3
"""Index of a big source file, so it is read in pieces: python3 scripts/cindex.py FILE [WORD].

For a C file it prints the section banners (`/* ---- name`) with their first line, and
the functions with their line, grouped under the banner they follow. For a Lua file, the
`function` / `local function` / `P.x = function` lines. With WORD, only the lines that
contain it (case-insensitive). Always fresh: nothing is stored, so it never goes stale.
Then `Read FILE` with offset/limit on the range you need."""
import re, signal, sys
if hasattr(signal, "SIGPIPE"):  # not on Windows
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)

def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    path = sys.argv[1]
    word = sys.argv[2].lower() if len(sys.argv) > 2 else None
    lua = path.endswith(".lua")
    banner = re.compile(r"^/\*\s*-{3,}\s*(.*?)\s*(\*/)?$")
    cfun = re.compile(r"^(?:static\s+)?(?:inline\s+)?[A-Za-z_][\w \*]*?\b([A-Za-z_]\w*)\s*\([^;]*$")
    lfun = re.compile(r"^\s*(?:local\s+)?function\s+([\w.:]+)|^\s*([\w.]+)\s*=\s*function")
    out, n = [], 0
    for n, line in enumerate(open(path, encoding="utf8", errors="replace"), 1):
        line = line.rstrip("\n")
        if lua:
            m = lfun.match(line)
            if m:
                out.append((n, "  " + (m.group(1) or m.group(2))))
        else:
            m = banner.match(line)
            if m and m.group(1):
                out.append((n, "# " + m.group(1)))
                continue
            m = cfun.match(line)
            if m and not line.startswith((" ", "\t", "#", "}", "/")) and m.group(1) not in ("if", "while", "for", "switch", "return"):
                out.append((n, "  " + m.group(1)))
    for ln, txt in out:
        if word and word not in txt.lower():
            continue
        print(f"{ln:6d} {txt}")
    print(f"-- {n} lines; Read with offset/limit on the range you need", file=sys.stderr)

main()
