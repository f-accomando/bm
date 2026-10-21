#!/usr/bin/env python3
"""Append rows to the SYNC.md Log and write operator inboxes.

The timestamp is always the real local time of the machine (never passed by
hand), formatted YYYY-MM-DDTHH:MM+HH:MM.

Examples:
  python tools/sync_log.py log --who SPV --branch sync/protocollo-ottobre \
      --action RULE --text "Nuova regola" [--commit abc123] [--file SYNC.md] [--push]
  python tools/sync_log.py inbox --id AGY --line "Leggi WO AGY-9" \
      --line "Poi consegna" [--file-dir sync/inbox] [--append] [--push] [--verbose]

  python tools/sync_log.py assign --id NET --wo W1 --wo W2 --branch B [--who SPV] [--push]
      one ASSIGNED Log row plus one inbox line per WO; with --push both files go in one commit.

--quiet (log: opt-in; inbox: default) prints one short line instead of the row or
the inbox text. Warnings and errors always go to stderr. git output is hidden in quiet mode.

Exit codes: 0 ok, 1 git error, 2 bad usage or I/O error.
Without --push git is never touched. With --push: pull --rebase --autostash first,
diff --stat, add (touched files only), commit, push to the current branch;
the pull runs before the file is written.
"""
import argparse
import datetime
import os
import re
import subprocess
import sys

MAX_MSG = 600
MAX_INBOX = 5


def now_stamp():
    s = datetime.datetime.now().astimezone().strftime("%Y-%m-%dT%H:%M%z")
    return s[:-2] + ":" + s[-2:]


def known_actions(text):
    acts = set()
    for ln in text.splitlines():
        cells = [c.strip() for c in ln.split("|")]
        if len(cells) >= 7 and re.match(r"\d{4}-\d\d-\d\dT", cells[1]):
            acts.add(cells[4].strip("`"))
    return acts


def git(args, cwd, quiet=False):
    r = subprocess.run(["git"] + args, cwd=cwd, capture_output=True, text=True)
    if r.stdout and not quiet:
        print(r.stdout, end="")
    if r.returncode != 0:
        sys.stderr.write(r.stderr or "git %s failed\n" % args[0])
        return False
    return True


def current_branch(cwd):
    r = subprocess.run(["git", "rev-parse", "--abbrev-ref", "HEAD"], cwd=cwd,
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr)
        return None
    return r.stdout.strip()


def pull_first(cwd, quiet=False):
    """Bring the branch up to date BEFORE the file is written, so the rebase never has to merge our edit."""
    br = current_branch(cwd)
    if br is None:
        return False
    return git(["pull", "--rebase", "--autostash", "origin", br], cwd, quiet)


def push(paths, action, who, cwd, quiet=False):
    br = current_branch(cwd)
    if br is None:
        return 1
    steps = [
        ["diff", "--stat"],
        ["add", "--"] + paths,
        ["commit", "-m", "sync: %s %s" % (action, who), "--"] + paths,
        ["push", "origin", "HEAD:" + br],
    ]
    for s in steps:
        if not git(s, cwd, quiet):
            return 1
    return 0


def cmd_log(a):
    text = a.text
    if "|" in text or "\n" in text or "\r" in text:
        sys.stderr.write("error: text must not contain '|' or newlines\n")
        return 2
    if a.push and not pull_first(os.path.dirname(os.path.abspath(a.file)), a.quiet):
        return 1
    try:
        with open(a.file, "rb") as f:
            raw = f.read().decode("utf-8")
    except OSError as e:
        sys.stderr.write("error: %s\n" % e)
        return 2
    if a.action not in known_actions(raw):
        sys.stderr.write("warning: action '%s' not found in the existing Log\n" % a.action)
    nl = "\r\n" if "\r\n" in raw else "\n"
    where = "`%s`" % a.branch + ("@%s" % a.commit if a.commit else "")
    row = "| %s | %s | %s | `%s` | %s |" % (now_stamp(), a.who, where, a.action, text)
    if len(row) > MAX_MSG:
        sys.stderr.write("error: the Log row is %d characters (max %d, as checked by sync_check.py): shorten the text by %d\n"
                         % (len(row), MAX_MSG, len(row) - MAX_MSG))
        return 2
    if raw and not raw.endswith("\n"):
        raw += nl
    try:
        with open(a.file, "wb") as f:
            f.write((raw + row + nl).encode("utf-8"))
    except OSError as e:
        sys.stderr.write("error: %s\n" % e)
        return 2
    print("log: riga scritta, %d caratteri" % len(row) if a.quiet else row)
    if a.push:
        d = os.path.dirname(os.path.abspath(a.file))
        return push([os.path.abspath(a.file)], a.action, a.who, d, a.quiet)
    return 0


