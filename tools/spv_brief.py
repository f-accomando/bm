#!/usr/bin/env python3
"""Short SPV view of the sync: at most 25 lines.

  python3 tools/spv_brief.py [--root DIR]

For every active operator of sync/operators.toml it prints: state, age of `updated`,
the number of unread Log rows addressed to it, and the DELIVERED lines of its
`## Consegne` that are new since the last REVIEW of that operator (WO-ID and commit only, no text).
A DELIVERED line is covered by a REVIEW row of SYNC.md when its WO-ID or its commit (first 8
characters) appears in that row. A covered line is never new. An uncovered line is new unless its
timestamp is not after the latest REVIEW covering that operator. Then the last 3 REVIEW rows of the
Log, cut at 100 characters. Parsing is shared with tools/sync_check.py (toml_ops, header, iso, rows,
consegne, delivered_parts, addressed_rows). Read-only: it writes nothing."""
import argparse
import datetime
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sync_check as sc  # noqa: E402

MAX_LINES = 25
MAX_DELIV = 3      # DELIVERED items shown per operator
REVIEW_N = 3       # REVIEW rows shown
CUT = 100          # characters per REVIEW row
TS_END = re.compile(r"(\d{4}-\d\d-\d\dT\d\d:\d\d(?:[+-]\d\d:\d\d)?)\s*$")


def is_review(line):
    cells = line.split("|")
    return len(cells) > 5 and cells[4].strip().strip("`") == "REVIEW"


def covering(wo, sha, reviews):
    """times of the REVIEW rows that mention this WO-ID or this commit"""
    out = []
    for t, text in reviews:
        if (sha[:8] and sha[:8] in text) or re.search(r"(?<![\w-])%s(?![\w-])" % re.escape(wo), text):
            out.append(t)
    return out


def pending(text, reviews):
    """[(WO-ID, commit)] of the DELIVERED lines new since the last REVIEW of this operator"""
    items = []
    for ln in sc.consegne(text):
        parts = sc.delivered_parts(ln)
        if parts:
            m = TS_END.search(ln)
            items.append((parts, sc.iso(m.group(1)) if m else None))
    last = None
    for (wo, sha), _ in items:
        for t in covering(wo, sha, reviews):
            if last is None or t > last:
                last = t
    out = []
    for (wo, sha), t in items:
        if covering(wo, sha, reviews):
            continue
        if last is not None and t is not None and t <= last:
            continue
        out.append((wo, sha))
    return out


def operator_lines(oid, log, reviews, now):
    path = os.path.join(sc.ROOT, "sync", oid + ".md")
    if not os.path.exists(path):
        return ["%-5s no sync/%s.md" % (oid, oid)]
    text = open(path, encoding="utf-8").read()
    h = sc.header(text)
    if not h:
        return ["%-5s no status block" % oid]
    upd = sc.iso(h.get("updated", ""))
    age = "%dm" % int((now - upd).total_seconds() // 60) if upd else "?"
    unread = len(sc.addressed_rows(oid, h, log))
    out = ["%-5s %-8s age %-6s unread %d" % (oid, h.get("state", "?"), age, unread)]
    items = pending(text, reviews)
    if items:
        shown = ", ".join("%s %s" % (wo, sha[:8]) for wo, sha in items[:MAX_DELIV])
        more = len(items) - MAX_DELIV
        out.append("    new DELIVERED: %s%s" % (shown, " (+%d)" % more if more > 0 else ""))
    return out


def main(argv=None):
    if hasattr(sys.stdout, "reconfigure"):
        try:
            sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--root", help="repository root (default: the one holding this tool)")
    a = p.parse_args(argv)
    if a.root:
        sc.ROOT = os.path.abspath(a.root)
    now = datetime.datetime.now().astimezone()
    log = sc.rows(os.path.join(sc.ROOT, "SYNC.md"))
    reviews = [(t, line) for t, line in log if is_review(line)]

    head = ["spv brief %s, Log rows %d, REVIEW rows %d" % (now.strftime("%H:%M"), len(log), len(reviews))]
    ops = []
    for oid, active in sc.toml_ops():
        if active:
            ops += operator_lines(oid, log, reviews, now)
    rev = ["last %d REVIEW:" % min(REVIEW_N, len(reviews))]
    for _, line in reviews[-REVIEW_N:]:
        rev.append(line[:CUT - 1] + "\u2026" if len(line) > CUT else line)

    room = MAX_LINES - len(head) - len(rev)
    if len(ops) > room:
        ops = ops[:max(room - 1, 0)] + ["... more operators: python tools/sync_check.py"]
    for ln in head + ops + rev:
        print(ln)
    return 0


if __name__ == "__main__":
    sys.exit(main())
