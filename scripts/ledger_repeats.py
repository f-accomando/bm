#!/usr/bin/env python3
"""List sub-agent ledger tasks repeated N or more times.

Usage: python scripts/ledger_repeats.py [--file docs/agents/ledger.txt] [--min 3] [--since YYYY-MM-DD]

Ledger row: date|agent|model|task|area|gate|necessity|outcome|tokens|tools|secs
(first line is a '#' comment). Malformed rows (not 11 fields) are skipped with a
warning on stderr. Exit 0 if the file is read (even with no groups), 2 on a
missing file or bad usage.

Normalisation of the `task` field into a "form" (deterministic):
  1. WO/branch identifiers in upper case with a hyphen (AGY-1, SDLOG-FIX, D-MON,
     SDLOG-1..5, R-NET4) are marked as ID;
  2. the rest is lower-cased; every run of digits becomes N;
  3. parts in parentheses () are removed; runs of spaces collapse to one;
  4. the form is the FIRST WORD of the result (split on whitespace and ','),
     followed by " ID" if the task contains at least one identifier.
So `review AGY-1 API-SRC`, `review AGY-2 API-LINT` and `review SDLOG-FIX (AGY)`
all give `review ID`.
"""
import argparse
import re
import sys

ID_RE = re.compile(r"\b[A-Z][A-Z0-9]*(?:-[A-Z0-9]+)+")
MARK = "\x00"
RECOMMEND = ('repeated >= 3 times: make it a skill or a script and note it in '
             'the ledger notes')


def form(task):
    s = ID_RE.sub(MARK, task)
    s = re.sub(r"\([^)]*\)", " ", s).lower()
    s = re.sub(r"\d+", "N", s)
    has_id = MARK in s
    words = [w for w in re.split(r"[\s,]+", s.replace(MARK, " ")) if w]
    first = words[0] if words else ("ID" if has_id else "")
    if not words:
        return first
    return first + (" ID" if has_id else "")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--file", default="docs/agents/ledger.txt")
    ap.add_argument("--min", type=int, default=3)
    ap.add_argument("--since", default=None)
    try:
        a = ap.parse_args(argv)
    except SystemExit as e:
        return 2 if e.code else 0
    if a.min < 2:
        print("error: --min must be >= 2", file=sys.stderr)
        return 2
    if a.since and not re.fullmatch(r"\d{4}-\d{2}-\d{2}", a.since):
        print("error: --since must be YYYY-MM-DD", file=sys.stderr)
        return 2
    try:
        with open(a.file, encoding="utf-8", newline="") as fh:
            lines = fh.read().splitlines()
    except OSError as e:
        print("error: cannot read %s: %s" % (a.file, e), file=sys.stderr)
        return 2
    groups, bad = {}, 0
    for n, line in enumerate(lines, 1):
        if not line.strip() or line.startswith("#"):
            continue
        f = line.split("|")
        if len(f) != 11:
            bad += 1
            print("line %d: expected 11 fields, got %d" % (n, len(f)), file=sys.stderr)
            continue
        if a.since and f[0] < a.since:
            continue
        try:
            tok = int(f[8])
        except ValueError:
            tok = 0
        g = groups.setdefault(form(f[3]), {"n": 0, "agents": set(), "tok": 0, "ex": []})
        g["n"] += 1
        g["agents"].add(f[1])
        g["tok"] += tok
        if f[3] not in g["ex"]:
            g["ex"].append(f[3])
    rep = sorted(((k, g) for k, g in groups.items() if g["n"] >= a.min),
                 key=lambda kv: (-kv[1]["n"], kv[0]))
    if not rep:
        print("no repeated tasks")
    for k, g in rep:
        print("%d x %s (agenti: %s; token totali %d; esempi: %s)" % (
            g["n"], k, ",".join(sorted(g["agents"])), g["tok"], "; ".join(g["ex"][:3])))
    if rep:
        print(RECOMMEND)
    if bad:
        print("skipped %d malformed line(s)" % bad, file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
