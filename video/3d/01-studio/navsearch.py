#!/usr/bin/env python3
"""Development helper: finds, with the real editor (the instrumented cartridge in bmhost), the arrows that take the pointer
of the vertex tool (or the select tool) to the wanted corners. The camera eases while the pointer moves, so the
transition table is not enough: every candidate sequence is replayed from the start and the pointer read from the log.

  python3 video/3d/01-studio/navsearch.py vertex "0,3,1" "1,3,1" "1,3,0" "0,3,0"
"""
import heapq
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
OUT = os.path.join(HERE, "out")
CACHE = {}


def run(tag, keys, field):
    key = (tag, tuple(keys))
    if key in CACHE:
        return CACHE[key]
    open(os.path.join(OUT, "probe.txt"), "w").write("\n".join(keys) + "\nwait 1\n")
    env = dict(os.environ, STUDIO_PROBE=tag)
    subprocess.run([sys.executable, os.path.join(HERE, "record.py"), "--log"], cwd=ROOT, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    last = None
    for line in open(os.path.join(OUT, "run.log"), encoding="utf-8", errors="replace"):
        if " BK " in line:
            m = re.search(field + r"=(\S+)", line)
            last = m.group(1) if m else None
    CACHE[key] = last
    return last


def parse(v):
    if not v or v == "-":
        return None
    return tuple(float(x) for x in re.findall(r"-?\d+\.?\d*", v)[:3])


def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def search(tag, field, prefix, target, limit=40):
    """Best-first on the distance to the target; returns the arrows."""
    start = parse(run(tag, prefix, field))
    heap = [(dist(start, target) if start else 99, 0, [])]
    seen = set()
    n = 0
    while heap and n < limit:
        d, _, seq = heapq.heappop(heap)
        pos = parse(run(tag, prefix + seq, field)) if seq else start
        if pos is not None and dist(pos, target) < 0.01:
            return seq
        if pos in seen:
            continue
        seen.add(pos)
        n += 1
        for dr in ("left", "right", "up", "down"):
            s2 = seq + [dr]
            p2 = parse(run(tag, prefix + s2, field))
            if p2 is not None and p2 not in seen:
                heapq.heappush(heap, (dist(p2, target) + 0.2 * len(s2), len(s2), s2))
    return None


if __name__ == "__main__":
    tag, targets = sys.argv[1], [tuple(float(x) for x in a.split(",")) for a in sys.argv[2:]]
    field = "hotp" if tag == "vertex" else "hotf"
    prefix, plan = [], []
    for t in targets:
        seq = search(tag, field, prefix, t)
        print("target", t, "->", seq, flush=True)
        if seq is None:
            break
        prefix = prefix + seq + ["space"]
        plan.append(seq)
    print("PLAN", plan)
