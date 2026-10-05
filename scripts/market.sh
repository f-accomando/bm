#!/usr/bin/env bash
# The bm Market from WSL (M25), every step of it:
#   scripts/market.sh                  the setup the first time, then the games
#   scripts/market.sh --market DIR     the clone of f-accomando/bm-market
#                                      (default: bm-market next to this repository)
#   scripts/market.sh --dry-run        only says what it would do
#
# Once (later runs find them done and go on):
# 1. the tools: the GitHub CLI (gh), logged in, and git pushing through it
# 2. the market key (scripts/market-key.sh; an existing one is kept) and the
#    secret BM_MARKET_KEY of f-accomando/bm-market, with which its workflow
#    signs the catalog
# 3. keys/market-pub.pem committed and pushed to bm-core: the workflow checks
#    the signature against it, and it goes into the kernel (so the consoles
#    need a new kernel after this)
# 4. GitHub Pages of the market, built by its workflow
# Every time:
# 5. the clone of the market next to this repository, brought up to date
# 6. make market-seed: the project's games built and put in it (a game
#    whose bytes did not change stays as it is)
# 7. commit and push to its main branch: the workflow checks the games,
#    signs the catalog and publishes it; the script waits for it and reads
#    the catalog back from https://f-accomando.github.io/bm-market/
# It asks before each step that changes something outside this PC (the
# secret, a push, Pages). Run again after a failure: what is done is kept.
set -euo pipefail

MREPO=f-accomando/bm-market
SITE=https://f-accomando.github.io/bm-market/
KEY=${BM_MARKET_KEY_FILE:-$HOME/.bm/market-key.pem}
PUB=keys/market-pub.pem
MARKET=
DRY=0

say()  { printf '\n\033[1;96m== %s\033[0m\n' "$*"; }
ok()   { printf '\033[92m%s\033[0m\n' "$*"; }
warn() { printf '\033[93m%s\033[0m\n' "$*"; }
die()  { printf '\033[91m%s\033[0m\n' "$*" >&2; exit 1; }
# what changes something: shown, and with --dry-run not done
run()  { printf '\033[2m$ %s\033[0m\n' "$*"; [ "$DRY" = 1 ] || "$@"; }
ask()  {
    [ "$DRY" = 1 ] && { printf '%s [y/N] y (dry run)\n' "$1"; return 0; }
    local a
    read -r -p "$1 [y/N] " a
    [[ $a == [yYsS]* ]]
}

while [ $# -gt 0 ]; do
    case $1 in
        --market) MARKET=${2:?--market needs a folder}; shift ;;
        --dry-run) DRY=1 ;;
        -h|--help) sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown argument: $1 (scripts/market.sh --help)" ;;
    esac
    shift
done
cd "$(dirname "$0")/.."
[ -n "$MARKET" ] || MARKET=$(dirname "$PWD")/bm-market

# ---------------------------------------------------------------- the repository
say "The repository"
BRANCH=$(git rev-parse --abbrev-ref HEAD)
echo "branch $BRANCH, commit $(git rev-parse --short HEAD)"
if [ "$BRANCH" != bm-core ]; then
    warn "The market's workflow reads bm-core (scripts/mkmarket.py, $PUB): the key must get there."
    ask "This is $BRANCH: go on anyway?" || exit 0
fi
run git pull --ff-only origin "$BRANCH"

# ---------------------------------------------------------------- the tools
say "The tools"
for t in git make openssl python3 curl; do
    command -v "$t" >/dev/null || die "$t is missing (see README_OLD.md, Build)"
done
if ! command -v gh >/dev/null; then
    ask "The GitHub CLI (gh) is missing: install it (sudo apt install gh)?" || die "gh is needed"
    run sudo apt-get update
    run sudo apt-get install -y gh
fi
if [ "$DRY" = 0 ] && ! gh auth status -h github.com >/dev/null 2>&1; then
    echo "gh is not logged in to GitHub: it shows a code, open the address and type it in"
    gh auth login -h github.com -p https -w
fi
# the market's workflow file is pushed too: GitHub wants the scope workflow
if [ "$DRY" = 0 ] && gh auth status -h github.com 2>&1 | grep -q 'Token scopes' \
   && ! gh auth status -h github.com 2>&1 | grep -q "'workflow'"; then
    echo "gh needs the scope workflow (to push the market's .github/workflows): it shows a code again"
    gh auth refresh -h github.com -s workflow
fi
run gh auth setup-git                           # git push with gh's login
ok "gh: $(gh --version 2>/dev/null | head -1)"

