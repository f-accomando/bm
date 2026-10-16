#!/usr/bin/env python3
"""Check the `file:line` citations of a work order against a repo checkout.

  python tools/wo_lines_check.py WO.md [--repo DIR]

For every backtick span like `src/kernel/monitor.c:206` or `src/a.c:10-20` the file must
exist under --repo (default: current directory) and the line number(s) must be <= the
file length. Content is not compared. Exit 0 all good, 1 failures listed, 2 bad usage.
"""
import argparse
import os
import re
import sys

CITE = re.compile(r"`([^`\s:]+\.[A-Za-z0-9_]+):(\d+)(?:-(\d+))?`")


def check(wo_text, repo):
    fails, n, cache = [], 0, {}
    for m in CITE.finditer(wo_text):
        n += 1
        path, lo = m.group(1), int(m.group(2))
        hi = int(m.group(3)) if m.group(3) else lo
        full = os.path.join(repo, path)
        if full not in cache:
            try:
                with open(full, "rb") as f:
                    cache[full] = f.read().count(b"\n") + (0 if f.tell() == 0 else 1) if False else None
                with open(full, "rb") as f:
                    data = f.read()
                cache[full] = len(data.splitlines())
            except OSError:
                cache[full] = None
        total = cache[full]
        if total is None:
            fails.append("%s: file not found" % m.group(0))
        elif max(lo, hi) > total or min(lo, hi) < 1:
            fails.append("%s: line out of range (file has %d lines)" % (m.group(0), total))
    return n, fails


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("wo")
    ap.add_argument("--repo", default=".")
    a = ap.parse_args(argv)
    try:
        with open(a.wo, encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        sys.stderr.write("error: %s\n" % e)
        return 2
    n, fails = check(text, a.repo)
    for f in fails:
        print("FAIL " + f)
    print("%d citation(s), %d failure(s)" % (n, len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
