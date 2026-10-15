#!/usr/bin/env python3
"""Audit CI: workflow .github/workflows/ci.yml e timeout dei test QEMU.

Uso: python scripts/ci_audit.py [--gh] [--ci PATH]

Exit 0 se nessun problema, 1 se ne viene trovato almeno uno.
Le righe INFO e WARN non influenzano l'exit code.
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
except ImportError:  # gestito in check_ci
    yaml = None

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CI = ROOT / ".github" / "workflows" / "ci.yml"
QEMU_TESTS = [
    ROOT / "tests" / "qemu_test.py",
    ROOT / "tests" / "rgb30" / "qemu_test.py",
]
TEST_MAIN = ROOT / "tests" / "qemu_test.py"
MAKEFILE = ROOT / "Makefile"

# chiave (sottostringa del needle di expect) -> timeout minimo in secondi
KNOWN_WAITS = {"stress settle": 19}

# expect("needle", ... timeout=<int>): needle solo se letterale (no f-string)
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
    """Check 1 (timeout-minutes per job, concurrency) e check 2 (sync/** in branches-ignore)."""
    name = rel(ci_path)
    if yaml is None:
        problem("ci", "PyYAML non installato (pip install pyyaml)")
        return
    if not ci_path.is_file():
        problem("ci", f"{name}: file non trovato")
        return
    try:
        data = yaml.safe_load(read_text(ci_path))
    except yaml.YAMLError as e:
        problem("ci", f"{name}: YAML non valido ({e})")
        return
    if not isinstance(data, dict):
        problem("ci", f"{name}: il documento YAML non e' un mapping")
        return

    # Check 1: ogni job ha timeout-minutes; workflow ha concurrency top-level.
    jobs = data.get("jobs") or {}
    if not isinstance(jobs, dict):
        problem("ci", f"{name}: 'jobs' non e' un mapping")
    else:
        for job_id, job in jobs.items():
            if not isinstance(job, dict) or "timeout-minutes" not in job:
                problem("ci", f"{name}: job '{job_id}' senza timeout-minutes")
    if "concurrency" not in data:
        problem("ci", f"{name}: concurrency top-level mancante")

    # Check 2: on.push.branches-ignore contiene 'sync/**'.
    # PyYAML legge la chiave 'on' come booleano True.
    on = data.get("on", data.get(True))
    if on is None:
        problem("ci", f"{name}: chiave 'on' mancante")
        return
    push = on.get("push") if isinstance(on, dict) else None
    if not isinstance(push, dict):
        problem("ci", f"{name}: on.push mancante o senza branches-ignore")
        return
    ignored = push.get("branches-ignore") or []
    if isinstance(ignored, str):
        ignored = [ignored]
    if "sync/**" not in ignored:
        problem("ci", f"{name}: on.push.branches-ignore non contiene 'sync/**'")


def check_waits(path):
    """Check 3: expect() con needle noto e timeout inferiore al minimo in KNOWN_WAITS."""
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
    """Legge la lista GAMES dalla riga 'GAMES := ...' del Makefile."""
    try:
        text = read_text(MAKEFILE)
    except OSError:
        return []
    m = re.search(r"^GAMES\s*:=\s*(.*)$", text, re.M)
    return m.group(1).split() if m else []


def check_cart_refs(path, games):
    """Check 4 (informativo): test functions che referenziano carts/<game>.bm."""
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
        info("nessuna test function riferisce carts/<game>.bm")
        return
    for name in sorted(found):
        info(f"{name} -> {', '.join(sorted(found[name]))}")


def parse_ts(value):
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def check_gh():
    """Check 5 (con --gh, informativo): durata media e quota di successi delle ultime run."""
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
        warn(f"gh non disponibile o fallito ({e}): statistiche CI saltate")
        return
    if r.returncode != 0:
        detail = (r.stderr or "").strip()[:200]
        warn(f"gh run list fallito (exit {r.returncode}): {detail}: statistiche CI saltate")
        return
    try:
        runs = json.loads(r.stdout or "[]")
    except json.JSONDecodeError:
        warn("output di gh run list non e' JSON: statistiche CI saltate")
        return
    if not runs:
        info("gh: nessuna run di ci.yml")
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
    avg = f"{sum(durations) / len(durations):.1f} min" if durations else "n/d"
    pct = 100.0 * ok / len(runs)
    info(
        f"CI ultime {len(runs)} run: durata media {avg} (su {len(durations)}), "
        f"success {ok}/{len(runs)} = {pct:.0f}%"
    )


def main(argv=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")

    ap = argparse.ArgumentParser(description="Audit CI di bm (workflow e timeout dei test).")
    ap.add_argument("--gh", action="store_true", help="statistiche delle ultime 20 run via gh (informativo)")
    ap.add_argument("--ci", default=None, help="percorso di ci.yml (default .github/workflows/ci.yml)")
    args = ap.parse_args(argv)

    ci_path = Path(args.ci).resolve() if args.ci else DEFAULT_CI
    check_ci(ci_path)

    for test_file in QEMU_TESTS:
        if test_file.is_file():
            check_waits(test_file)
        else:
            problem("waits", f"{rel(test_file)}: file non trovato")

    games = read_games()
    if not games:
        info("GAMES non trovato nel Makefile: check 4 saltato")
    elif TEST_MAIN.is_file():
        check_cart_refs(TEST_MAIN, games)
    else:
        info(f"{rel(TEST_MAIN)} non trovato: check 4 saltato")

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
