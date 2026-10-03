#!/usr/bin/env bash
# A bm release from WSL (M19), every step of it:
#   scripts/release.sh v0.1.0            the whole procedure
#   scripts/release.sh v0.1.0 --sd E     the SD card is drive E: (default D:)
#   scripts/release.sh v0.2.0 --no-sd    a later release: the Pi updates itself
#   scripts/release.sh v0.1.0 --dry-run  only says what it would do
#
# 1. the tools: the GitHub CLI (gh), logged in, and git pushing through it
# 2. the release key (scripts/release-key.sh; an existing one is kept) and
#    the repository's secret BM_RELEASE_KEY, with which the CI signs
# 3. keys/release-pub.pem committed and pushed (it goes into the kernel)
# 4. a kernel with that key on the SD card (make install): before the tag,
#    because a kernel built on the tag would be the release itself
# 5. the tag, pushed: the CI tests it and publishes the release; the script
#    waits for it and lists the release's files
# It asks before each step that changes something outside this PC (the
# secret, a push, the tag) and before writing on the SD card. Run again
# after a failure: what is done already is kept or done again harmlessly.
set -euo pipefail

REPO=f-accomando/bm
KEY=${BM_RELEASE_KEY_FILE:-$HOME/.bm/release-key.pem}
PUB=keys/release-pub.pem
VERSION=
DRIVE=D
SD=
SD_STEP=1
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
        --sd) DRIVE=${2:?--sd needs a drive letter}; shift ;;
        --sd-path) SD=${2:?}; shift ;;          # a folder instead of a drive (tests)
        --no-sd) SD_STEP=0 ;;
        --dry-run) DRY=1 ;;
        -h|--help) sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        v*) VERSION=$1 ;;
        *) die "unknown argument: $1 (scripts/release.sh --help)" ;;
    esac
    shift
