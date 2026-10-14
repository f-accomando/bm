#!/usr/bin/env python3
"""A heartbeat that costs no model tokens (docs/progress/sync.md, "Cost of the heartbeats").

  python3 tools/sync_watch.py --id AGY [--branch bm-core] [--every 5] [--beat 15] [--wake-cmd "..."] [--once] [--no-push]

Run it in the operator's own clone (checked out on the sync branch), in a terminal or a background process. Every `--every` minutes:
  1. `git fetch origin <branch>` and `git pull --rebase`;
  2. looks at what changed since the last look (the hash is kept in .git/sync_watch_<ID>): the operator's inbox `sync/inbox/<ID>.md`
     and the Log rows of SYNC.md addressed to it (its ID, or TUTTI);
  3. if something changed it WAKES the model: writes `.sync_wake` (the text of the news), prints it and runs `--wake-cmd`
     (e.g. a command that starts or messages the agent's session); the model reads, works, and clears `.sync_wake`;
  4. every `--beat` minutes (default 15) it writes the heartbeat itself: `tools/sync_hdr.py <ID> --ack auto` (it keeps state and task as the
     agent last wrote them; only `updated` and `ack` move), commits `sync(<ID>): heartbeat` and pushes.
Without news nothing reaches the model, so a heartbeat costs no tokens. It never changes the state or the task: the agent does that.
`--once` does one round and exits (to test); `--no-push` commits nothing remote (a dry run: prints what it would do)."""
import argparse
import datetime
import os
import re
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def git(*a, check=True):
    r = subprocess.run(["git", "-C", ROOT] + list(a), capture_output=True, text=True, encoding="utf-8", errors="replace")
    if check and r.returncode:
        raise RuntimeError("git %s: %s" % (" ".join(a), r.stderr.strip()))
    return r.stdout.strip()


def news(oid, since, head):
    """text of what is new for `oid` between the commits `since` and `head` (empty if nothing)"""
    out = []
    inbox = "sync/inbox/%s.md" % oid
    d = git("diff", "--unified=0", since, head, "--", inbox, check=False)
    added = [l[1:] for l in d.splitlines() if l.startswith("+") and not l.startswith("+++") and l[1:].strip()]
    if added:
        out.append("INBOX %s:\n%s" % (inbox, "\n".join(added)))
    d = git("diff", "--unified=0", since, head, "--", "SYNC.md", check=False)
    pat = re.compile(r"(?<![A-Za-z])%s(?![A-Za-z])|TUTTI|Tutti|tutti" % re.escape(oid))
    for l in d.splitlines():
        if l.startswith("+|") and re.match(r"\+\|\s*\d{4}-\d\d-\d\d", l) and "| %s " % oid not in l[:70]:
            cols = l.split("|")
            if len(cols) > 4 and pat.search("|".join(cols[4:])):
                out.append("LOG: " + l[1:][:240])
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description="a heartbeat that costs no model tokens")
    ap.add_argument("--id", required=True)
    ap.add_argument("--branch", default="bm-core", help="the sync branch (default bm-core; a project's own: sync/<name>)")
    ap.add_argument("--every", type=float, default=5, help="minutes between two looks (default 5)")
    ap.add_argument("--beat", type=float, default=15, help="minutes between two heartbeats written to the file (default 15)")
    ap.add_argument("--wake-cmd", help="command run when there is news (the text is in .sync_wake and in $SYNC_NEWS)")
    ap.add_argument("--once", action="store_true")
    ap.add_argument("--no-push", action="store_true")
    a = ap.parse_args()
    gd = git("rev-parse", "--git-dir")                      # a clone or a worktree
    seen_file = os.path.join(gd if os.path.isabs(gd) else os.path.join(ROOT, gd), "sync_watch_%s" % a.id)
    last_beat = 0.0
    while True:
        try:
            git("fetch", "origin", a.branch)
            head = git("rev-parse", "origin/" + a.branch)
            since = open(seen_file).read().strip() if os.path.exists(seen_file) else head
            text = news(a.id, since, head) if since != head else ""
            if text:
                stamp = datetime.datetime.now().astimezone().strftime("%H:%M")
                open(os.path.join(ROOT, ".sync_wake"), "w", encoding="utf-8", newline="\n").write("[%s] news for %s since %s\n%s\n" % (stamp, a.id, since[:8], text))
                print("[%s] WAKE %s: %d line(s) of news" % (stamp, a.id, len(text.splitlines())))
                print(text)
                if a.wake_cmd:
                    subprocess.run(a.wake_cmd, shell=True, env=dict(os.environ, SYNC_NEWS=text))
            else:
                print("[%s] nothing new for %s (origin/%s %s)" % (datetime.datetime.now().strftime("%H:%M"), a.id, a.branch, head[:8]))
            open(seen_file, "w").write(head)
            if time.time() - last_beat >= a.beat * 60 or a.once:
                last_beat = time.time()
                if a.no_push:
                    print("  (dry run) would run: sync_hdr.py %s --ack auto; commit; push origin HEAD:%s" % (a.id, a.branch))
                else:
                    git("pull", "--rebase", "origin", a.branch)
                    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "sync_hdr.py"), a.id, "--ack", "auto"], capture_output=True, text=True)
                    print("  " + (r.stdout or r.stderr).strip()[-120:])
                    git("add", "sync/%s.md" % a.id)
                    git("commit", "-m", "sync(%s): heartbeat" % a.id)
                    git("pull", "--rebase", "origin", a.branch)
                    git("push", "origin", "HEAD:" + a.branch)
        except RuntimeError as e:
            print("  ! %s (it will try again)" % e)
        if a.once:
            return 0
        time.sleep(a.every * 60)


if __name__ == "__main__":
    sys.exit(main())