# ---------------------------------------------------------------- the key
say "The market key"
# the public key bm-core has now: a real one, or the placeholder of before
COMMITTED=$(git show "HEAD:$PUB" 2>/dev/null || true)
REAL=0
grep -q "BEGIN PUBLIC KEY" <<< "$COMMITTED" && REAL=1
NEW_KEY=0
if [ ! -e "$KEY" ]; then
    [ "$REAL" = 0 ] || die "$PUB has a key already, but $KEY is not on this PC: copy it there
(the PC where scripts/market-key.sh made it has it). A new key would need a new kernel on every console."
    NEW_KEY=1
fi
run scripts/market-key.sh "$KEY" | sed '/^$/,$d'
if [ "$NEW_KEY" = 1 ]; then
    warn "Keep a copy of $KEY somewhere safe (a USB stick, a password manager):"
    warn "without it the catalog cannot be signed for the kernels that have this key."
fi
if [ "$DRY" = 0 ] && [ "$REAL" = 1 ] && [ "$(cat "$PUB")" != "$COMMITTED" ]; then
    warn "$KEY is not the key of the $PUB on $BRANCH: the consoles with that kernel would refuse"
    warn "the catalog signed with it until they get a new kernel."
    ask "Use $KEY anyway (the new $PUB committed below)?" || { git checkout -- "$PUB"; die "stopped"; }
fi

have_secret() { gh secret list -R "$MREPO" 2>/dev/null | grep -q '^BM_MARKET_KEY[[:space:]]'; }
SECRET_SET=0
if [ "$DRY" = 1 ] || [ "$NEW_KEY" = 1 ] || ! have_secret; then
    if ask "Set the secret BM_MARKET_KEY of $MREPO to this key (its workflow signs the catalog with it)?"; then
        printf '\033[2m$ gh secret set BM_MARKET_KEY -R %s < %s\033[0m\n' "$MREPO" "$KEY"
        [ "$DRY" = 1 ] || gh secret set BM_MARKET_KEY -R "$MREPO" < "$KEY"
        SECRET_SET=1
    fi
else
    ok "the secret BM_MARKET_KEY is on $MREPO"
fi
if [ "$DRY" = 0 ] && ! have_secret; then
    die "the secret BM_MARKET_KEY is not on $MREPO: without it the catalog is not published"
fi

KEY_PUSHED=0
if [ -n "$(git status --porcelain -- "$PUB")" ]; then
    git --no-pager diff --stat -- "$PUB" || true
    ask "Commit $PUB and push it to $BRANCH?" || die "the workflow and the kernel need the public key: stopped"
    run git add "$PUB"
    run git commit -m "Market key (scripts/market-key.sh)" -- "$PUB"
    run git push origin "$BRANCH"
    KEY_PUSHED=1
else
    ok "$PUB is the committed one"
fi

# ---------------------------------------------------------------- GitHub Pages
PAGES=0
pages() {                                       # GitHub Pages from the workflow; 1 when on
    local t
    # not on: gh answers 404, and prints GitHub's message where the answer goes
    t=$(gh api "repos/$MREPO/pages" --jq .build_type 2>/dev/null) || t=
    case $t in
        workflow) PAGES=1 ;;
        "")
            ask "Turn on GitHub Pages for $MREPO (built by its workflow)?" || return 0
            if run gh api --silent -X POST "repos/$MREPO/pages" -f build_type=workflow; then PAGES=1; fi ;;
        *)
            ask "GitHub Pages of $MREPO is built from a branch ($t): build it with the workflow?" || return 0
            if run gh api --silent -X PUT "repos/$MREPO/pages" -f build_type=workflow; then PAGES=1; fi ;;
    esac
    [ "$DRY" = 0 ] || PAGES=1
}
say "GitHub Pages"
pages || true
if [ "$PAGES" = 1 ]; then ok "on: $SITE"; else warn "not on yet: again after the games are pushed"; fi

# ---------------------------------------------------------------- the clone
say "The market's clone ($MARKET)"
if [ ! -d "$MARKET/.git" ]; then
    run git clone "https://github.com/$MREPO.git" "$MARKET"
else
    case $(git -C "$MARKET" config --get remote.origin.url) in
        *bm-market*) ;;
        *) die "$MARKET is not a clone of $MREPO (--market DIR names another folder)" ;;
    esac
fi
if [ "$DRY" = 0 ]; then
    if [ -n "$(git -C "$MARKET" status --porcelain)" ]; then
        git -C "$MARKET" status --short | head -20
        warn "$MARKET has changes not committed: from a run of this that stopped, or yours."
        ask "Throw them away (the games are put there again below)?" \
            || die "stopped: commit them or put them aside (git stash) first"
        if git -C "$MARKET" rev-parse -q --verify HEAD >/dev/null; then
            git -C "$MARKET" reset -q --hard
        else
            git -C "$MARKET" rm -rqf --cached . >/dev/null 2>&1 || true
        fi
        git -C "$MARKET" clean -fdq
    fi
    if git -C "$MARKET" ls-remote --exit-code --heads origin main >/dev/null 2>&1; then
        run git -C "$MARKET" checkout -q main
        run git -C "$MARKET" pull -q --ff-only origin main
    else                                        # empty repository: main is its first branch
        run git -C "$MARKET" symbolic-ref HEAD refs/heads/main
    fi
