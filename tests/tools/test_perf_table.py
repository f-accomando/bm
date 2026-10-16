#!/usr/bin/env python3
"""Tests for scripts/perf_table.py: the script runs as a subprocess on text fixtures.

Run with: python tests/tools/test_perf_table.py   (exit 0 = all pass, 1 = failures)
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SCRIPT = os.path.join(ROOT, "scripts", "perf_table.py")
BEGIN = "<!-- budget:begin -->"
END = "<!-- budget:end -->"
EMPTY = "—"

REPORT_RGB30 = (
    "bm report\n"
    "kind: budget\n"
    "kernel: 0.9.1\n"
    "branch: bm-core\n"
    "board: RGB30\n"
    "date: 2026-10-16 10:00:00\n"
    "file: 20261016-100000_budget_rgb30_0.9.1.txt\n"
    "\n"
    "bm budget 1\n"
    "screen 640x360\n"
    "gpu3d 0\n"
    "test,unit,n60,n30,us_item,tris60\n"
    "spr16,spr,812,1620,12.34,\n"
    "tiles8,tile,300,600,5.00,\n"
    "mesh96,obj,1,3,15000.00,96\n"
    "part2d,part,400,800,2.50,\n"
    "point3d,pt,500,1000,2.00,\n"
    "profile_load,spr16,812\n"
    "profile,draw,main.lua:42,0.120,0.300\n"
)
REPORT_QEMU = REPORT_RGB30.replace("board: RGB30", "board: QEMU virt")
REPORT_KIND = REPORT_RGB30.replace("kind: budget", "kind: stress")
REPORT_NOT_BM = "hello\nworld\n"

DOC = (
    "# Performance\n"
    "\n"
    "intro\n"
    "\n"
    f"{BEGIN}\n"
    "| Load | Unit | Pi Zero W (60 / 30 fps) | RGB30 (60 / 30 fps) |\n"
    "| --- | --- | --- | --- |\n"
    "| spr16 | spr | P1 | — |\n"
    "| tiles8 | tile | P2 | — |\n"
    "| mesh96 | obj | P3 | — |\n"
    "| part2d | part | P4 | — |\n"
    "| point3d | pt | P5 | — |\n"
    "| source |  | P6 | — |\n"
    f"{END}\n"
    "\n"
    "tail\n"
)
PI_BEFORE = {"spr16": "P1", "tiles8": "P2", "mesh96": "P3", "part2d": "P4", "point3d": "P5", "source": "P6"}
EXPECTED = {
    "spr16": "812 / 1620",
    "tiles8": "300 / 600",
    "mesh96": "1 / 3 (96 tri)",
    "part2d": "400 / 800",
    "point3d": "500 / 1000",
    "source": "RGB30, 0.9.1, 2026-10-16 10:00:00, 640x360",
}


def check(cond, msg):
    if not cond:
        raise AssertionError(msg)


def run_script(args):
    res = subprocess.run([sys.executable, SCRIPT, *args], capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    return res.returncode, res.stderr


def run_case(report_text, doc_text, column="rgb30", extra=()):
    """Run the script on temp files; return (exit code, stderr, doc text after the run)."""
    with tempfile.TemporaryDirectory() as tmp:
        rpt = os.path.join(tmp, "RPT00001.TXT")
        doc = os.path.join(tmp, "PERFORMANCE.md")
        with open(rpt, "w", encoding="utf-8", newline="") as fh:
            fh.write(report_text)
        with open(doc, "w", encoding="utf-8", newline="") as fh:
            fh.write(doc_text)
        rc, err = run_script(["--column", column, rpt, "--doc", doc, *extra])
        with open(doc, encoding="utf-8", newline="") as fh:
            after = fh.read()
    return rc, err, after


def table_cells(text):
    """{load: (pi, rgb30)} of the table between the markers."""
    begin = text.index(BEGIN) + len(BEGIN)
    cells = {}
    for line in text[begin:text.index(END)].splitlines():
        s = line.strip()
        if not s.startswith("|"):
            continue
        parts = [c.strip() for c in s[1:].rstrip("|").split("|")]
        if len(parts) == 4 and parts[0] not in ("Load", "---"):
            cells[parts[0]] = (parts[2], parts[3])
    return cells


def test_rgb30_report_fills_rgb30_only():
    rc, err, after = run_case(REPORT_RGB30, DOC, "rgb30")
    check(rc == 0, f"exit {rc}, stderr: {err}")
    cells = table_cells(after)
    for name, pi in PI_BEFORE.items():
        check(cells.get(name) == (pi, EXPECTED[name]), f"{name}: {cells.get(name)}")
    check(list(cells)[-1] == "source", "source row is not the last row")
    check(after[:after.index(BEGIN)] == DOC[:DOC.index(BEGIN)], "text before the markers changed")
    check(after[after.index(END):] == DOC[DOC.index(END):], "text after the markers changed")


def test_pi_report_leaves_rgb30_alone():
    rc, err, after = run_case(REPORT_RGB30, DOC, "pi")
    check(rc == 0, f"exit {rc}, stderr: {err}")
    cells = table_cells(after)
    for name in PI_BEFORE:
        check(cells.get(name) == (EXPECTED[name], EMPTY), f"{name}: {cells.get(name)}")


def test_qemu_report_rejected():
    rc, err, after = run_case(REPORT_QEMU, DOC, "rgb30")
    check(rc == 1, f"exit {rc}, expected 1")
    check("QEMU report: not a hardware measurement" in err, f"stderr: {err!r}")
    check(after == DOC, "doc changed on a rejected QEMU report")


def test_qemu_report_accepted_with_flag():
    rc, err, after = run_case(REPORT_QEMU, DOC, "rgb30", extra=["--allow-qemu"])
    check(rc == 0, f"exit {rc}, stderr: {err}")
    cells = table_cells(after)
    check(cells["spr16"] == ("P1", "812 / 1620"), f"spr16: {cells['spr16']}")
    check(cells["source"][1] == "QEMU virt, 0.9.1, 2026-10-16 10:00:00, 640x360",
          f"source: {cells['source']}")


def test_missing_markers_exit_1():
    doc = DOC.replace(BEGIN + "\n", "")
    rc, err, after = run_case(REPORT_RGB30, doc, "rgb30")
    check(rc == 1, f"exit {rc}, expected 1")
    check("markers" in err, f"stderr: {err!r}")
    check(after == doc, "doc changed without markers")


def test_kind_mismatch_exit_1():
    rc, err, after = run_case(REPORT_KIND, DOC, "rgb30")
    check(rc == 1, f"exit {rc}, expected 1")
    check("kind" in err, f"stderr: {err!r}")
    check(after == DOC, "doc changed on a wrong kind")


def test_not_a_bm_report_exit_1():
    rc, _err, after = run_case(REPORT_NOT_BM, DOC, "rgb30")
    check(rc == 1, f"exit {rc}, expected 1")
    check(after == DOC, "doc changed on an invalid report")


def test_missing_report_file_exit_2():
    with tempfile.TemporaryDirectory() as tmp:
        doc = os.path.join(tmp, "PERFORMANCE.md")
        with open(doc, "w", encoding="utf-8", newline="") as fh:
            fh.write(DOC)
        rc, _err = run_script(["--column", "pi", os.path.join(tmp, "missing.txt"), "--doc", doc])
    check(rc == 2, f"exit {rc}, expected 2")


def test_bad_column_exit_2():
    rc, _err = run_script(["--column", "xbox", "x.txt"])
    check(rc == 2, f"exit {rc}, expected 2")


def test_second_run_is_stable():
    rc1, _err1, once = run_case(REPORT_RGB30, DOC, "rgb30")
    rc2, _err2, twice = run_case(REPORT_RGB30, once, "rgb30")
    check(rc1 == 0 and rc2 == 0, f"exit {rc1}, {rc2}")
    check(once == twice, "second run changed the table")


TESTS = [
    test_rgb30_report_fills_rgb30_only,
    test_pi_report_leaves_rgb30_alone,
    test_qemu_report_rejected,
    test_qemu_report_accepted_with_flag,
    test_missing_markers_exit_1,
    test_kind_mismatch_exit_1,
    test_not_a_bm_report_exit_1,
    test_missing_report_file_exit_2,
    test_bad_column_exit_2,
    test_second_run_is_stable,
]


def main():
    failed = 0
    for fn in TESTS:
        try:
            fn()
        except AssertionError as exc:
            failed += 1
            print(f"FAIL {fn.__name__}: {exc}")
        else:
            print(f"PASS {fn.__name__}")
    if failed:
        print(f"{failed} of {len(TESTS)} failed")
        return 1
    print(f"All {len(TESTS)} perf_table tests PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
