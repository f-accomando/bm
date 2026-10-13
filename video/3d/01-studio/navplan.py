#!/usr/bin/env python3
"""Development helper: from the NAV table of the instrumented run (out/run.log, key `j` in out/probe.txt) the arrows
that take the pointer of the vertex or select tool from one element to the next (BFS on the real transition table)."""
import itertools
import os
import re
import sys
from collections import deque

HERE = os.path.dirname(os.path.abspath(__file__))


def table(path=os.path.join(HERE, "out", "run.log")):
    rows, init = {}, None
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"NAV (\d+) (.*?) \| (\d+) (\d+) (\d+) (\d+)", line)
        if m:
            rows[int(m.group(1))] = (m.group(2), dict(zip(("left", "right", "up", "down"), map(int, m.groups()[2:]))))
        m = re.match(r"NAVINIT (\d+)", line)
        if m:
            init = int(m.group(1))
    return rows, init


def path(rows, a, b):
    seen, q = {a: []}, deque([a])
    while q:
        u = q.popleft()
        if u == b:
            return seen[u]
        for d, v in rows[u][1].items():
            if v not in seen:
                seen[v] = seen[u] + [d]
                q.append(v)
    return None


def find(rows, text):
    return [i for i, (info, _) in rows.items() if info.startswith(text)]


if __name__ == "__main__":
    rows, init = table()
    print("init", init, rows[init][0] if init else None)
    for t in sys.argv[1:]:
        print(t, find(rows, t))