def cmd_inbox(a):
    quiet = not a.verbose  # quiet is the default for inbox
    lines = a.line
    if len(lines) > MAX_INBOX:
        sys.stderr.write("error: %d lines (max %d)\n" % (len(lines), MAX_INBOX))
        return 2
    for ln in lines:
        if not ln.strip() or "\n" in ln or "\r" in ln:
            sys.stderr.write("error: every line must be non-empty and single-line\n")
            return 2
    path = os.path.abspath(os.path.join(a.file_dir, a.id + ".md"))
    existing = []
    try:
        with open(path, "rb") as f:
            existing = [x for x in f.read().decode("utf-8").splitlines() if x.strip()]
    except OSError:
        pass
    if a.append:
        if len(existing) + len(lines) > MAX_INBOX:
            sys.stderr.write("error: %d existing + %d new lines (max %d): %s untouched\n"
                             % (len(existing), len(lines), MAX_INBOX, path))
            return 2
        lines = existing + lines
    elif existing:
        sys.stderr.write("warning: %s is not empty and --append is absent: overwriting %d line(s)\n"
                         % (path, len(existing)))
    if a.push and os.path.isdir(os.path.dirname(path)) and not pull_first(os.path.dirname(path), quiet):
        return 1
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(("\n".join(lines) + "\n").encode("utf-8"))
    except OSError as e:
        sys.stderr.write("error: %s\n" % e)
        return 2
    if quiet:
        print("inbox %s: %d righe" % (a.id, len(lines)))
    else:
        print("\n".join(lines))
    if a.push:
        return push([path], "inbox", a.id, os.path.dirname(path), quiet)
    return 0


def cmd_assign(a):
    """One Log row ASSIGNED plus one inbox line per WO, all checked before anything is written."""
    wo_re = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
    for w in a.wo:
        if not wo_re.match(w):
            sys.stderr.write("error: WO '%s' does not match [A-Za-z0-9][A-Za-z0-9._-]*\n" % w)
            return 2
    stamp = now_stamp()
    row = "| %s | %s | `%s` | `ASSIGNED` | %s: %s |" % (stamp, a.who, a.branch, a.id, ", ".join(a.wo))
    if len(row) > MAX_MSG:
        sys.stderr.write("error: the Log row is %d characters (max %d): use fewer WOs\n" % (len(row), MAX_MSG))
        return 2
    new = ["ASSIGNED %s @ %s · %s" % (w, a.branch, stamp) for w in a.wo]
    ipath = os.path.abspath(os.path.join(a.file_dir, a.id + ".md"))
    lpath = os.path.abspath(a.file)
    if a.push:
        for d in sorted({os.path.dirname(lpath), os.path.dirname(ipath)}):
            if os.path.isdir(d) and not pull_first(d, True):
                return 1
    existing = []
    try:
        with open(ipath, "rb") as f:
            existing = [x for x in f.read().decode("utf-8").splitlines() if x.strip()]
    except OSError:
        pass
    if len(existing) + len(new) > MAX_INBOX:
        sys.stderr.write("error: %d existing + %d new inbox lines (max %d): nothing written\n"
                         % (len(existing), len(new), MAX_INBOX))
        return 2
    try:
        with open(lpath, "rb") as f:
            raw = f.read().decode("utf-8")
    except OSError as e:
        sys.stderr.write("error: %s\n" % e)
        return 2
    nl = "\r\n" if "\r\n" in raw else "\n"
    if raw and not raw.endswith("\n"):
        raw += nl
    try:
        os.makedirs(os.path.dirname(ipath), exist_ok=True)
        with open(lpath, "wb") as f:
            f.write((raw + row + nl).encode("utf-8"))
        with open(ipath, "wb") as f:
            f.write(("\n".join(existing + new) + "\n").encode("utf-8"))
    except OSError as e:
        sys.stderr.write("error: %s\n" % e)
        return 2
    print("assign %s: %d WO, log + inbox" % (a.id, len(a.wo)))
    if a.push:
        return push([lpath, ipath], "ASSIGNED", a.who, os.path.dirname(lpath), True)
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    lg = sub.add_parser("log")
    lg.add_argument("--who", required=True)
    lg.add_argument("--branch", required=True)
    lg.add_argument("--commit")
    lg.add_argument("--action", required=True)
    lg.add_argument("--text", required=True)
    lg.add_argument("--file", default="SYNC.md")
    lg.add_argument("--push", action="store_true")
    lg.add_argument("--quiet", action="store_true", help="print one line: log: riga scritta, K caratteri")
    ib = sub.add_parser("inbox")
    ib.add_argument("--id", required=True)
    ib.add_argument("--line", action="append", required=True)
    ib.add_argument("--file-dir", default="sync/inbox")
    ib.add_argument("--append", action="store_true", help="add --line values after the existing lines (max 5 in all)")
    ib.add_argument("--push", action="store_true")
    ib.add_argument("--quiet", action="store_true", help="default: print inbox ID: N righe only")
    ib.add_argument("--verbose", action="store_true", help="print the inbox text after writing")
    asg = sub.add_parser("assign")
    asg.add_argument("--id", required=True)
    asg.add_argument("--wo", action="append", required=True)
    asg.add_argument("--branch", required=True)
    asg.add_argument("--who", default="SPV")
    asg.add_argument("--file", default="SYNC.md")
    asg.add_argument("--file-dir", default="sync/inbox")
    asg.add_argument("--push", action="store_true")
    try:
        a = p.parse_args(argv)
    except SystemExit as e:
        return 0 if e.code in (0, None) else 2
    if a.cmd == "assign":
        return cmd_assign(a)
    return cmd_log(a) if a.cmd == "log" else cmd_inbox(a)


if __name__ == "__main__":
    sys.exit(main())
