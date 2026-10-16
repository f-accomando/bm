#!/usr/bin/env python3
"""Calibration stats for the sub-agent ledger (read-only).

  agent_audit.py [--ledger PATH] [--agent NAME]

Reads docs/agents/ledger.txt (one line per subagent run):
  date|agent|model|task|area|gate|necessity|outcome|tokens|tools|secs
Prints per-agent outcome shares and median tokens/secs, %DONE by gate and by
necessity, and a warning when runs with gate<=2 fail too often.
Invalid lines go to stderr as "line N: reason" (exit 1; the report still covers
the valid rows). Nothing here writes files."""
import argparse, os, re, statistics, sys

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf8')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_LEDGER = os.path.join(ROOT, "docs", "agents", "ledger.txt")
FIELDS = ["date", "agent", "model", "task", "area", "gate", "necessity", "outcome", "tokens", "tools", "secs"]
AGENTS = {"bm-planner", "bm-coder", "bm-mechanic", "bm-light", "bm-deep", "bm-research", "bm-ops", "general-purpose"}
OUTCOMES = {"DONE", "BLOCKED", "FAILED x2", "ESCALATED"}
WARN_MIN_RUNS, WARN_MAX_GATE = 5, 2


def _check(row):
    """Return the reason the row is invalid, or None if it is valid."""
    if not re.match(r"[0-9]{4}-[0-9]{2}-[0-9]{2}", row["date"]):
        return "date must start with YYYY-MM-DD"
    if row["agent"] not in AGENTS:
        return f"unknown agent '{row['agent']}'"
    for key in ("gate", "necessity"):
        if row[key] != "-" and not re.fullmatch(r"[1-5]", row[key]):
            return f"{key} must be 1-5 or -"
    if row["outcome"] not in OUTCOMES:
        return f"unknown outcome '{row['outcome']}'"
    for key in ("tokens", "tools", "secs"):
        if not re.fullmatch(r"[0-9]+", row[key]):
            return f"{key} must be an integer >= 0"
    for key in ("model", "task", "area"):
        if not row[key]:
            return f"{key} is empty"
    return None


def _typed(row):
    def num(value):
        return None if value == "-" else int(value)
    return {
        "agent": row["agent"],
        "gate": num(row["gate"]),
        "necessity": num(row["necessity"]),
        "outcome": row["outcome"],
        "tokens": int(row["tokens"]),
        "secs": int(row["secs"]),
    }


def parse(path):
    """Return (rows, errors): rows are typed dicts, errors are (line_no, reason) tuples."""
    rows, errors = [], []
    with open(path, encoding="utf-8-sig") as fh:
        lines = fh.read().split("\n")
    for lineno, raw in enumerate(lines, 1):
        line = raw.rstrip("\r")
        if not line.strip() or line.startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) != len(FIELDS):
            errors.append((lineno, f"expected {len(FIELDS)} fields, got {len(parts)}"))
            continue
        row = dict(zip(FIELDS, parts))
        reason = _check(row)
        if reason:
            errors.append((lineno, reason))
        else:
            rows.append(_typed(row))
    return rows, errors


def _pct(part, whole):
    return 100.0 * part / whole if whole else 0.0


def _outcome_pct(rows, outcome):
    return _pct(sum(r["outcome"] == outcome for r in rows), len(rows))


def _median_text(values):
    med = statistics.median(values)
    return str(int(med)) if med == int(med) else f"{med:.1f}"


def _share_table(title, rows, key):
    out = [f"{title:<12}{'runs':>6}{'%DONE':>8}"]
    for value in sorted({r[key] for r in rows if r[key] is not None}):
        subset = [r for r in rows if r[key] == value]
        out.append(f"{value:<12}{len(subset):>6}{_outcome_pct(subset, 'DONE'):>8.1f}")
    return out


def report(rows, agent=None):
    if agent is not None:
        rows = [r for r in rows if r["agent"] == agent]
    if not rows:
        return "no rows"
    out = [f"{'agent':<16}{'runs':>6}{'%DONE':>8}{'%BLOCKED':>10}{'%FAILED':>9}{'%ESCALATED':>12}{'tok_med':>10}{'secs_med':>10}"]
    for name in sorted({r["agent"] for r in rows}):
        sub = [r for r in rows if r["agent"] == name]
        out.append(
            f"{name:<16}{len(sub):>6}"
            f"{_outcome_pct(sub, 'DONE'):>8.1f}"
            f"{_outcome_pct(sub, 'BLOCKED'):>10.1f}"
            f"{_outcome_pct(sub, 'FAILED x2'):>9.1f}"
            f"{_outcome_pct(sub, 'ESCALATED'):>12.1f}"
            f"{_median_text([r['tokens'] for r in sub]):>10}"
            f"{_median_text([r['secs'] for r in sub]):>10}"
        )
    out.append("")
    out += _share_table("gate", rows, "gate")
    out.append("")
    out += _share_table("necessity", rows, "necessity")
    low = [r for r in rows if r["gate"] is not None and r["gate"] <= WARN_MAX_GATE]
    failed = sum(r["outcome"] != "DONE" for r in low)
    if len(low) >= WARN_MIN_RUNS and failed * 5 > len(low):  # failed/len > 20%, exact integer test
        out.append("")
        out.append(f"gate troppo ottimista: non-DONE {failed}/{len(low)} "
                   f"({_pct(failed, len(low)):.1f}%) with gate<=2, threshold 20%")
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Calibration stats for docs/agents/ledger.txt (read-only).")
    ap.add_argument("--ledger", default=DEFAULT_LEDGER, help="ledger file (default: docs/agents/ledger.txt)")
    ap.add_argument("--agent", help="report only this agent")
    args = ap.parse_args(argv)
    try:
        rows, errors = parse(args.ledger)
    except OSError as exc:
        print(f"cannot read ledger: {exc}", file=sys.stderr)
        return 2
    for lineno, reason in errors:
        print(f"line {lineno}: {reason}", file=sys.stderr)
    print(report(rows, args.agent))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
