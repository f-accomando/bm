#!/usr/bin/env python3
"""Hook UserPromptSubmit (SPV only): print new DELIVERED/BLOCKED/request lines that
operators pushed to the sync branch since the last prompt. Silent when nothing new.
Opt-in: .claude/state/spv_sync.on holds the session id that wants it.
Env: SYNC_WORKTREE (default ../bm33-sync), SYNC_BRANCH (default sync/protocollo-ottobre)."""
import json, os, re, subprocess, sys, time

root = os.environ.get("CLAUDE_PROJECT_DIR") or os.getcwd()
try:
    inp = json.load(sys.stdin)
except ValueError:
    sys.exit(0)
state = os.path.join(root, ".claude", "state")
flag = os.path.join(state, "spv_sync.on")
if not os.path.exists(flag) or open(flag).read().strip() != inp.get("session_id", ""):
    sys.exit(0)
wt = os.environ.get("SYNC_WORKTREE") or os.path.normpath(os.path.join(root, "..", "bm33-sync"))
br = os.environ.get("SYNC_BRANCH", "sync/protocollo-ottobre")
seen_f = os.path.join(state, "spv_sync.seen")
stamp_f = os.path.join(state, "spv_sync.stamp")


def git(*a):
    return subprocess.run(["git", "-C", wt] + list(a), capture_output=True, text=True, timeout=60).stdout.strip()


try:
    if time.time() - (os.path.getmtime(stamp_f) if os.path.exists(stamp_f) else 0) > 90:
        git("fetch", "-q", "origin", br)
        open(stamp_f, "w").close()
    head = git("rev-parse", "origin/" + br)
    last = open(seen_f).read().strip() if os.path.exists(seen_f) else ""
    if not head or head == last:
        sys.exit(0)
    if last:
        diff = git("diff", "-U0", last, head, "--", "sync/NET.md", "sync/B16.md", "sync/CI.md", "sync/AGX.md")
        hits = []
        cur = ""
        for l in diff.splitlines():
            m = re.match(r"\+\+\+ b/sync/(\w+)\.md", l)
            if m:
                cur = m.group(1)
            elif l.startswith("+") and not l.startswith("+++") and re.search(r"DELIVERED|BLOCKED|BLOCCATO|richiesta utente|Chiusura", l):
                hits.append("%s: %s" % (cur, l[1:].strip()[:170]))
        if hits:
            print("[sync_watch] new on %s (not yet in your worktree, run git pull --rebase --autostash):" % br)
            print("\n".join(hits[:8]))
    open(seen_f, "w").write(head)
except Exception:
    sys.exit(0)