fi

# ---------------------------------------------------------------- the games
say "The games"
run make -j"$(nproc)" market-seed MARKET="$MARKET"
PUSHED=0
if [ "$DRY" = 1 ]; then
    echo "(dry run: the changes would be committed and pushed to $MREPO main)"
elif [ -z "$(git -C "$MARKET" status --porcelain)" ]; then
    ok "the market has these games already: nothing to push"
else
    git -C "$MARKET" status --short | sed 's/^/  /'
    ask "Commit these and push them to $MREPO (main)? Its workflow checks them, signs the catalog and publishes it" \
        || die "stopped: the changes stay in $MARKET"
    run git -C "$MARKET" add -A
    run git -C "$MARKET" commit -q -m "The games of bm ($(git describe --always --tags))"
    run git -C "$MARKET" push -u origin HEAD:main
    PUSHED=1
fi
LATE=0
if [ "$PAGES" = 0 ]; then
    say "GitHub Pages"
    pages || true
    [ "$PAGES" = 1 ] || die "without GitHub Pages the catalog is not online: $MREPO > Settings > Pages > Source: GitHub Actions"
    LATE=1
fi
[ "$DRY" = 1 ] && { ok "(dry run: stopped here)"; exit 0; }

# ---------------------------------------------------------------- the workflow
# a run is needed for the new games, and for a key, a secret or Pages just
# set up (a push run started before Pages was on fails at the deploy)
if [ "$PUSHED" = 1 ] && [ "$LATE" = 0 ]; then
    want=push
elif [ "$PUSHED" = 1 ] || [ "$SECRET_SET" = 1 ] || [ "$KEY_PUSHED" = 1 ] || [ "$LATE" = 1 ] \
     || ! curl -fsS "${SITE}index.txt" 2>/dev/null | grep -q '^bm market'; then
    want=dispatch
else
    want=
fi
if [ -n "$want" ]; then
    say "The market's workflow"
    since=$(date -u -d '-1 min' +%Y-%m-%dT%H:%M:%SZ)
    if [ "$want" = dispatch ]; then
        run gh workflow run market.yml -R "$MREPO" --ref main
    fi
    echo "waiting for the run..."
    RUN=
    for _ in $(seq 30); do
        RUN=$(gh run list -R "$MREPO" -w market.yml -b main -e "$( [ "$want" = push ] && echo push || echo workflow_dispatch)" \
              -L 1 --json databaseId,createdAt --jq ".[] | select(.createdAt >= \"$since\") | .databaseId" 2>/dev/null || true)
        [ -n "$RUN" ] && break
        sleep 5
    done
    [ -n "$RUN" ] || die "no run of the market's workflow yet: https://github.com/$MREPO/actions"
    echo "https://github.com/$MREPO/actions/runs/$RUN"
    if ! gh run watch "$RUN" -R "$MREPO" --exit-status --interval 15; then
        gh run view "$RUN" -R "$MREPO" --log-failed 2>/dev/null | tail -30 || true
        die "the market's workflow failed (above): fix it and run this again"
    fi
fi

# ---------------------------------------------------------------- the catalog
say "The catalog online"
INDEX=
for _ in $(seq 12); do                          # Pages takes a moment to serve the new files
    INDEX=$(curl -fsS "${SITE}index.txt" 2>/dev/null || true)
    grep -q '^bm market' <<< "$INDEX" && break
    sleep 10
done
if ! grep -q '^bm market' <<< "$INDEX"; then
    [ -z "${RUN:-}" ] || gh run view "$RUN" -R "$MREPO" 2>/dev/null | grep -i warning || true
    die "${SITE}index.txt is not there: the run's warnings say why (https://github.com/$MREPO/actions)"
fi
ok "$SITE: $(grep -m1 '^serial' <<< "$INDEX"), $(grep -c '^game ' <<< "$INDEX") games"
grep '^title ' <<< "$INDEX" | sed 's/^title /  /'
echo
if [ "$KEY_PUSHED" = 1 ] || [ "$NEW_KEY" = 1 ]; then
    ok "The consoles check the catalog with the key in their kernel: put a kernel built now on them"
    ok "(easy_install 1 or 2), then the Market tab shows these games."
else
    ok "The consoles see them the next time they open the Market tab."
fi
