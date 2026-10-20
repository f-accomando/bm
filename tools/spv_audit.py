#!/usr/bin/env python3
"""Read-only audit of SPV's cost to the sync, from files and git only (stdlib).

  python tools/spv_audit.py [--repo DIR] [--since YYYY-MM-DD]

Sources: SYNC.md Log rows, sync/OP-*.ledger.jsonl and sync/<ID>.ledger.jsonl (--close rows: fields wo, min, iso),
sync/*.md (DELIVERED lines, `preso WO <X>` / `concluso WO <X>` lines) and `git log` of sync/wo/.
Numbers (raw, no weighting):
  idle_min        sum of gaps between a concluded WO (ledger iso / DELIVERED / `concluso WO`) and the
                  next `preso WO` row of the same operator (approximation)
  changes_req     REVIEW: CHANGES REQUESTED rows; changes_with_lines = those whose text cites
                  riga/righe/perimetro/collide
  est_vs_actual   per closed WO: estimate ("Stima: N minuti" in sync/wo/<WO>.md) vs ledger `min`
  pickup_delay    mean minutes from the first commit of sync/wo/<ID>.md to the `preso WO <ID>` row
  rollbacks       SPV Log rows with action NOTE/RULE containing "ripristin" or "errore"
Missing data prints n/a. Exit code 0 always.
"""
import argparse
import datetime
import glob
import json
import os
import re
import subprocess
import sys

KW = ("riga", "righe", "perimetro", "collide")


def parse_iso(s):
    try:
        return datetime.datetime.fromisoformat(s.strip())
    except (ValueError, AttributeError):
        return None


def read(path):
    try:
        with open(path, encoding="utf-8") as f:
            return f.read()
    except OSError:
        return ""


def log_rows(text):
    rows = []
    for ln in text.splitlines():
        c = [x.strip() for x in ln.split("|")]
        if len(c) >= 7 and re.match(r"\d{4}-\d\d-\d\dT", c[1]):
            rows.append({"t": parse_iso(c[1]), "who": c[2], "action": c[4].strip("`"), "text": "|".join(c[5:-1])})
    return rows


def git_first_commits(repo):
    """{WO id: datetime of the first commit touching sync/wo/<id>.md}."""
    r = subprocess.run(["git", "log", "--diff-filter=A", "--name-only", "--format=@@%aI", "--", "sync/wo"],
                       cwd=repo, capture_output=True, text=True)
    out, cur = {}, None
    for ln in r.stdout.splitlines():
        if ln.startswith("@@"):
            cur = parse_iso(ln[2:])
        else:
            m = re.match(r"sync/wo/(.+)\.md$", ln.strip())
            if m and cur:
                out[m.group(1)] = cur
    return out


def mean(v):
    return sum(v) / len(v) if v else None


