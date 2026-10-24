#!/usr/bin/env python3
"""CI audit: the .github/workflows/ci.yml workflow and the timeouts of the QEMU tests.

Usage: python scripts/ci_audit.py [--gh] [--ci PATH]

Exit 0 when there is no problem, 1 when at least one is found.
INFO and WARN lines do not affect the exit code.
"""

import argparse
import json
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path

try:
    import yaml
except ImportError:  # handled in check_ci
    yaml = None

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CI = ROOT / ".github" / "workflows" / "ci.yml"
QEMU_TESTS = [
    ROOT / "tests" / "qemu_test.py",
    ROOT / "tests" / "rgb30" / "qemu_test.py",
]
TEST_MAIN = ROOT / "tests" / "qemu_test.py"
MAKEFILE = ROOT / "Makefile"

# key (substring of the expect needle) -> minimum timeout in seconds
KNOWN_WAITS = {"3d bench: waiting": 19}

# expect("needle", ... timeout=<int>): needle only if literal (no f-string)
EXPECT_RE = re.compile(
    r"""\bexpect\(\s*(["'])((?:\\.|(?!\1).)*?)\1\s*,[^()]*?\btimeout\s*=\s*(\d+)(?![\d.])"""
)
DEF_RE = re.compile(r"^\s*def\s+(\w+)\s*\(")
GAME_REF_RE = re.compile(r"carts/(\w+)\.bm")

problems = []


def rel(path):
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def read_text(path):
    return path.read_text(encoding="utf-8", errors="replace")


def problem(check, msg):
    line = f"PROBLEM [{check}] {msg}"
    problems.append(line)
    print(line)


def info(msg):
    print(f"INFO {msg}")


def warn(msg):
    print(f"WARN {msg}")


def check_ci(ci_path):
    """Check 1 (timeout-minutes per job, concurrency) and check 2 (sync/** in branches-ignore)."""
    name = rel(ci_path)
    if yaml is None:
        problem("ci", "PyYAML not installed (pip install pyyaml)")
        return
    if not ci_path.is_file():
        problem("ci", f"{name}: file not found")
        return
    try:
        data = yaml.safe_load(read_text(ci_path))
    except yaml.YAMLError as e:
        problem("ci", f"{name}: invalid YAML ({e})")
        return
    if not isinstance(data, dict):
        problem("ci", f"{name}: the YAML document is not a mapping")
        return

    # Check 1: every job has timeout-minutes; the workflow has a top-level concurrency.
    jobs = data.get("jobs") or {}
    if not isinstance(jobs, dict):
        problem("ci", f"{name}: 'jobs' is not a mapping")
    else:
        for job_id, job in jobs.items():
            if not isinstance(job, dict) or "timeout-minutes" not in job:
                problem("ci", f"{name}: job '{job_id}' without timeout-minutes")
    if "concurrency" not in data:
        problem("ci", f"{name}: top-level concurrency missing")

    # Check 2: on.push.branches-ignore contains 'sync/**'.
    # PyYAML reads the key 'on' as the boolean True.
    on = data.get("on", data.get(True))
    if on is None:
        problem("ci", f"{name}: key 'on' missing")
        return
    push = on.get("push") if isinstance(on, dict) else None
    if not isinstance(push, dict):
        problem("ci", f"{name}: on.push missing or without branches-ignore")
        return
    ignored = push.get("branches-ignore") or []
    if isinstance(ignored, str):
        ignored = [ignored]
    if "sync/**" not in ignored:
        problem("ci", f"{name}: on.push.branches-ignore does not contain 'sync/**'")


def check_waits(path):
    """Check 3: expect() with a known needle and a timeout below the minimum in KNOWN_WAITS."""
    src = read_text(path)
    for m in EXPECT_RE.finditer(src):
        needle = m.group(2)
        timeout = int(m.group(3))
        lineno = src.count("\n", 0, m.start()) + 1
        for key, minimum in KNOWN_WAITS.items():
            if key in needle and timeout < minimum:
                problem(
                    "waits",
                    f"{rel(path)}:{lineno} expect({ascii(needle)}) "
                    f"timeout={timeout} < {minimum} (KNOWN_WAITS['{key}'])",
                )


