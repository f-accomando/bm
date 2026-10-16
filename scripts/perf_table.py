#!/usr/bin/env python3
"""Compile the game budget table of docs/PERFORMANCE.md from a `budget` report.

    python scripts/perf_table.py --column pi|rgb30 REPORT.TXT [--doc PATH] [--allow-qemu]

Only the chosen column is rewritten; the other one is kept as it is. The table
lives between the markers <!-- budget:begin --> and <!-- budget:end -->.

Exit codes: 0 table written; 1 invalid report, QEMU report without --allow-qemu,
or markers missing/invalid; 2 usage or I/O error.
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DOC = os.path.join(ROOT, "docs", "PERFORMANCE.md")
BEGIN = "<!-- budget:begin -->"
END = "<!-- budget:end -->"
TABLE_HEADER = "| Load | Unit | Pi Zero W (60 / 30 fps) | RGB30 (60 / 30 fps) |"
TABLE_SEP = "| --- | --- | --- | --- |"
EMPTY = "—"  # em dash, cell not measured yet
TEST_HEADER = "test,unit,n60,n30,us_item,tris60"
# index among the two value cells of a row: [pi, rgb30]
COLUMNS = {"pi": 0, "rgb30": 1}


class CompileError(Exception):
    """Invalid report or markers: exit 1."""


def parse_report(text):
    """Return (header, tests, screen) of a budget report; raise CompileError if invalid.

    header: dict of the "key: value" lines before the empty line (as report_parse in src/net/report.c).
    tests: list of the 6-field rows after the test header, up to the profile lines.
    """
    lines = text.split("\n")
    if lines[0] != "bm report":
        raise CompileError("not a bm report (first line must be 'bm report')")
    header = {}
    i = 1
    while i < len(lines) and lines[i] != "":
        key, sep, value = lines[i].partition(":")
        if sep:
            header[key] = value.strip()
        i += 1
    if header.get("kind") != "budget":
        raise CompileError(f"kind is {header.get('kind', '(missing)')!r}, expected 'budget'")
    body = [line.rstrip() for line in lines[i + 1:]]
    if "bm budget 1" not in body:
        raise CompileError("missing 'bm budget 1' line")
    screen = next((line[len("screen "):].strip() for line in body if line.startswith("screen ")), "")
    if not screen:
        raise CompileError("missing 'screen <W>x<H>' line")
    if TEST_HEADER not in body:
        raise CompileError("missing test header line")
    start = body.index(TEST_HEADER)
    tests = []
    for line in body[start + 1:]:
        if line == "":
            continue
        fields = line.split(",")
        if fields[0] in ("profile", "profile_load"):
            break
        if len(fields) != 6 or not all(fields[:5]):
            raise CompileError(f"bad test row: {line!r}")
        tests.append(fields)
    if not tests:
        raise CompileError("no test rows after the test header")
    return header, tests, screen


def source_cell(header, screen):
    date = header.get("date", "")
    if date.startswith("unknown"):  # report_parse: "unknown (no network time)" means no date
        date = ""
    fields = [header.get("board", ""), header.get("kernel", ""), date, screen]
    return ", ".join(f or "unknown" for f in fields)


def test_cell(fields):
    name, _unit, n60, n30, _us, tris = fields
    cell = f"{n60} / {n30}"
    if name == "mesh96" and tris:
        cell += f" ({tris} tri)"
    return cell


def split_row(line):
    """Cells of a markdown table row, or None if the line is not a row."""
    s = line.strip()
    if not s.startswith("|"):
        return None
    body = s[1:]
    if body.endswith("|"):
        body = body[:-1]
    return [c.strip() for c in body.split("|")]


def parse_table(lines):
    """Data rows [load, unit, pi, rgb30] of the existing table, in order (header and separator skipped)."""
    rows = []
    for line in lines:
        cells = split_row(line)
        if cells is None or len(cells) != 4:
            continue
        if cells[0] == "Load" or all(c and set(c) <= set("-:") for c in cells):
            continue
        rows.append([cells[0], cells[1], cells[2] or EMPTY, cells[3] or EMPTY])
    return rows


def fmt_row(cells):
    return "|" + "|".join(f" {c} " if c else " " for c in cells) + "|"


def build_table(old, idx, tests, source):
    """Table lines: existing rows keep their place and their other column; source row is last."""
    rows = [r for r in old if r[0] != "source"]
    by_name = {r[0]: r for r in rows}
    for fields in tests:
        row = by_name.get(fields[0])
        if row is None:
            row = [fields[0], fields[1], EMPTY, EMPTY]
            rows.append(row)
            by_name[fields[0]] = row
        row[2 + idx] = test_cell(fields)
    src = next((r for r in old if r[0] == "source"), ["source", "", EMPTY, EMPTY])
    src = ["source", "", src[2], src[3]]
    src[2 + idx] = source
    return [TABLE_HEADER, TABLE_SEP] + [fmt_row(r) for r in rows + [src]]


def splice(text, column, tests, source):
    """Return the document text with the block between the markers rewritten."""
    begin = text.find(BEGIN)
    end = text.find(END)
    if (begin < 0 or end < 0 or text.count(BEGIN) != 1 or text.count(END) != 1
            or end < begin + len(BEGIN)):
        raise CompileError("markers <!-- budget:begin --> / <!-- budget:end --> missing or invalid")
    eol = "\r\n" if "\r\n" in text else "\n"
    old = parse_table(text[begin + len(BEGIN):end].splitlines())
    lines = build_table(old, COLUMNS[column], tests, source)
    return text[:begin + len(BEGIN)] + eol + eol.join(lines) + eol + text[end:]


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Compile the game budget table in docs/PERFORMANCE.md from a budget report.")
    parser.add_argument("--column", required=True, choices=sorted(COLUMNS), help="column to fill")
    parser.add_argument("report", help="REPORT.TXT written by carts/budget")
    parser.add_argument("--doc", default=DEFAULT_DOC, help="markdown file with the budget markers")
    parser.add_argument("--allow-qemu", action="store_true",
                        help="accept a QEMU report (not a hardware measurement)")
    args = parser.parse_args(argv)

    try:
        with open(args.report, "rb") as fh:
            report_text = fh.read().decode("utf-8", errors="replace").replace("\r\n", "\n")
    except OSError as exc:
        print(f"perf_table: cannot read report: {exc}", file=sys.stderr)
        return 2
    try:
        header, tests, screen = parse_report(report_text)
    except CompileError as exc:
        print(f"perf_table: invalid report: {exc}", file=sys.stderr)
        return 1
    if "QEMU" in header.get("board", "") and not args.allow_qemu:
        print("QEMU report: not a hardware measurement", file=sys.stderr)
        return 1

    try:
        with open(args.doc, encoding="utf-8", newline="") as fh:
            doc_text = fh.read()
    except (OSError, UnicodeDecodeError) as exc:
        print(f"perf_table: cannot read doc: {exc}", file=sys.stderr)
        return 2
    try:
        new_text = splice(doc_text, args.column, tests, source_cell(header, screen))
    except CompileError as exc:
        print(f"perf_table: {exc}", file=sys.stderr)
        return 1

    tmp = args.doc + ".tmp"
    try:
        with open(tmp, "w", encoding="utf-8", newline="") as fh:
            fh.write(new_text)
        os.replace(tmp, args.doc)
    except OSError as exc:
        print(f"perf_table: cannot write doc: {exc}", file=sys.stderr)
        return 2
    print(f"perf_table: {args.column} column written to {args.doc}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
