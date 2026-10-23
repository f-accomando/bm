#!/usr/bin/env python3
"""Calibration stats for the sub-agent ledger.

  agent_audit.py [--ledger PATH] [--agent NAME] [--area NAME] [--since YYYY-MM-DD]
                 [--save-baseline PATH] [--baseline PATH]

Reads docs/agents/ledger.txt (one line per subagent run):
  date|agent|model|task|area|gate|necessity|outcome|tokens|tools|secs
tokens, tools and secs are integers, or "-" when nobody measured them. Medians
use the measured values only ("-" when none). Prints per-agent outcome shares and median tokens/secs, %DONE by gate, necessity,
model and area, and a warning when runs with gate<=2 fail too often.
Invalid lines go to stderr as "line N: reason" (exit 1; the report still covers
the valid rows). Writes a file only with --save-baseline."""
import argparse, os, re, statistics, sys

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf8')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_LEDGER = os.path.join(ROOT, "docs", "agents", "ledger.txt")
FIELDS = ["date", "agent", "model", "task", "area", "gate", "necessity", "outcome", "tokens", "tools", "secs"]
AGENTS = {"bm-planner", "bm-coder", "bm-mechanic", "bm-light", "bm-deep", "bm-research", "bm-ops",
          "bm-runner", "general-purpose", "claude", "Explore", "Plan"}
# Old rows (15 Oct) use "generic". agent_audit.py still reads them; ledger_add.py rejects the name.
LEGACY_AGENTS = {"generic"}
UNKNOWN = "-"  # tokens, tools or secs that nobody measured; never write 0 for them
OUTCOMES = {"DONE", "BLOCKED", "FAILED x2", "ESCALATED"}
WARN_MIN_RUNS, WARN_MAX_GATE = 5, 2


def _check(row):
    """Return the reason the row is invalid, or None if it is valid."""
    if not re.match(r"[0-9]{4}-[0-9]{2}-[0-9]{2}", row["date"]):
        return "date must start with YYYY-MM-DD"
    if row["agent"] not in AGENTS | LEGACY_AGENTS:
        return f"unknown agent '{row['agent']}'"
    for key in ("gate", "necessity"):
        if row[key] != "-" and not re.fullmatch(r"[1-5]", row[key]):
            return f"{key} must be 1-5 or -"
    if row["outcome"] not in OUTCOMES:
        return f"unknown outcome '{row['outcome']}'"
    for key in ("tokens", "tools", "secs"):
        if row[key] != UNKNOWN and not re.fullmatch(r"[0-9]+", row[key]):
            return f"{key} must be an integer >= 0 or {UNKNOWN} (not measured)"
    for key in ("model", "task", "area"):
        if not row[key]:
            return f"{key} is empty"
    return None


