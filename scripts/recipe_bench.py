#!/usr/bin/env python3
"""Bench of the lookup recipes: python scripts/recipe_bench.py bench --area <area>.

Reads docs/agents/recipes/golden.toml. For each entry of the area it runs `baseline` and
`recipe` (a command string, or a list of them; split with shlex, run without a shell from the
repo root) and prints one TSV line: question, baseline bytes, recipe bytes, calls, pass/fail.
An entry passes when `answer` is a substring of the recipe output and the recipe ran clean.
Exit 1 if any entry fails. Stdlib only (Python >= 3.11 for tomllib)."""
import argparse, os, shlex, subprocess, sys, tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GOLDEN = os.path.join(ROOT, "docs", "agents", "recipes", "golden.toml")


def run(cmds):
    """Run the commands; return (stdout bytes joined, number of commands, all exit 0)."""
    if isinstance(cmds, str):
        cmds = [cmds]
    env = dict(os.environ, PYTHONIOENCODING="utf-8")
    out, ok = b"", True
    for c in cmds:
        argv = shlex.split(c)
        if argv and argv[0] == "python":
            argv[0] = sys.executable
        p = subprocess.run(argv, cwd=ROOT, env=env, capture_output=True)
        out += p.stdout
        ok = ok and p.returncode == 0
    return out, len(cmds), ok


def bench(area):
    with open(GOLDEN, "rb") as f:
        entries = [e for e in tomllib.load(f).get("entry", []) if e.get("area") == area]
    if not entries:
        sys.exit("no entries for area " + area)
    fails = 0
    for e in entries:
        base, _, _ = run(e["baseline"])
        rec, calls, ok = run(e["recipe"])
        good = ok and e["answer"] in rec.decode("utf-8", "replace")
        fails += not good
        print("\t".join([e["question"], str(len(base)), str(len(rec)), str(calls), "pass" if good else "fail"]))
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("bench")
    b.add_argument("--area", required=True)
    a = ap.parse_args()
    sys.exit(bench(a.area))


if __name__ == "__main__":
    main()
