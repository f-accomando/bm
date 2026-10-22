#!/usr/bin/env python3
"""Heartbeat and sync report of Claude cloud, in one command (SYNC.md rules of 2026-10-14: a HEARTBEAT row every 30 min).

  python3 video/3d/hb.py --task "video 4 prep" --pct 60 --note "waiting for the editor patch"   # report + heartbeat row + push
  python3 video/3d/hb.py --report                                                                # only the report (no row, no push)

The report: new commits on the other agents' branches since the last run (state in .git/hb_seen), the last Log rows written by
the others, the recordings in progress (ps rec_qemu) and the newest console.mkv sizes. Then the row is appended to SYNC.md,
committed and pushed to the current branch. A subagent (model haiku) can run it: no decisions inside.
"""
import argparse
import json
import os
import subprocess
import sys
import time

ROOT = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True).stdout.strip()
SEEN = os.path.join(ROOT, ".git", "hb_seen.json")
BRANCHES = ["origin/bm-core", "origin/feat/claude-video3d", "origin/feat/antigravity-map3d", "origin/feat/grok-docs"]


def git(*a):
    return subprocess.run(["git", *a], cwd=ROOT, capture_output=True, text=True).stdout.strip()


def report():
    subprocess.run(["git", "fetch", "-q", "origin"], cwd=ROOT)
    seen = json.load(open(SEEN)) if os.path.exists(SEEN) else {}
    now = {}
    for b in BRANCHES:
        sha = git("rev-parse", "--verify", "-q", b)
        if not sha:
            continue
        now[b] = sha
        if seen.get(b) != sha:
            rng = (seen[b] + ".." + sha) if b in seen else "-5"
            print("NEW on %s:" % b)
            print("   " + git("log", "--oneline", rng if b in seen else sha, *( [] if b in seen else ["-5"])).replace("\n", "\n   "))
    json.dump(now, open(SEEN, "w"))
    log = git("show", "origin/bm-core:SYNC.md").splitlines() + git("show", "origin/feat/claude-video3d:SYNC.md").splitlines()
    rows = [l for l in log if l.startswith("| 2026-") and "Claude cloud" not in l.split("|")[2]]
    print("last Log rows of the others (state column):")
    for l in rows[-6:]:
        c = [x.strip() for x in l.split("|")]
        print("   %s %s %s %s" % (c[1], c[2], c[4][:40], c[5][:160]))
    ps = subprocess.run(["pgrep", "-af", "^python3 .*rec_qemu"], capture_output=True, text=True).stdout.strip()
    print("recordings running:", ps or "none")
    mk = subprocess.run("ls -la video/3d/*/out/qemu*/console.mkv 2>/dev/null | awk '{print $5, $9}'", shell=True, cwd=ROOT,
                        capture_output=True, text=True).stdout.strip()
    print("console.mkv:\n   " + mk.replace("\n", "\n   ") if mk else "console.mkv: none")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--task")
    ap.add_argument("--pct", default="")
    ap.add_argument("--note", default="")
    ap.add_argument("--report", action="store_true")
    a = ap.parse_args()
    report()
    if a.report or not a.task:
        return 0
    branch = git("rev-parse", "--abbrev-ref", "HEAD")
    row = "| %s | Claude cloud | `%s` | `HEARTBEAT %s %s` | %s |" % (time.strftime("%Y-%m-%d"), branch, a.task,
                                                                  (a.pct + "%") if a.pct else "", a.note)
    p = os.path.join(ROOT, "SYNC.md")
    s = open(p).read().rstrip("\n") + "\n" + row + "\n"
    open(p, "w").write(s)
    subprocess.run(["git", "add", "SYNC.md"], cwd=ROOT)
    subprocess.run(["git", "commit", "-q", "-m", "docs(sync): heartbeat " + a.task], cwd=ROOT)
    r = subprocess.run(["git", "push", "-q", "origin", branch], cwd=ROOT, capture_output=True, text=True)
    print("pushed" if r.returncode == 0 else "PUSH FAILED: " + r.stderr[-200:])
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