def read_games():
    """Reads the GAMES list from the 'GAMES := ...' line of the Makefile."""
    try:
        text = read_text(MAKEFILE)
    except OSError:
        return []
    m = re.search(r"^GAMES\s*:=\s*(.*)$", text, re.M)
    return m.group(1).split() if m else []


def check_cart_refs(path, games):
    """Check 4 (informational): test functions that reference carts/<game>.bm."""
    found = {}
    current = None
    for line in read_text(path).splitlines():
        m = DEF_RE.match(line)
        if m:
            current = m.group(1)
        elif line and not line[0].isspace() and not line.startswith(("#", "@", ")")):
            current = None
        if current is None or not current.startswith("test"):
            continue
        for game in GAME_REF_RE.findall(line):
            if game in games:
                found.setdefault(current, set()).add(game)
    if not found:
        info("no test function references carts/<game>.bm")
        return
    for name in sorted(found):
        info(f"{name} -> {', '.join(sorted(found[name]))}")


def parse_ts(value):
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def check_gh():
    """Check 5 (with --gh, informational): average duration and success rate of the latest runs."""
    cmd = [
        "gh", "run", "list", "-w", "ci.yml", "-L", "20",
        "--json", "conclusion,createdAt,updatedAt",
    ]
    try:
        r = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=60,
        )
    except (OSError, subprocess.SubprocessError) as e:
        warn(f"gh not available or failed ({e}): CI statistics skipped")
        return
    if r.returncode != 0:
        detail = (r.stderr or "").strip()[:200]
        warn(f"gh run list failed (exit {r.returncode}): {detail}: CI statistics skipped")
        return
    try:
        runs = json.loads(r.stdout or "[]")
    except json.JSONDecodeError:
        warn("gh run list output is not JSON: CI statistics skipped")
        return
    if not runs:
        info("gh: no runs of ci.yml")
        return

    durations = []
    for run in runs:
        try:
            created = parse_ts(run["createdAt"])
            updated = parse_ts(run["updatedAt"])
        except (KeyError, TypeError, ValueError):
            continue
        durations.append((updated - created).total_seconds() / 60.0)

    ok = sum(1 for run in runs if run.get("conclusion") == "success")
    avg = f"{sum(durations) / len(durations):.1f} min" if durations else "n/a"
    pct = 100.0 * ok / len(runs)
    info(
        f"CI last {len(runs)} runs: average duration {avg} (over {len(durations)}), "
        f"success {ok}/{len(runs)} = {pct:.0f}%"
    )


def main(argv=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")

    ap = argparse.ArgumentParser(description="bm CI audit (workflow and test timeouts).")
    ap.add_argument("--gh", action="store_true", help="statistics of the latest 20 runs via gh (informational)")
    ap.add_argument("--ci", default=None, help="path of ci.yml (default .github/workflows/ci.yml)")
    args = ap.parse_args(argv)

    ci_path = Path(args.ci).resolve() if args.ci else DEFAULT_CI
    check_ci(ci_path)

    for test_file in QEMU_TESTS:
        if test_file.is_file():
            check_waits(test_file)
        else:
            problem("waits", f"{rel(test_file)}: file not found")

    games = read_games()
    if not games:
        info("GAMES not found in the Makefile: check 4 skipped")
    elif TEST_MAIN.is_file():
        check_cart_refs(TEST_MAIN, games)
    else:
        info(f"{rel(TEST_MAIN)} not found: check 4 skipped")

    if args.gh:
        check_gh()

    n = len(problems)
    if n:
        print(f"SUMMARY: {n} problem(s) found")
    else:
        print("SUMMARY: OK, 0 problems")
    return 1 if n else 0


if __name__ == "__main__":
    sys.exit(main())