done
[[ $VERSION =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "the version is vX.Y.Z, e.g.: scripts/release.sh v0.1.0"
DRIVE=$(printf '%s' "$DRIVE" | tr -d ':' | tr 'a-z' 'A-Z')
[ -n "$SD" ] || SD=/mnt/$(printf '%s' "$DRIVE" | tr 'A-Z' 'a-z')
cd "$(dirname "$0")/.."

# ---------------------------------------------------------------- the repository
say "The repository"
[ -f src/kernel/update.c ] || die "this branch has no update from the Pi (src/kernel/update.c): use bm-store"
BRANCH=$(git rev-parse --abbrev-ref HEAD)
echo "branch $BRANCH, commit $(git rev-parse --short HEAD)"
if [ -n "$(git status --porcelain --untracked-files=no -- . ":!$PUB")" ]; then
    git status --short --untracked-files=no
    die "there are changes not committed: commit or put them aside (git stash) first"
fi
run git pull --ff-only origin "$BRANCH"
if git rev-parse -q --verify "refs/tags/$VERSION" >/dev/null || [ -n "$(git ls-remote --tags origin "$VERSION")" ]; then
    die "$VERSION exists already: choose the next one (e.g. ${VERSION%.*}.$((${VERSION##*.} + 1)))"
fi
LAST=$(git ls-remote --tags --refs origin 'v*' | sed 's|.*refs/tags/||' | sort -V | tail -1)
[ -z "$LAST" ] || echo "the latest release tag: $LAST"

# ---------------------------------------------------------------- the tools
say "The tools"
for t in git make openssl arm-none-eabi-gcc python3; do
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
run gh auth setup-git                           # git push with gh's login
ok "gh: $(gh --version 2>/dev/null | head -1)"

# ---------------------------------------------------------------- the key
say "The release key"
NEW_KEY=0
[ -e "$KEY" ] || NEW_KEY=1
run scripts/release-key.sh "$KEY" | sed '/^$/,$d'
if [ "$NEW_KEY" = 1 ]; then
    warn "Keep a copy of $KEY somewhere safe (a USB stick, a password manager):"
    warn "without it no release can be signed for the kernels that have this key."
fi
if ask "Set the secret BM_RELEASE_KEY of $REPO to this key (the CI signs with it)?"; then
    printf '\033[2m$ gh secret set BM_RELEASE_KEY -R %s < %s\033[0m\n' "$REPO" "$KEY"
    [ "$DRY" = 1 ] || gh secret set BM_RELEASE_KEY -R "$REPO" < "$KEY"
fi
if [ "$DRY" = 0 ] && ! gh secret list -R "$REPO" 2>/dev/null | grep -q '^BM_RELEASE_KEY'; then
    die "the secret BM_RELEASE_KEY is not on $REPO: without it the CI makes no release"
fi

if [ -n "$(git status --porcelain -- "$PUB")" ]; then
    git --no-pager diff --stat -- "$PUB" || true
    ask "Commit $PUB and push it to $BRANCH?" || die "the kernel must have the public key: stopped"
    run git add "$PUB"
    run git commit -m "Release key (scripts/release-key.sh)"
    run git push origin "$BRANCH"
else
    ok "$PUB is the committed one"
fi

# ---------------------------------------------------------------- the SD card
if [ "$SD_STEP" = 1 ]; then
    say "The kernel on the SD card ($SD)"
    [ -f firmware/start.elf ] || run make firmware
    run make -j"$(nproc)"
    BODY=$(sed -n 2p "$PUB")
    [ "$DRY" = 1 ] || grep -qF "$BODY" build/kernel.img || die "build/kernel.img does not have $PUB"
    if [ "$SD" = "/mnt/${DRIVE,,}" ] && ! mountpoint -q "$SD"; then
        ask "Mount the SD card ($DRIVE:) on $SD (sudo)?" || die "the SD card is needed (or --no-sd)"
        run sudo mkdir -p "$SD"
        run sudo mount -t drvfs "$DRIVE:" "$SD"
    fi
    if [ "$DRY" = 0 ] && [ ! -e "$SD/config.txt" ] && [ ! -e "$SD/bootcode.bin" ]; then
        ls "$SD" | head -10
        ask "$SD does not look like bm's SD card (no config.txt): write on it anyway?" || die "stopped"
    fi
    ask "Install this kernel ($(git describe --always --dirty)) and the games on $SD?" || die "stopped"
    run make install SD="$SD"
    run sync
    ok "Done: eject the card in Windows, put it in the Pi and start it."
    ok "Settings > System shows version $(git describe --always --dirty): a build of the sources,"
    ok "which the release may replace."
fi

# ---------------------------------------------------------------- the release
say "The release $VERSION"
ask "Create the tag $VERSION on $(git rev-parse --short HEAD) and push it? The CI tests it (about half an hour) and publishes the release" \
    || { echo "the tag later: git tag $VERSION && git push origin $VERSION"; exit 0; }
run git tag -a "$VERSION" -m "bm $VERSION"
run git push origin "$VERSION"
[ "$DRY" = 1 ] && { ok "(dry run: stopped here)"; exit 0; }

echo "waiting for the CI run of $VERSION..."
RUN=
for _ in $(seq 30); do
    RUN=$(gh api "repos/$REPO/actions/runs?branch=$VERSION&event=push&per_page=1" --jq '.workflow_runs[0].id // empty' 2>/dev/null || true)
    [ -n "$RUN" ] && break
    sleep 5
done
[ -n "$RUN" ] || die "no CI run for $VERSION yet: https://github.com/$REPO/actions"
echo "https://github.com/$REPO/actions/runs/$RUN"
if ! gh run watch "$RUN" -R "$REPO" --exit-status --interval 30; then
    gh run view "$RUN" -R "$REPO" --log-failed 2>/dev/null | tail -40 || true
    die "the CI failed: no release. Fix it, then: git tag -d $VERSION && git push origin :$VERSION, and run again"
fi
gh release view "$VERSION" -R "$REPO"
ok ""
ok "Released: https://github.com/$REPO/releases/tag/$VERSION"
ok "On the Pi, with the WiFi on: Settings (4) > System > Check for updates"
ok "  ($VERSION: the files that change), then Install the update and confirm:"
ok "  it restarts, and Settings > System shows $VERSION."