def fmt(x, nd=1):
    return "n/a" if x is None else ("%.*f" % (nd, x) if isinstance(x, float) else str(x))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--repo", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    ap.add_argument("--since", help="YYYY-MM-DD: ignore events before this date")
    a = ap.parse_args(argv)
    repo = os.path.abspath(a.repo)
    since = None
    if a.since:
        try:
            since = datetime.datetime.fromisoformat(a.since).astimezone()
        except ValueError:
            sys.stderr.write("error: --since must be YYYY-MM-DD\n")
            return 2

    def ok(t):
        return t is not None and (since is None or t >= since)

    rows = [r for r in log_rows(read(os.path.join(repo, "SYNC.md"))) if ok(r["t"])]

    # per-operator events: concluded and taken
    done, taken = {}, {}
    wo_taken = {}
    ledger = []
    paths = set()
    for pat in ("OP-*.ledger.jsonl", "*.ledger.jsonl"):     # sync/<ID>.ledger.jsonl is the new path
        paths.update(glob.glob(os.path.join(repo, "sync", pat)))
    for p in sorted(paths):
        for ln in read(p).splitlines():
            try:
                d = json.loads(ln)
            except ValueError:
                continue
            t = parse_iso(d.get("iso", ""))
            if ok(t):
                ledger.append(d)
                who = d.get("operator", "")[3:] if d.get("operator") else                     os.path.basename(p).split(".")[0]           # sync/<ID>.ledger.jsonl rows carry no operator
                done.setdefault(who, []).append(t)
    for p in sorted(glob.glob(os.path.join(repo, "sync", "*.md"))):
        op = os.path.basename(p)[:-3]
        txt = read(p)
        for m in re.finditer(r"DELIVERED\s+\S+.*?(\d{4}-\d\d-\d\dT\d\d:\d\d[+-]\d\d:\d\d)\s*$", txt, re.M):
            t = parse_iso(m.group(1))
            if ok(t) and t not in done.get(op, []):
                done.setdefault(op, []).append(t)
        for m in re.finditer(r"preso WO (\S+?)\b.*?(\d{4}-\d\d-\d\dT\d\d:\d\d[+-]\d\d:\d\d)", txt):
            t = parse_iso(m.group(2))
            if ok(t):
                taken.setdefault(op, []).append(t)
                wo_taken[m.group(1)] = t
    for r in rows:
        m = re.search(r"preso WO (\S+)", r["text"])
        if m and r["who"] != "SPV":
            taken.setdefault(r["who"], []).append(r["t"])
            wo_taken[m.group(1)] = r["t"]

    # (a)
    idle, n_gaps = 0.0, 0
    for op, ds in done.items():
        for d in sorted(ds):
            nxt = [t for t in taken.get(op, []) if t > d]
            if nxt:
                idle += (min(nxt) - d).total_seconds() / 60
                n_gaps += 1
    idle_v = round(idle, 1) if n_gaps else None

    # (b)
    ch = [r for r in rows if r["action"] == "REVIEW: CHANGES REQUESTED"]
    ch_lines = [r for r in ch if any(k in r["text"].lower() for k in KW)]

    # (c)
    est = []
    for d in ledger:
        wo = d.get("wo")
        m = re.search(r"^\s*Stima:?\s*(\d+)", read(os.path.join(repo, "sync", "wo", "%s.md" % wo)), re.M | re.I)
        est.append((wo, int(m.group(1)) if m else None, d.get("min")))
    pairs = [(e, m) for _, e, m in est if e is not None and m is not None]

    # (d)
    first = git_first_commits(repo)
    delays = [(wo_taken[w] - first[w]).total_seconds() / 60 for w in wo_taken if w in first]

    # (e)
    rb = [r for r in rows if r["who"] == "SPV" and r["action"] in ("NOTE", "RULE")
          and re.search(r"ripristin|errore", r["text"], re.I)]

    print("SPV audit (%s%s)" % (repo, ", since " + a.since if a.since else ""))
    print("%-34s %s" % ("a idle minutes (done->taken gaps)", "%s (%d gaps)" % (fmt(idle_v), n_gaps)))
    print("%-34s %d, of which with line/perimeter hint: %d" % ("b CHANGES REQUESTED", len(ch), len(ch_lines)))
    print("%-34s %s" % ("c closed WO est vs actual min", "n/a" if not est else
          ", ".join("%s %s/%s" % (w, fmt(e), fmt(m)) for w, e, m in est)))
    print("%-34s %s (%d WO)" % ("d mean pickup delay min", fmt(mean(delays)), len(delays)))
    print("%-34s %d" % ("e SPV NOTE/RULE ripristin|errore", len(rb)))
    print(json.dumps({"score": {
        "idle_min": idle_v, "changes_requested": len(ch), "changes_with_lines": len(ch_lines),
        "est_vs_actual_sum": [sum(p[0] for p in pairs), sum(p[1] for p in pairs)] if pairs else None,
        "pickup_delay_mean_min": None if not delays else round(mean(delays), 1), "rollback_rows": len(rb)}}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
