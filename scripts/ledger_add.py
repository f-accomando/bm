#!/usr/bin/env python3
"""Append one validated row to the sub-agent ledger (docs/agents/ledger.txt).

Row format: date|agent|model|task|area|gate|necessity|outcome|tokens|tools|secs

Validation reuses scripts/agent_audit.py (AGENTS, FIELDS, _check), so a row
accepted here is read without warnings by agent_audit.py. A row identical to an
existing one is not added (warning on stderr, exit 0) unless --force. The
file's line ending is kept and a missing final newline is restored first.

Example:
  python scripts/ledger_add.py --agent general-purpose --model sonnet \\
      --task "review AGY-1 API-SRC" --area sync --tokens 73196 --tools 7 --secs 66

Exit codes: 0 ok (added, printed with --no-write, or duplicate skipped),
2 bad usage, validation failure or I/O error."""
import argparse, datetime, os, shlex, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import agent_audit as aa  # AGENTS, FIELDS, _check live there

DEFAULT_LEDGER = aa.DEFAULT_LEDGER


def build_row(args):
    """Return (line, error): error is None when every field is valid."""
    values = {"date": args.date, "agent": args.agent, "model": args.model,
              "task": args.task, "area": args.area, "gate": args.gate,
              "necessity": args.necessity, "outcome": args.outcome,
              "tokens": args.tokens, "tools": args.tools, "secs": args.secs}
    for key, val in values.items():
        if "|" in val or "\n" in val or "\r" in val:
            return None, f"{key} must not contain '|' or a newline"
    if values["agent"] not in aa.AGENTS:
        return None, "unknown agent '%s'; allowed: %s" % (
            values["agent"], ", ".join(sorted(aa.AGENTS)))
    try:
        datetime.date.fromisoformat(values["date"])
    except ValueError:
        return None, "date must be YYYY-MM-DD"
    for key in ("model", "task", "area"):
        values[key] = values[key].strip()
    reason = aa._check(values)  # same rules as agent_audit.py (outcome, ints, gate...)
    if reason:
        return None, reason
    return "|".join(values[k] for k in aa.FIELDS), None


FIELD_DEFAULTS = {"gate": "-", "necessity": "-", "outcome": "DONE",
                  "tokens": aa.UNKNOWN, "tools": aa.UNKNOWN, "secs": aa.UNKNOWN}
MERGE_KEYS = ("agent", "model", "task", "area", "gate", "necessity", "outcome",
              "tokens", "tools", "secs", "date")


def make_parser():
    ap = argparse.ArgumentParser(description="Append a validated row to the sub-agent ledger.")
    for key in MERGE_KEYS:  # None = not given; defaults are applied after the merge
        ap.add_argument("--" + key, default=None)
    ap.add_argument("--file", default=DEFAULT_LEDGER)
    ap.add_argument("--force", action="store_true", help="add even if an identical row exists")
    ap.add_argument("--no-write", action="store_true", help="only print the row")
    ap.add_argument("--from-report", metavar="FILE",
                    help="read the ledger_add.py line of a runner report (- = stdin)")
    return ap


def report_args(text):
    """Return the argument list of the last ledger_add.py command in text, or None."""
    found = None
    for raw in text.splitlines():
        pos = raw.find("ledger_add.py")
        if pos < 0:
            continue
        rest = raw[pos + len("ledger_add.py"):].split("`")[0]  # stop at a closing backtick
        try:
            found = shlex.split(rest)
        except ValueError:
            continue
    return found


def main(argv=None):
    ap = make_parser()
    try:
        args = ap.parse_args(argv)
    except SystemExit as exc:
        return 2 if exc.code else 0
    if args.from_report:
        try:
            if args.from_report == "-":
                text = sys.stdin.read()
            else:
                with open(args.from_report, encoding="utf-8-sig") as fh:
                    text = fh.read()
        except (OSError, UnicodeDecodeError) as exc:
            print(f"ledger_add: cannot read report: {exc}", file=sys.stderr)
            return 2
        words = report_args(text)
        if words is None:
            print("ledger_add: no ledger_add.py command line in the report", file=sys.stderr)
            return 2
        try:
            rep = make_parser().parse_args(words)
        except SystemExit:
            print("ledger_add: the command line in the report is not valid", file=sys.stderr)
            return 2
        for key in MERGE_KEYS:
            if getattr(args, key) is None:
                setattr(args, key, getattr(rep, key))
    for key in ("agent", "model", "task", "area"):
        if getattr(args, key) is None:
            print(f"ledger_add: --{key} is required", file=sys.stderr)
            return 2
    for key, val in FIELD_DEFAULTS.items():
        if getattr(args, key) is None:
            setattr(args, key, val)
    if args.date is None:
        args.date = datetime.date.today().isoformat()
    if args.outcome == "FAILED":
        args.outcome = "FAILED x2"
    line, err = build_row(args)
    if err:
        print(f"ledger_add: {err}", file=sys.stderr)
        return 2
    if args.no_write:
        print(line)
        return 0
    try:
        data = b""
        if os.path.exists(args.file):
            with open(args.file, "rb") as fh:
                data = fh.read()
        text = data.decode("utf-8-sig")
        eol = "\r\n" if "\r\n" in text else "\n"
        if not args.force and line in [l.rstrip("\r") for l in text.split("\n")]:
            print(f"ledger_add: identical row already present, not added: {line}", file=sys.stderr)
            return 0
        prefix = b"" if not data or data.endswith(b"\n") else eol.encode()
        with open(args.file, "ab") as fh:
            fh.write(prefix + (line + eol).encode("utf-8"))
    except (OSError, UnicodeDecodeError) as exc:
        print(f"ledger_add: cannot write ledger: {exc}", file=sys.stderr)
        return 2
    print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