def _typed(row):
    def num(value):
        return None if value == "-" else int(value)
    return {
        "date": row["date"][:10],
        "agent": row["agent"],
        "model": row["model"],
        "task": row["task"],
        "area": row["area"],
        "gate": num(row["gate"]),
        "necessity": num(row["necessity"]),
        "outcome": row["outcome"],
        "tokens": num(row["tokens"]),
        "tools": num(row["tools"]),
        "secs": num(row["secs"]),
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


def _known(rows, key):
    return [r[key] for r in rows if r[key] is not None]


def _median_text(values):
    if not values:
        return UNKNOWN
    med = statistics.median(values)
    return str(int(med)) if med == int(med) else f"{med:.1f}"


def _share_table(title, rows, key):
    out = [f"{title:<12}{'runs':>6}{'%DONE':>8}"]
    for value in sorted({r[key] for r in rows if r[key] is not None}):
        subset = [r for r in rows if r[key] == value]
        out.append(f"{value:<12}{len(subset):>6}{_outcome_pct(subset, 'DONE'):>8.1f}")
    return out


def _fmt_delta(val, is_pct=False):
    if is_pct:
        return f"{val:+.1f}"
    if isinstance(val, float) and not val.is_integer():
        return f"{val:+.1f}"
    return f"{int(val):+d}"


def save_baseline(rows, ledger_name, out_path):
    last_date = max((r["date"] for r in rows), default="")
    try:
        rel_ledger = os.path.relpath(ledger_name, ROOT).replace("\\", "/")
    except ValueError:
        rel_ledger = ledger_name
    lines = [f"# baseline of {rel_ledger}, rows {len(rows)}, last date {last_date}"]
    for name in sorted({r["agent"] for r in rows}):
        sub = [r for r in rows if r["agent"] == name]
        pct = f"{_outcome_pct(sub, 'DONE'):.1f}"
        tok_med = _median_text(_known(sub, "tokens"))
        secs_med = _median_text(_known(sub, "secs"))
        lines.append(f"{name}|{len(sub)}|{pct}|{tok_med}|{secs_med}")
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")


def load_baseline(path):
    base_data = {}
    with open(path, encoding="utf-8") as fh:
        lines = fh.read().splitlines()
    for lno, line in enumerate(lines, 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("|")
        if len(parts) != 5:
            return None, f"baseline line {lno}: expected 5 fields, got {len(parts)}"
        agent, runs_s, pct_s, tok_s, secs_s = parts
        try:
            runs = int(runs_s)
            pct = float(pct_s)
            tok = None if tok_s == UNKNOWN else float(tok_s)
            secs = None if secs_s == UNKNOWN else float(secs_s)
        except ValueError:
            return None, f"baseline line {lno}: invalid numerical value"
        base_data[agent] = {
            "runs": runs,
            "pct": pct,
            "tok": tok,
            "secs": secs,
        }
    return base_data, None


def report(rows, agent=None, area=None, since=None, baseline=None):
    if agent is not None:
        rows = [r for r in rows if r["agent"] == agent]
    if area is not None:
        rows = [r for r in rows if r["area"] == area]
    if since is not None:
        rows = [r for r in rows if r["date"] >= since]
    if not rows and baseline is None:
        return "no rows"

    out = []
    if rows:
        out.append(f"{'agent':<16}{'runs':>6}{'%DONE':>8}{'%BLOCKED':>10}{'%FAILED':>9}{'%ESCALATED':>12}{'tok_med':>10}{'secs_med':>10}")
        for name in sorted({r["agent"] for r in rows}):
            sub = [r for r in rows if r["agent"] == name]
            out.append(
                f"{name:<16}{len(sub):>6}"
                f"{_outcome_pct(sub, 'DONE'):>8.1f}"
                f"{_outcome_pct(sub, 'BLOCKED'):>10.1f}"
                f"{_outcome_pct(sub, 'FAILED x2'):>9.1f}"
                f"{_outcome_pct(sub, 'ESCALATED'):>12.1f}"
                f"{_median_text(_known(sub, 'tokens')):>10}"
                f"{_median_text(_known(sub, 'secs')):>10}"
            )
        out.append("")
        out += _share_table("gate", rows, "gate")
        out.append("")
        out += _share_table("necessity", rows, "necessity")
        out.append("")
        out += _share_table("model", rows, "model")
        out.append("")
        out += _share_table("area", rows, "area")

        low = [r for r in rows if r["gate"] is not None and r["gate"] <= WARN_MAX_GATE]
        failed = sum(r["outcome"] != "DONE" for r in low)
        if len(low) >= WARN_MIN_RUNS and failed * 5 > len(low):  # failed/len > 20%, exact integer test
            out.append("")
            out.append(f"gate troppo ottimista: non-DONE {failed}/{len(low)} "
                       f"({_pct(failed, len(low)):.1f}%) with gate<=2, threshold 20%")

    if baseline is not None:
        if out:
            out.append("")
        out.append("vs baseline")
        out.append(f"{'agent':<16}{'Δruns':>8}{'Δ%DONE':>10}{'Δtok_med':>12}{'Δsecs_med':>12}")

        cur_stats = {}
        for name in sorted({r["agent"] for r in rows}):
            sub = [r for r in rows if r["agent"] == name]
            tok, secs = _known(sub, "tokens"), _known(sub, "secs")
            cur_stats[name] = {
                "runs": len(sub),
                "pct": _outcome_pct(sub, "DONE"),
                "tok": float(statistics.median(tok)) if tok else None,
                "secs": float(statistics.median(secs)) if secs else None,
            }

        all_agents = sorted(set(cur_stats.keys()) | set(baseline.keys()))
        for name in all_agents:
            if name in cur_stats and name in baseline:
                c = cur_stats[name]
                b = baseline[name]
                d_runs = c["runs"] - b["runs"]
                d_pct = c["pct"] - b["pct"]
                d_tok = None if c["tok"] is None or b["tok"] is None else c["tok"] - b["tok"]
                d_secs = None if c["secs"] is None or b["secs"] is None else c["secs"] - b["secs"]
                out.append(
                    f"{name:<16}"
                    f"{_fmt_delta(d_runs):>8}"
                    f"{_fmt_delta(d_pct, is_pct=True):>10}"
                    f"{UNKNOWN if d_tok is None else _fmt_delta(d_tok):>12}"
                    f"{UNKNOWN if d_secs is None else _fmt_delta(d_secs):>12}"
                )
            elif name in cur_stats:
                out.append(f"{name:<16}{'new':>8}")
            else:
                out.append(f"{name:<16}{'gone':>8}")

    return "\n".join(out) if out else "no rows"


def main(argv=None):
    ap = argparse.ArgumentParser(description="Calibration stats for docs/agents/ledger.txt.")
    ap.add_argument("--ledger", default=DEFAULT_LEDGER, help="ledger file (default: docs/agents/ledger.txt)")
    ap.add_argument("--agent", help="report only this agent")
    ap.add_argument("--area", help="report only this area")
    ap.add_argument("--since", help="filter runs on or after YYYY-MM-DD")
    ap.add_argument("--save-baseline", help="save current baseline stats to PATH")
    ap.add_argument("--baseline", help="compare with baseline stats from PATH")
    args = ap.parse_args(argv)

    if args.since:
        if not re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}", args.since):
            print("bad --since", file=sys.stderr)
            return 2

    try:
        rows, errors = parse(args.ledger)
    except OSError as exc:
        print(f"cannot read ledger: {exc}", file=sys.stderr)
        return 2

    for lineno, reason in errors:
        print(f"line {lineno}: {reason}", file=sys.stderr)

    if args.save_baseline:
        try:
            save_baseline(rows, args.ledger, args.save_baseline)
        except OSError as exc:
            print(f"cannot write baseline: {exc}", file=sys.stderr)
            return 2
        return 1 if errors else 0

    base_data = None
    if args.baseline:
        try:
            base_data, base_err = load_baseline(args.baseline)
        except OSError as exc:
            print(f"cannot read baseline: {exc}", file=sys.stderr)
            return 2
        if base_err:
            print(base_err, file=sys.stderr)
            return 2

    print(report(rows, agent=args.agent, area=args.area, since=args.since, baseline=base_data))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
