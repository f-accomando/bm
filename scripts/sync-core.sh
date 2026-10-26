#!/usr/bin/env bash
# One call for the state of bm-core: fetch, ahead/behind, clean tree, and the fast-forward when it is safe.
#   scripts/sync-core.sh          report only
#   scripts/sync-core.sh --ff     also fast-forward the current branch to origin/bm-core when it is bm-core and clean
set -u
cd "$(dirname "$0")/.." || exit 2
git fetch -q origin || { echo "bm-core: fetch failed"; exit 1; }
branch=$(git rev-parse --abbrev-ref HEAD)
read -r ahead behind < <(git rev-list --left-right --count "HEAD...origin/bm-core")
dirty=clean
[ -n "$(git status --porcelain --untracked-files=no)" ] && dirty=dirty
msg="$branch: ahead $ahead, behind $behind of origin/bm-core, $dirty"
if [ "${1:-}" = "--ff" ] && [ "$branch" = "bm-core" ] && [ "$dirty" = clean ] && [ "$behind" -gt 0 ] && [ "$ahead" -eq 0 ]; then
    git merge -q --ff-only origin/bm-core && msg="$msg -> fast-forwarded"
fi
echo "$msg"
