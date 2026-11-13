#!/usr/bin/env python3
"""Check that a branch changed only the files its work order names.

  wo_scope.py --order ORDER.md [--branch BRANCH] [--base REF] [--allow GLOB ...]
  wo_scope.py --files PATH [PATH ...] [--branch BRANCH] [--base REF] [--allow GLOB ...]
  wo_scope.py --order ORDER.md --changed PATH [PATH ...]      (no git needed)

The scope is the set of paths in backticks in the order. If the order has a
heading that contains "files" (for example "Files to touch"), only the lines of
that section count; otherwise the whole order counts. A scope entry matches:
  - a file path exactly,
  - a directory (entry ends with "/"): every file below it,
  - a glob (entry has * ? or [): fnmatch on the whole path.
--allow adds globs that every order may touch (for example docs/progress/*).

The changed files are `git diff --name-only BASE...BRANCH` (default BASE =
origin/bm-core, BRANCH = HEAD), or the paths given with --changed.

Output: one line per changed file outside the scope ("out of scope: PATH"), one
line per scope entry that no changed file used ("unused: ENTRY", information
only), and a final "scope ok" or "scope violated: N file(s)".
Exit codes: 0 scope ok, 1 scope violated, 2 bad usage or git error."""
import argparse, fnmatch, re, subprocess, sys

TICK_RE = re.compile(r"`([^`\s]+)`")
PATHLIKE_RE = re.compile(r"[\w.\-*?\[\]]+(?:/[\w.\-*?\[\]]+)*/?")


def _is_path(word):
    """A backtick word is a path when it has a slash or a file extension."""
    if not PATHLIKE_RE.fullmatch(word):
        return False
    return "/" in word or bool(re.search(r"\.[A-Za-z0-9]{1,5}$", word))


def order_scope(text):
    """Return the scope entries of a work order text."""
    lines = text.splitlines()
    chosen, inside, found = [], False, False
    for line in lines:
        stripped = line.lstrip()
        if stripped.startswith(("#", "**")):  # a heading line
            inside = "files" in stripped.lower()
            found = found or inside
        if inside:
            chosen.append(line)
    if not found:  # no files heading: the "Files" bullet or the whole text
        chosen = [l for l in lines if re.match(r"\s*[-*]?\s*(\*\*)?files", l, re.I)] or lines
    entries = []
    for word in TICK_RE.findall("\n".join(chosen)):
        word = word.replace("\\", "/")
        if _is_path(word) and word not in entries:
            entries.append(word)
    return entries


def matches(entry, path):
    if entry.endswith("/"):
        return path.startswith(entry)
    if any(c in entry for c in "*?["):
        return fnmatch.fnmatchcase(path, entry)
    return path == entry


def check(scope, changed, allow=()):
    """Return (out_of_scope, unused) lists."""
    out = [p for p in changed
           if not any(matches(e, p) for e in scope) and not any(matches(a, p) for a in allow)]
    unused = [e for e in scope if not any(matches(e, p) for p in changed)]
    return out, unused


def git_changed(base, branch, repo):
    cmd = ["git", "-C", repo, "diff", "--name-only", f"{base}...{branch}"]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode:
        raise RuntimeError(res.stderr.strip() or "git diff failed")
    return [l.strip() for l in res.stdout.splitlines() if l.strip()]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--order", help="work order file (Markdown or text)")
    ap.add_argument("--files", nargs="+", help="scope paths given directly")
    ap.add_argument("--changed", nargs="+", help="changed paths given directly (no git)")
    ap.add_argument("--branch", default="HEAD")
    ap.add_argument("--base", default="origin/bm-core")
    ap.add_argument("--repo", default=".")
    ap.add_argument("--allow", nargs="*", default=[], help="globs every order may touch")
    try:
        args = ap.parse_args(argv)
    except SystemExit as exc:
        return 2 if exc.code else 0
    if bool(args.order) == bool(args.files):
        print("wo_scope: give exactly one of --order or --files", file=sys.stderr)
        return 2
    if args.order:
        try:
            with open(args.order, encoding="utf-8-sig") as fh:
                scope = order_scope(fh.read())
        except (OSError, UnicodeDecodeError) as exc:
            print(f"wo_scope: cannot read order: {exc}", file=sys.stderr)
            return 2
        if not scope:
            print("wo_scope: the order names no file in backticks", file=sys.stderr)
            return 2
    else:
        scope = [f.replace("\\", "/") for f in args.files]
    try:
        changed = ([c.replace("\\", "/") for c in args.changed] if args.changed
                   else git_changed(args.base, args.branch, args.repo))
    except (RuntimeError, OSError) as exc:
        print(f"wo_scope: {exc}", file=sys.stderr)
        return 2
    out, unused = check(scope, changed, args.allow)
    for p in out:
        print(f"out of scope: {p}")
    for e in unused:
        print(f"unused: {e}")
    if out:
        print(f"scope violated: {len(out)} file(s)")
        return 1
    print(f"scope ok ({len(changed)} changed, {len(scope)} in scope)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
