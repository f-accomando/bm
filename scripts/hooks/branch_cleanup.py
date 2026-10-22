#!/usr/bin/env python3
"""Hook SessionStart and PostToolUse(Bash): print the branches that are safe to delete
after a merge (read-only report from tools/branch_audit.py, DELETE and REVIEW only).
Silent when nothing qualifies. PostToolUse runs only after `gh pr merge` or `git merge`.
Deleting stays manual: python tools/branch_audit.py --repo . --delete-local
Env: BRANCH_AUDIT (default ../bm33-sync/tools/branch_audit.py)."""
import json, os, subprocess, sys

root = os.environ.get("CLAUDE_PROJECT_DIR") or os.getcwd()
try:
    inp = json.load(sys.stdin)
except ValueError:
    inp = {}
if inp.get("hook_event_name") == "PostToolUse":
    cmd = (inp.get("tool_input") or {}).get("command", "")
    if "gh pr merge" not in cmd and "git merge" not in cmd:
        sys.exit(0)
tool = os.environ.get("BRANCH_AUDIT") or os.path.normpath(os.path.join(root, "..", "bm33-sync", "tools", "branch_audit.py"))
if not os.path.exists(tool):
    sys.exit(0)
try:
    out = subprocess.run([sys.executable, tool, "--repo", root], capture_output=True, text=True, timeout=90).stdout
except Exception:
    sys.exit(0)
rows = [l for l in out.splitlines() if l.startswith(("DELETE", "REVIEW"))]
if rows:
    print("[branch_cleanup] merged branches (delete with: python %s --repo . --delete-local):" % tool)
    print("\n".join(rows[:15]))
