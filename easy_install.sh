#!/usr/bin/env bash
# easy_install.sh: bm on the SD card, from WSL.   ./easy_install.sh
#
# The first time (on this PC) it remembers this repository's folder and the
# SD card's drive letter in .easy_install.conf (in .gitignore: it stays on
# this PC), installs the packages the build needs and fetches the Pi's
# firmware (make firmware, into firmware/); the file keeps what it did.
# Every time it shows the branch you are on, then a menu:
#   1  [NET] update kernel  to a console on the network, without the card
#                           (tools/bm_net.py --kernel): a saved profile (name,
#                           IP, the 6-digit console code, the board: Pi, Pi
#                           Zero 2 W or RGB30) or a new one
#   2   [SD] update kernel  kernel.img on the card (the old one in bm/backup)
#   3   [SD] full install   make install: kernel, boot files, games, bm/
#   4   [SD] disk image     make image (dist/bm.img), then the card erased and
#                           formatted (FAT32, the whole card up to 31 GB) with
#                           the image's files on it; settings, saves and your
#                           own games are kept, unless you say no
#   5  [NET] send a file    a game (.bm, into /carts: the menu shows it at once),
#                           a resource (.bmm .bmi .bms ..., into /bm/lib) or any
#                           file to a console's SD card (tools/bm_net.py --send)
#   6  [NET] monitor        a console's monitor on this PC (tools/bm_net.py): its
#                           keys and commands (the console says what takes them:
#                           the menu, without echo, or the monitor; in the menu a
#                           monitor line starts with ':'); Ctrl-Q leaves
#   7  [NET] config         a console's bm/config.txt (bm_net.py --config): the
#                           settings shown (tokens and passwords hidden), keys set
#                           (github_token for the reports, WiFi...) or removed
#   8   [SD] config         the same on the card in the reader
#   r  release              a new version on GitHub (scripts/release.sh --no-sd:
#                           the tag, the CI tests it and publishes it); the
#                           consoles take it from Settings > Updates
#   o  older release        an older release on the card (also: rollback [vX.Y.Z]):
#                           the signed manifest checked with keys/release-pub.pem,
#                           every file downloaded and checked (size, SHA-256) before
#                           the card is touched, the old kernels in bm/backup, the
#                           card's own kernel last
#   m  market               the project's games in the bm Market (scripts/market.sh):
#                           the first time also the key, the secret, the public
#                           key on bm-core and GitHub Pages; the market's clone
#                           is bm-market next to this repository
#   b  branch              change it, or bring it up to date (git pull)
#   p  paths               the repository's folder, the card's drive letter
# The same as an argument: ./easy_install.sh kernel | install | image | net [profile]
# | bench [FLAGS] [profile] (the Overbit benchmark on a console, see below) | send FILE [profile] | monitor [profile] | line "gpu; b3d; send" [profile] (a monitor
# line run, its output shown: bm_net.py --line) | config [profile] | config-sd | release [vX.Y.Z] | rollback [vX.Y.Z]
# | market.
# bench: Overbit's benchmark (about a minute) on a console, its report sent to the reports'
# branch: ./easy_install.sh bench              the calibration (up the scale from the lightest
#                                              step, ARM and GPU, at most ~60 s: the heaviest at
#                                              60 fps, else at 30, saved with its renderer)
#                 ./easy_install.sh bench "res:1080+640/gpu:vs+gpu/q:4+3/secs:4/ring:1"
#                                              every combination (res: 1080 640 360 720; gpu: arm gpu
#                                              aa gq vs1 vs q; q 0..4; secs, warm, ring, save:1, stay:1)
# It is "set overbit_bench=FLAGS; play overbit; send" as a monitor line (bm_net.py --line).
# sudo is asked for when needed (packages, mounting the card); the card is
# mounted, synced and unmounted (and ejected) by the script. At the end:
# the kernel the card had -> the one it has now.
set -euo pipefail

SCRIPT=$(readlink -f "${BASH_SOURCE[0]}")
HERE=$(dirname "$SCRIPT")
CONF=$HERE/.easy_install.conf
BACKUPS=$HOME/.bm/sd-backup

say()  { printf '\n\033[1;96m== %s\033[0m\n' "$*"; }
ok()   { printf '\033[92m%s\033[0m\n' "$*"; }
warn() { printf '\033[93m%s\033[0m\n' "$*"; }
die()  { printf '\033[91m%s\033[0m\n' "$*" >&2; exit 1; }
ask()  {                                        # ask "question" [default y|n]
    local a def=${2:-n}
    read -r -p "$1 $([ "$def" = y ] && echo '[Y/n]' || echo '[y/N]') " a || a=
    a=${a:-$def}
    [[ $a == [yYsS]* ]]
}
now()  { date '+%Y-%m-%d %H:%M'; }

# ---------------------------------------------------------------- settings
REPO='' DRIVE='' CREATED='' ALIAS='' PACKAGES_CHECKED='' FIRMWARE='' FIRMWARE_FETCHED='' LAST_RUN=''
PROFILES=()                                     # name|IP|code|board|last time
# shellcheck disable=SC1090
[ -f "$CONF" ] && . "$CONF"
# what the build needs (apt), after the file: this list wins over the one saved
PACKAGES="git make build-essential gcc-arm-none-eabi libnewlib-arm-none-eabi binutils-arm-none-eabi \
binutils python3 python3-numpy python3-pil curl dosfstools mtools openssl"

save_conf() {
    {
        echo "# easy_install.sh: this PC's settings and what it did (not in git: .gitignore)"
        printf 'REPO=%q\n' "$REPO"                   # this repository on the PC
        printf 'DRIVE=%q\n' "$DRIVE"                 # the SD card's letter in Windows
        printf 'CREATED=%q\n' "$CREATED"
        printf 'ALIAS=%q\n' "$ALIAS"                 # the command easy_install in ~/.bashrc
        printf 'PACKAGES=%q\n' "$PACKAGES"           # what the build needs (apt)
        printf 'PACKAGES_CHECKED=%q\n' "$PACKAGES_CHECKED"
        printf 'FIRMWARE=%q\n' "$FIRMWARE"           # make firmware: the Pi's boot files, WiFi/BT
        printf 'FIRMWARE_FETCHED=%q\n' "$FIRMWARE_FETCHED"
        printf 'LAST_RUN=%q\n' "$LAST_RUN"
        printf 'PROFILES=('                         # the consoles on the network: name|IP|code|board|last
        local p
        for p in "${PROFILES[@]}"; do printf '\n    %q' "$p"; done
        printf '\n)\n'
    } > "$CONF"
    chmod 600 "$CONF"                           # it has the consoles' codes
}

valid_drive() { [[ $1 =~ ^[A-Za-z]$ ]] && [[ ${1^^} != C ]]; }

set_drive() {
    local d
    while :; do
        read -r -p "The SD card's drive letter in Windows (Explorer shows it, e.g. D:) [${DRIVE:-D}]: " d || d=
        d=${d:-${DRIVE:-D}}; d=${d%:}
        valid_drive "$d" && break
        warn "one letter, not C (that is Windows' own disk)"
    done
    DRIVE=${d^^}
}

set_alias() {                                   # the command easy_install in every WSL terminal
    local rc=$HOME/.bashrc
    touch "$rc"
    sed -i '/# bm easy_install$/d' "$rc"
    printf "alias easy_install='%s' # bm easy_install\n" "$REPO/easy_install.sh" >> "$rc"
    ALIAS=yes
    ok "easy_install now starts this script from any folder (in new terminals, or after: source ~/.bashrc)"
}

first_time() {
    say "First time on this PC"
    git -C "$HERE" rev-parse --show-toplevel >/dev/null 2>&1 || die "$HERE is not a git repository"
    REPO=$(git -C "$HERE" rev-parse --show-toplevel)
    echo "repository: $REPO"
    set_drive
    CREATED=$(now)
    if ask "Add the command 'easy_install' to WSL, to start this from any folder?" y; then
        set_alias
    else
        ALIAS=no
    fi
    save_conf
    ok "saved in $CONF"
}

[ -n "$REPO" ] && [ -n "$DRIVE" ] || first_time
[ -d "$REPO/.git" ] || die "$REPO is not the repository any more: delete $CONF and run again"
cd "$REPO"

# ---------------------------------------------------------------- packages and firmware
packages() {
    command -v dpkg-query >/dev/null || { warn "no dpkg: the packages are not checked"; return; }
    local missing=() p
    for p in $PACKAGES; do
        dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q 'install ok installed' || missing+=("$p")
    done
    if [ ${#missing[@]} -gt 0 ]; then
        say "Packages the build needs"
        echo "missing: ${missing[*]}"
        ask "Install them (sudo apt-get install)?" y || die "they are needed"
        sudo apt-get update
        sudo apt-get install -y "${missing[@]}"
        PACKAGES_CHECKED="$(now) (installed: ${missing[*]})"
        save_conf
    elif [ -z "$PACKAGES_CHECKED" ]; then
        PACKAGES_CHECKED="$(now) (all there)"
        save_conf
    fi
}

firmware() {                                    # the Pi's boot files and the radio chips' firmware
    local fw=$REPO/firmware f need=0
    for f in bootcode.bin start.elf fixup.dat BCM43430A1.hcd brcmfmac43430-sdio.bin brcmfmac43430-sdio.txt; do
        [ -s "$fw/$f" ] || need=1
    done
    if [ $need = 1 ]; then
        say "The Pi's firmware (make firmware, into $fw)"
        make firmware
        [ -s "$fw/start.elf" ] || die "make firmware did not fetch start.elf: is the network on?"
        FIRMWARE=$fw
        FIRMWARE_FETCHED=$(now)
        save_conf
    fi
}

# ---------------------------------------------------------------- Windows
PS=$(command -v powershell.exe || echo /mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe)
win() { "$PS" -NoProfile -NonInteractive -Command "$1" 2>/dev/null | tr -d '\r'; }

eject() {
    [ -n "${EASY_SD_DIR:-}" ] && return 0
    win "(New-Object -ComObject Shell.Application).Namespace(17).ParseName('${DRIVE}:').InvokeVerb('Eject')" \
        >/dev/null || true
}

# ---------------------------------------------------------------- the card
SD=${EASY_SD_DIR:-/mnt/${DRIVE,,}}              # EASY_SD_DIR: a folder instead of the card (tests)

mount_sd() {
    [ -n "${EASY_SD_DIR:-}" ] && return 0
    if mountpoint -q "$SD"; then
        if touch "$SD/.bm-easy" 2>/dev/null; then
            rm -f "$SD/.bm-easy"
            return 0
        fi
        sudo umount "$SD"                       # mounted by WSL without write access: again
    fi
    sudo mkdir -p "$SD"
    for _ in $(seq 20); do
        if sudo mount -t drvfs "$DRIVE:" "$SD" -o "uid=$(id -u),gid=$(id -g)" 2>/dev/null; then
            ok "SD card $DRIVE: mounted on $SD"
            return 0
        fi
        sleep 1
    done
    die "the SD card ($DRIVE:) is not there: is it in the reader, and does Windows show it as $DRIVE:? (p to change the letter)"
}

unmount_sd() {
    sync
    [ -n "${EASY_SD_DIR:-}" ] && return 0
    if mountpoint -q "$SD"; then
        sudo umount "$SD" || { sleep 2; sudo umount "$SD"; }
    fi
}

kernel_version() {                              # the version in a kernel.img
    local f=$1 v
    [ -f "$f" ] || { echo "none"; return; }
    v=$(grep -aom1 'bmVER=[[:graph:]]*' "$f" | head -1 | cut -d= -f2) || true
    if [ -z "$v" ]; then                        # a kernel from before the mark: its git describe
        v=$(strings -n 7 "$f" | grep -E '^(v[0-9]+\.[0-9]+\.[0-9]+(-[0-9]+-g[0-9a-f]+)?(-dirty)?|[0-9a-f]{7,12}(-dirty)?)$' \
            | grep -E '^v|[a-f]' | sort -r | head -1) || true
    fi
    echo "${v:-unknown}"
}

rgb30_card() {                                  # the RGB30's card: U-Boot reads extlinux, starts kernel8.img
    [ -e "$SD/extlinux/extlinux.conf" ] || [ -e "$SD/kernel8.img" ]
}

build_rgb30() {                                 # the RGB30's kernel8.img (its own compiler)
    command -v aarch64-linux-gnu-gcc >/dev/null || {
        ask "The RGB30's compiler is missing: install it (gcc-aarch64-linux-gnu, picolibc)?" y || die "it is needed"
        sudo apt-get install -y gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu picolibc-aarch64-linux-gnu
    }
    make TARGET=rgb30 -j"$(nproc)" build/rgb30/kernel8.img
}

looks_like_bm() {                               # a card with bm (or empty)
    [ -e "$SD/config.txt" ] || [ -e "$SD/bootcode.bin" ] || [ -z "$(ls -A "$SD" 2>/dev/null)" ]
}

# ---------------------------------------------------------------- git
BRANCH=
branch_line() {                                 # BRANCH must be set
    local s
    s="$BRANCH  $(git rev-parse --short HEAD)"
    timeout 20 git fetch -q origin "$BRANCH" 2>/dev/null || true
    if git rev-parse -q --verify '@{u}' >/dev/null 2>&1; then
        local ab behind ahead
        ab=$(git rev-list --left-right --count 'HEAD...@{u}')
        ahead=${ab%%[[:space:]]*}; behind=${ab##*[[:space:]]}
        if [ "$behind" = 0 ] && [ "$ahead" = 0 ]; then s+="  (up to date with GitHub)"
        else s+="  ($behind behind, $ahead ahead of GitHub)"; fi
    else
        s+="  (not on GitHub)"
    fi
    local dirty
    dirty=$(git status --porcelain --untracked-files=no | wc -l)
    [ "$dirty" = 0 ] || s+="  $dirty files changed"
    echo "$s"
}

up_to_date() {                                  # before a build: the branch as on GitHub
    git rev-parse -q --verify '@{u}' >/dev/null 2>&1 || return 0
    local behind
    behind=$(git rev-list --count 'HEAD..@{u}')
    if [ "$behind" != 0 ] && ask "$BRANCH is $behind commits behind GitHub: update it first (git pull)?" y; then
        git pull --ff-only || die "git pull failed: local changes in the way? (git status)"
    fi
}

change_branch() {
    say "Branch"
    local was
    was=$(git hash-object "$SCRIPT" 2>/dev/null)
    # every branch on GitHub (whatever the clone's refspec), the ones deleted
    # there gone here too: without --prune they stayed in the list for ever
    git fetch -q --prune origin '+refs/heads/*:refs/remotes/origin/*' 2>/dev/null ||
        warn "GitHub not reached: the list may be old"
    echo "on GitHub, the most recent first:"
    local names=() name date subject b
    while IFS='|' read -r name date subject; do
        [ "$name" != HEAD ] || continue
        names+=("$name")
        printf '  %2d %s %-26s %s  %.44s\n' "${#names[@]}" "$([ "$name" = "$BRANCH" ] && echo '*' || echo ' ')" \
            "$name" "$date" "$subject"
        [ "${#names[@]}" -lt 15 ] || break
    done < <(git for-each-ref --sort=-committerdate \
                 --format='%(refname:lstrip=3)|%(committerdate:short)|%(subject)' refs/remotes/origin)
    read -r -p "Branch to use (number or name; Enter: stay on $BRANCH and update it): " b || b=
    if [[ $b =~ ^[0-9]+$ ]] && [ "$b" -ge 1 ] && [ "$b" -le "${#names[@]}" ]; then
        b=${names[$((b - 1))]}
    fi
    b=${b:-$BRANCH}
    if [ "$b" != "$BRANCH" ]; then
        [ -z "$(git status --porcelain --untracked-files=no)" ] ||
            die "there are changes not committed: commit or git stash them first"
        git checkout -q "$b" || { warn "could not move to $b"; return; }
    fi
    # from GitHub's branch of the same name, also for a local branch made
    # without tracking (a plain git pull refused it)
    git branch -q --set-upstream-to="origin/$b" 2>/dev/null || true
    git pull -q --ff-only origin "$b" || warn "git pull failed: $b is not where GitHub has it"
    # this script came with the branch: the new one at once, not the copy in memory
    if [ "$(git hash-object "$SCRIPT" 2>/dev/null)" != "$was" ]; then
        ok "easy_install.sh changed with $b: starting the new one"
        exec bash "$SCRIPT"
    fi
}

change_paths() {
    say "Paths"
    local r
    read -r -p "The repository's folder [$REPO]: " r || r=
    r=${r:-$REPO}; r=${r%/}
    if [ "$r" != "$REPO" ]; then
        [ -d "$r/.git" ] && [ -f "$r/Makefile" ] || die "$r is not bm's repository"
        REPO=$r
        [ "$ALIAS" = yes ] && [ -f "$REPO/easy_install.sh" ] && set_alias
    fi
    set_drive
    save_conf
    ok "saved: repository $REPO, SD card $DRIVE:"
    exec "$SCRIPT"
}

# ---------------------------------------------------------------- consoles on the network
# A profile: name|IP|code|board|last. The code is the console's 6 digits
# (the Pi: Settings > WiFi and network > Console password, where the IP is
# too; the RGB30: System > WiFi once connected); the board says which
# kernel it takes.
BOARDS=(pi zero2 rgb30)
board_name() {
    case $1 in
        pi) echo "Pi Zero / Zero W / Pi 1 (kernel.img)" ;;
        zero2) echo "Pi Zero 2 W (kernel7.img)" ;;
        rgb30) echo "PowKiddy RGB30 (kernel8.img)" ;;
        *) echo "$1" ;;
    esac
}

net_version() {                                 # the version a console on the network runs, or nothing
    python3 -c 'import sys; sys.path.insert(0, "tools"); import bm_net; print(bm_net.console_version(sys.argv[1]) or "")' \
        "$1" 2>/dev/null || true
}

profile_field() { local IFS='|'; local f; read -r -a f <<< "$1"; echo "${f[$2]:-}"; }

list_profiles() {
    local i p
    for i in "${!PROFILES[@]}"; do
        p=${PROFILES[$i]}
        printf '  %d  %-12s %-15s %s\n' $((i + 1)) "$(profile_field "$p" 0)" "$(profile_field "$p" 1)" \
            "$(board_name "$(profile_field "$p" 3)")"
        [ -z "$(profile_field "$p" 4)" ] || printf '       last time: %s\n' "$(profile_field "$p" 4)"
    done
}

new_profile() {                                 # sets PICK to the new profile's index
    say "New console"
    echo "The console's IP and its Console password (6 digits): on the Pi in Settings > WiFi and"
    echo "network; on the RGB30 in System > WiFi once connected (\"net: console on port 3333, password ...\")."
    local name ip code b i
    while :; do
        read -r -p "A name for it (e.g. salotto, pi1): " name || die "stopped"
        [[ $name =~ ^[A-Za-z0-9._-]{1,16}$ ]] || { warn "letters, digits, . _ - (at most 16)"; continue; }
        for i in "${!PROFILES[@]}"; do
            [ "$(profile_field "${PROFILES[$i]}" 0)" != "$name" ] || { warn "$name exists already"; name=; break; }
        done
        [ -n "$name" ] && break
    done
    while :; do
        read -r -p "Its IP (e.g. 192.168.1.108): " ip || die "stopped"
        [[ $ip =~ ^[0-9]{1,3}(\.[0-9]{1,3}){3}$ || $ip =~ ^[A-Za-z0-9.-]+$ ]] && break
        warn "an IP like 192.168.1.108 (or a host name)"
    done
    while :; do
        read -r -p "Its Console password (6 digits): " code || die "stopped"
        [[ $code =~ ^[0-9]{6}$ ]] && break
        warn "the 6 digits under Console password"
    done
    echo "Which board?"
    for i in "${!BOARDS[@]}"; do printf '  %d  %s\n' $((i + 1)) "$(board_name "${BOARDS[$i]}")"; done
    while :; do
        read -r -p "> " b || die "stopped"
        [[ $b =~ ^[0-9]+$ ]] && [ "$b" -ge 1 ] && [ "$b" -le ${#BOARDS[@]} ] && break
        warn "1 to ${#BOARDS[@]}"
    done
    local v
    v=$(net_version "$ip")
    if [ -n "$v" ]; then ok "$ip answers: bm $v"
    else warn "$ip does not answer now (is it on, and on the same network as the PC?): saved anyway"; fi
    PROFILES+=("$name|$ip|$code|${BOARDS[$((b - 1))]}|")
    PICK=$(( ${#PROFILES[@]} - 1 ))
    save_conf
    ok "saved: $name"
}

pick_profile() {                                # PICK: the profile chosen ($1: a name, if given)
    PICK=
    local i c
    if [ -n "${1:-}" ]; then
        for i in "${!PROFILES[@]}"; do
            [ "$(profile_field "${PROFILES[$i]}" 0)" = "$1" ] && PICK=$i
        done
        [ -n "$PICK" ] || die "no profile called $1"
        return
    fi
    while [ -z "$PICK" ]; do
        say "Consoles on the network"
        if [ ${#PROFILES[@]} -gt 0 ]; then list_profiles; else echo "  (none saved yet)"; fi
        echo "  n  a new one"
        [ ${#PROFILES[@]} -eq 0 ] || echo "  d  delete one"
        read -r -p "${PICK_Q:-Which one to update} (Enter: back)? " c || c=
        case $c in
            "") return 1 ;;
            n|N) new_profile ;;
            d|D)
                read -r -p "Number to delete: " c || c=
                if [[ $c =~ ^[0-9]+$ ]] && [ "$c" -ge 1 ] && [ "$c" -le ${#PROFILES[@]} ]; then
                    ok "deleted: $(profile_field "${PROFILES[$((c - 1))]}" 0)"
                    unset 'PROFILES[c-1]'
                    PROFILES=("${PROFILES[@]}")
                    save_conf
                fi ;;
            *)
                if [[ $c =~ ^[0-9]+$ ]] && [ "$c" -ge 1 ] && [ "$c" -le ${#PROFILES[@]} ]; then
                    PICK=$((c - 1))
                else
                    warn "a number, n or d"
                fi ;;
        esac
    done
}

# the console answers on $ip, the caller's (it may have changed: the router
# gives it); sets the caller's old to the version it runs
reach_console() {
    local x
    while :; do
        old=$(net_version "$ip")
        [ -z "$old" ] || return 0
        warn "$name does not answer on $ip (port 3333, bm's network console)."
        if ping -c 2 -W 2 "$ip" >/dev/null 2>&1; then
            warn "  $ip answers ping: the network is there, bm's console is not (another device on that IP,"
            warn "  or the console not connected yet)."
        else
            warn "  $ip does not answer ping either: the console is off the network, or the PC cannot reach it."
        fi
        if [ "$board" = rgb30 ]; then
            warn "  On the RGB30: System > WiFi, X joins; the IP and the line \"net: console on port 3333,"
            warn "  password ...\" come when it is in (wifi_boot=1 in bm/config.txt joins at every start)."
        else
            warn "  On the Pi: Settings > WiFi and network shows its IP once connected."
        fi
        read -r -p "Its IP now (Enter: stop): " x || x=
        [ -n "$x" ] || die "stopped"
        ip=$x
    done
}

# tools/bm_net.py on the caller's ip with its code (asked again if it is not
# the console's); sets the caller's out, returns bm_net's status
net_do() {
    local x rc
    while :; do
        rc=0
        out=$(python3 tools/bm_net.py "$ip" -p "$code" "$@" 2>&1 | tee /dev/stderr) || rc=$?
        grep -q "wrong password" <<< "$out" || return "$rc"
        read -r -p "Not its code: the Console password now (6 digits, Enter: stop): " x || x=
        [[ $x =~ ^[0-9]{6}$ ]] || die "stopped"
        code=$x
    done
}

job_net() {
    PICK_Q="Which one to update"
    pick_profile "${1:-}" || return 0
    local p=${PROFILES[$PICK]} name ip code board file old new out
    name=$(profile_field "$p" 0); ip=$(profile_field "$p" 1)
    code=$(profile_field "$p" 2); board=$(profile_field "$p" 3)
    up_to_date
    say "The kernel for $name ($(board_name "$board"), $ip)"
    case $board in
        pi) file=build/kernel.img ;;
        zero2) file=build/kernel7.img ;;
        rgb30) file=build/rgb30/kernel8.img ;;
        *) die "$name: unknown board $board" ;;
    esac
    if [ "$board" = rgb30 ]; then
        build_rgb30
    else
        make -j"$(nproc)" "$file"
    fi
    reach_console
    echo "$name runs bm $old"
    net_do --kernel "$file" || true
    PROFILES[PICK]="$name|$ip|$code|$board|$(profile_field "$p" 4)"     # IP and code as they work now
    save_conf
    new=$(sed -n 's/.*running bm \([^ ]*\).*/\1/p' <<< "$out" | tail -1)
    [ -n "$new" ] || die "the kernel did not go, or the console did not come back: see above"
    if [ "$new" = "$old" ] && [ "$(git describe --always --dirty)" != "$old" ]; then
        warn "$name came back with the same kernel: it did not start the one sent."
        [ "$board" != rgb30 ] || warn "An RGB30 kernel from before 2026-10-04 writes what it receives as kernel.img, which U-Boot \
does not start: put kernel8.img on its card once (2 [SD] update kernel, with the RGB30's card in the reader)."
    fi
    PROFILES[PICK]="$name|$ip|$code|$board|$(now) $old -> $new"
    LAST_RUN="$(now) net $name: $old -> $new"
    save_conf
    printf '\n\033[1;92mkernel (%s): %s -> %s\033[0m\n' "$name" "$old" "$new"
    exit 0
}

short_name() {                                  # an 8.3 name for the SD card (PONG.BM): the file's if it is one
    local b=$1 stem ext
    if [[ $b =~ ^[A-Za-z0-9_-]{1,8}(\.[A-Za-z0-9_-]{1,3})?$ ]]; then echo "$b"; return; fi
    stem=${b%.*}; ext=
    [ "$stem" = "$b" ] || ext=${b##*.}
    stem=$(tr -cd 'A-Za-z0-9_-' <<< "$stem"); ext=$(tr -cd 'A-Za-z0-9_-' <<< "$ext")
    stem=${stem:0:8}; ext=${ext:0:3}
    [ -n "$stem" ] || stem=FILE
    stem=${stem^^}; ext=${ext^^}
    echo "$stem${ext:+.$ext}"
}

# a file on a console's SD card (tools/bm_net.py --send): a game goes into
# /carts (bm/ on the RGB30) and the console's menu shows it once written, a
# resource into /bm/lib
job_send() {
    local file=${1:-} base n83 to x p name ip code board out old rc
    if [ -z "$file" ]; then
        say "Send a file to a console"
        echo "A game (.bm), a resource (.bmm .bmi .bms .bmt .bmc .bmk) or any file. Built here:"
        ls build/carts/*.bm 2>/dev/null | sed 's/^/  /' || echo "  (none: make builds them in build/carts/)"
        read -e -r -p "File (Tab completes; Enter: back): " file || file=
        [ -n "$file" ] || return 0
    fi
    file=${file/#\~/$HOME}
    if [ ! -f "$file" ] && [[ $file == build/carts/*.bm ]]; then
        make "$file" || true                    # a game of the repository not built yet
    fi
    [ -f "$file" ] || { warn "$file: no such file"; return 0; }
    base=$(basename "$file")
    n83=$(short_name "$base")
    if [ "$n83" != "$base" ]; then
        read -r -p "The SD card wants an 8.3 name (8 letters, dot, 3) [$n83]: " x || x=
        n83=${x:-$n83}
    fi
    case ${base,,} in
        *.bmm|*.bmi|*.bms|*.bmt|*.bmc|*.bmk) to=/bm/lib ;;
        *) to=/carts ;;
    esac
    read -r -p "Folder on the SD card [$to]: " x || x=
    to=${x:-$to}
    PICK_Q="Which one to send it to"
    pick_profile "${2:-}" || return 0
    p=${PROFILES[$PICK]}
    name=$(profile_field "$p" 0); ip=$(profile_field "$p" 1)
    code=$(profile_field "$p" 2); board=$(profile_field "$p" 3)
    reach_console
    say "$base -> $name ($ip): $to/$n83"
    rc=0
    net_do --send "$file" --to "$to" --name "$n83" || rc=$?
    PROFILES[PICK]="$name|$ip|$code|$board|$(profile_field "$p" 4)"     # IP and code as they work now
    save_conf
    if [ "$rc" = 0 ]; then
        LAST_RUN="$(now) sent $n83 to $name"
        save_conf
        ok "sent: $to/$n83 on $name"
        # the console answers as soon as the file arrived whole and writes it
        # from its menu ("Updating" on the game meanwhile); a game that is
        # open is replaced when it is closed. The RGB30 puts a game for
        # /carts in bm/, the folder its menu lists.
        [[ ${n83,,} != *.bm && ${n83,,} != *.b16 ]] || [ "$to" != /carts ] ||
            ok "the game is in its menu once written (Games, or Dev for a tool; if it is open: when you close it)"
    else
        warn "not sent: see above"
    fi
}

# a console's monitor on this PC (tools/bm_net.py): its keys and the
# commands of its monitor; Ctrl-Q (or Enter ~ .) leaves. With a line (the
# second argument): that monitor line run, its output shown (--line)
job_monitor() {
    PICK_Q="Which one to open"
    pick_profile "${1:-}" || return 0
    local p=${PROFILES[$PICK]} name ip code board old x rc line=${2:-}
    name=$(profile_field "$p" 0); ip=$(profile_field "$p" 1)
    code=$(profile_field "$p" 2); board=$(profile_field "$p" 3)
    reach_console
    while :; do
        rc=0
        if [ -n "$line" ]; then
            say "The line on $name ($ip, bm $old): $line"
            python3 tools/bm_net.py "$ip" -p "$code" --line "$line" || rc=$?
        else
            say "The monitor of $name ($ip, bm $old): Ctrl-Q leaves; in the menu a monitor line starts with ':'"
            python3 tools/bm_net.py "$ip" -p "$code" || rc=$?
        fi
        [ "$rc" = 1 ] || break                  # 1: not let in (2: the console in a game)
        read -r -p "It did not let you in? The Console password now (6 digits, Enter: stop): " x || x=
        [[ $x =~ ^[0-9]{6}$ ]] || break
        code=$x
    done
    PROFILES[PICK]="$name|$ip|$code|$board|$(profile_field "$p" 4)"     # IP and code as they work now
    save_conf
}

# ---------------------------------------------------------------- a console's settings
# bm/config.txt: "key=value" lines (src/kernel/config.c); the ones you may want
config_keys() {
    cat <<'KEYS'
  github_token   a GitHub token: the tests' reports go to the reports branch (and the Market)
  report_upload  0: the reports wait on the card, sent from Settings > Reports
  wifi_ssid      the WiFi network (Settings > WiFi and network saves it); wifi_psk its password
  wifi_boot      0: no WiFi at the start
  net_password   the Console password, 6 digits (from the console's next start)
  layout         it or us: the keyboard           volume   0 to 10
  game_intro     0: no loading splash             perf     1: the performance overlay
  gpu3d          0: the 3D on the ARM (gpu3d_aa, gpu3d_vs, gpu3d_queue: the GPU's options)
  menu_scale     3: the Pi's menu at 1920x1080    mouse    off: no pointer anywhere
  confirm        a: A confirms (RGB30)            show_bm  0: no .bm in the RGB30's menu
KEYS
}

# the changes typed, into KV: key=value sets a key, key= removes it, a key
# alone asks for its value (hidden as it is typed if it is a secret)
config_lines() {
    KV=()
    local l k v
    echo "Changes, one a line: key=value sets a key, key= removes it, a key alone asks for its value"
    echo "(not shown as you type it for a token, a password, a key); ? the keys; Enter: done."
    while :; do
        read -r -p "> " l || l=
        [ -n "$l" ] || break
        if [ "$l" = "?" ]; then config_keys; continue; fi
        if [[ $l =~ ^[A-Za-z0-9_]{1,23}$ ]]; then
            k=$l
            if [[ $k =~ (_token|_psk|_password|_key)$ ]]; then
                read -r -s -p "$k (hidden): " v || v=
                echo
            else
                read -r -p "$k: " v || v=
            fi
            l="$k=$v"
        fi
        if [[ ! $l =~ ^[A-Za-z0-9_]{1,23}= ]]; then
            warn "key=value (the key: letters, digits and _, at most 23)"
            continue
        fi
        v=${l#*=}
        if [ "$(printf %s "$v" | wc -c)" -gt 127 ]; then
            warn "at most 127 characters"
            continue
        fi
        if [[ $l == net_password=?* ]] && [[ ! $v =~ ^[0-9]{6}$ ]]; then
            warn "net_password: 6 digits (the profiles and the console's page expect them)"
            continue
        fi
        KV+=("$l")
    done
}

# a console's bm/config.txt over the network (bm_net.py --config): shown,
# then the changes typed go
job_config() {
    PICK_Q="Whose settings"
    pick_profile "${1:-}" || return 0
    local p=${PROFILES[$PICK]} name ip code board old out rc kv KV
    name=$(profile_field "$p" 0); ip=$(profile_field "$p" 1)
    code=$(profile_field "$p" 2); board=$(profile_field "$p" 3)
    reach_console
    say "bm/config.txt of $name ($ip, bm $old)"
    rc=0
    net_do --config || rc=$?
    PROFILES[PICK]="$name|$ip|$code|$board|$(profile_field "$p" 4)"     # IP and code as they work now
    save_conf
    if [ "$rc" != 0 ]; then
        if grep -q "newer kernel" <<< "$out"; then
            warn "1 [NET] update kernel first, then 7 again (or 8 with its card in the reader)"
        fi
        return 0
    fi
    config_lines
    [ ${#KV[@]} -gt 0 ] || return 0
    rc=0
    net_do --config "${KV[@]}" || rc=$?
    if [ "$rc" != 0 ]; then
        warn "not changed as asked: see above"
        return 0
    fi
    for kv in "${KV[@]}"; do
        if [[ $kv == net_password=?* ]]; then
            warn "$name takes the new Console password at its next start: the profile has it now (until then"
            warn "the old one is asked for)"
            code=${kv#*=}
        fi
    done
    PROFILES[PICK]="$name|$ip|$code|$board|$(now) config: ${#KV[@]} changed"
    LAST_RUN="$(now) config of $name: ${#KV[@]} changed"
    save_conf
    ok "bm/config.txt of $name changed"
}

# sd_config FILE [key=value ...]: the changes written into FILE (the other
# lines and the comments as they are; key= removes the key), then the
# settings shown, the secrets hidden
sd_config() {
    python3 - "$@" <<'PY'
import re, sys
path, changes = sys.argv[1], sys.argv[2:]
try:
    with open(path, encoding="utf-8", errors="surrogateescape") as f:
        lines = f.read().splitlines()
except FileNotFoundError:
    lines = []
if changes:
    lines = lines or ["# bm settings (key=value)"]
    for c in changes:
        k, v = c.split("=", 1)
        at = [i for i, l in enumerate(lines) if not l.startswith("#") and l.split("=", 1)[0] == k and "=" in l]
        if v and at:
            lines[at.pop(0)] = c
        elif v:
            lines.append(c)
        for i in reversed(at):
            del lines[i]
    with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
shown = 0
for l in lines:
    l = l.rstrip("\r ")
    if not l or l.startswith("#") or "=" not in l:
        continue
    k, v = l.split("=", 1)
    print(f"  {k}=(hidden, {len(v)} characters)" if re.search(r"(_token|_psk|_password|_key)$", k) else f"  {l}")
    shown += 1
if not shown:
    print("  (no settings: the console's defaults)")
PY
}

# bm/config.txt on the card in the reader: the console reads it at its next start
job_config_sd() {
    mount_sd
    local d=$SD/bm f KV
    if ! looks_like_bm && ! rgb30_card && [ ! -d "$d" ] && [ ! -d "$SD/bm33" ]; then
        ask "$SD does not look like bm's card: write bm/config.txt on it anyway?" || { unmount_sd; return 0; }
    fi
    f=$(find "$d" -maxdepth 1 -iname config.txt 2>/dev/null | head -1) || true
    if [ -z "$f" ]; then                        # the folder of before the rename, still read
        f=$(find "$SD/bm33" -maxdepth 1 -iname config.txt 2>/dev/null | head -1) || true
        [ -n "$f" ] || f=$d/config.txt
    fi
    say "${f#"$SD"/} on the card ($DRIVE:)"
    sd_config "$f"
    config_lines
    if [ ${#KV[@]} -gt 0 ]; then
        mkdir -p "$(dirname "$f")"
        echo "now:"
        sd_config "$f" "${KV[@]}"
        sync
        LAST_RUN="$(now) config on the card: ${#KV[@]} changed"
        save_conf
        ok "written: the console reads it at its next start"
    fi
    unmount_sd
    eject
    [ -n "${EASY_SD_DIR:-}" ] || ok "You can take the card out of the reader."
}

# a release on GitHub: scripts/release.sh VERSION --no-sd makes the tag, the
# CI tests it and publishes the release; the consoles (the Pi, the RGB30)
# take it from Settings > Updates
job_release() {
    local v=${1:-} last next minor
    say "A release on GitHub"
    last=$(git ls-remote --tags --refs origin 'v*' 2>/dev/null | sed 's|.*refs/tags/||' | sort -V | tail -1)
    echo "  the latest one: ${last:-none yet}"
    echo "  from:           $BRANCH at $(git rev-parse --short HEAD) ($(git log -1 --format=%s | cut -c1-50))"
    if [ "$BRANCH" != bm-core ] && ! ask "The releases come from bm-core, this is $BRANCH: go on anyway?"; then
        return 0
    fi
    if [ -z "$v" ]; then
        if [[ $last =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
            next="v${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.$((BASH_REMATCH[3] + 1))"
            minor="v${BASH_REMATCH[1]}.$((BASH_REMATCH[2] + 1)).0"
            echo "  $next for fixes, $minor for new things"
        else
            next=v0.1.0
        fi
        read -r -p "Version [$next]: " v || v=
        v=${v:-$next}
    fi
    [[ $v =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || { warn "a version is vX.Y.Z (e.g. $next)"; return 0; }
    echo "scripts/release.sh asks before every step that changes GitHub (the secret, a push, the tag)."
    scripts/release.sh "$v" --no-sd || { warn "the release stopped: see above"; return 0; }
    LAST_RUN="$(now) release $v"
    save_conf
    exit 0
}

# an older release on the card: the signed manifest of a release on GitHub
# (the same files the consoles' Settings > Updates fetch: manifest.txt +
# manifest.sig, checked with keys/release-pub.pem), every file downloaded and
# checked (size, SHA-256) BEFORE the card is touched; then the other files, the
# old kernels in bm/backup, the card's own kernel last
job_rollback() {
    local v=${1:-} rel=${BM_RELEASES:-https://github.com/f-accomando/bm} man=manifest base tmp i
    local -a tags=()
    command -v openssl >/dev/null && command -v curl >/dev/null || die "openssl and curl are needed"
    [ -f keys/release-pub.pem ] || die "keys/release-pub.pem is missing: the signature cannot be checked"
    say "An older release on the card"
    mount_sd
    local kimg=kernel.img
    if rgb30_card; then kimg=kernel8.img; man=manifest-rgb30; fi
    if [ -z "$v" ]; then
        mapfile -t tags < <(git ls-remote --tags --refs "$rel" 'v*' 2>/dev/null | sed 's|.*refs/tags/||' | sort -Vr)
        [ ${#tags[@]} -gt 0 ] || die "no release found at $rel"
        echo "  now on the card: $(kernel_version "$SD/$kimg")"
        for i in "${!tags[@]}"; do printf '  %2d  %s\n' $((i + 1)) "${tags[$i]}"; done
        read -r -p "Which release (number or vX.Y.Z, empty to stop): " v || v=
        [ -n "$v" ] || return 0
        if [[ $v =~ ^[0-9]+$ ]] && [ "$v" -ge 1 ] && [ "$v" -le ${#tags[@]} ]; then v=${tags[$((v - 1))]}; fi
    fi
    [[ $v =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "a version is vX.Y.Z (e.g. v0.1.0)"
    base=$rel/releases/download/$v
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' RETURN
    curl -fsSL "$base/$man.txt" -o "$tmp/$man.txt" && curl -fsSL "$base/$man.sig" -o "$tmp/$man.sig" \
        || die "$v has no $man.txt/.sig on GitHub (a release from before this card's board?)"
    openssl dgst -sha256 -verify keys/release-pub.pem -signature "$tmp/$man.sig" "$tmp/$man.txt" 2>/dev/null \
        | grep -q 'Verified OK' || die "the signature of $v does not match keys/release-pub.pem: nothing written"
    ok "$v: manifest signed, good"
    local kind name path size sha got
    local -a names=() paths=()
    while read -r kind name path size sha _; do
        [ "$kind" = file ] || continue
        [[ $name =~ ^[A-Za-z0-9._-]+$ && $path == /* && $path != *..* && $size =~ ^[0-9]+$ && $sha =~ ^[0-9a-f]{64}$ ]] \
            || die "the manifest has a bad line for ${name:-?}: nothing written"
        curl -fsSL "$base/$name" -o "$tmp/$name" || die "$name: download failed: nothing written"
        got=$(stat -c %s "$tmp/$name")
        [ "$got" = "$size" ] && [ "$(sha256sum "$tmp/$name" | cut -d' ' -f1)" = "$sha" ] \
            || die "$name: not the file of the manifest (size or SHA-256): nothing written"
        names+=("$name"); paths+=("$path")
    done < "$tmp/$man.txt"
    [ ${#names[@]} -gt 0 ] || die "the manifest of $v has no files"
    ok "all ${#names[@]} files downloaded and checked"
    looks_like_bm || ask "$SD does not look like bm's card: write $v on it anyway?" || die "stopped"
    ask "Write $v on the card ($SD)? The old kernels stay in bm/backup." y || { warn "stopped, nothing written"; return 0; }
    local old pass
    old=$(kernel_version "$SD/$kimg")
    for pass in rest kernels card; do               # the card's own kernel last
        for i in "${!names[@]}"; do
            name=${names[$i]} path=${paths[$i]}
            case $pass in
                rest)    [[ $name == kernel*.img ]] && continue ;;
                kernels) [[ $name == kernel*.img && $name != "$kimg" ]] || continue ;;
                card)    [ "$name" = "$kimg" ] || continue ;;
            esac
            if [[ $name == kernel*.img && -f "$SD$path" ]]; then
                mkdir -p "$SD/bm/backup"
                cp "$SD$path" "$SD/bm/backup/$name"
            fi
            mkdir -p "$(dirname "$SD$path")"
            cp "$tmp/$name" "$SD$path"
            sync
            cmp -s "$tmp/$name" "$SD$path" || die "$name on the card is not the downloaded one: try again"
        done
    done
    finish rollback "$old" "$(kernel_version "$SD/$kimg")"
}

# the bm Market: scripts/market.sh sets it up the first time (the key, the
# secret, GitHub Pages) and puts the project's games in it, every time
job_market() {
    say "The bm Market"
    echo "  the market's clone: $(dirname "$REPO")/bm-market"
    echo "scripts/market.sh asks before every step that changes GitHub (the secret, Pages, a push)."
    scripts/market.sh --market "$(dirname "$REPO")/bm-market" || { warn "the market stopped: see above"; return 0; }
    LAST_RUN="$(now) market"
    save_conf
    exit 0
}

# ---------------------------------------------------------------- the jobs
finish() {                                      # the last line: old kernel -> new one
    LAST_RUN="$(now) $1: $2 -> $3"
    save_conf
    unmount_sd
    eject
    [ -n "${EASY_SD_DIR:-}" ] || ok "You can take the card out of the reader."
    printf '\n\033[1;92mkernel: %s -> %s\033[0m\n' "$2" "$3"
    exit 0
}

job_kernel() {                                  # the Pi's kernel.img, or the RGB30's kernel8.img on its card
    up_to_date
    mount_sd
    local img=kernel.img built=build/kernel.img old new
    if rgb30_card; then
        img=kernel8.img built=build/rgb30/kernel8.img
        say "The RGB30's kernel (the card has extlinux/ and kernel8.img)"
        build_rgb30
    else
        say "The kernel"
        looks_like_bm || ask "$SD does not look like bm's card: write the kernel on it anyway?" || die "stopped"
        make -j"$(nproc)" build/kernel.img
    fi
    old=$(kernel_version "$SD/$img")
    if [ -f "$SD/$img" ]; then
        mkdir -p "$SD/bm/backup"
        cp "$SD/$img" "$SD/bm/backup/$img"
    fi
    cp "$built" "$SD/$img"
    sync
    cmp -s "$built" "$SD/$img" || die "the kernel on the card is not the one built: try again"
    new=$(kernel_version "$SD/$img")
    finish kernel "$old" "$new"
}

job_install() {
    up_to_date
    mount_sd
    local old new
    if rgb30_card; then                         # the RGB30's partition: kernel8.img, extlinux, bm/
        say "Full install on the RGB30's card"
        build_rgb30
        old=$(kernel_version "$SD/kernel8.img")
        make TARGET=rgb30 sdcard SD="$SD"
        new=$(kernel_version "$SD/kernel8.img")
        finish install "$old" "$new"
    fi
    say "Full install"
    make -j"$(nproc)"
    looks_like_bm || ask "$SD does not look like bm's card: install on it anyway?" || die "stopped"
    old=$(kernel_version "$SD/kernel.img")
    make install SD="$SD"
    new=$(kernel_version "$SD/kernel.img")
    finish install "$old" "$new"
}

disk_info() {                                   # number|name|bytes|bus|boot|system|type, or nothing
    [ -n "${EASY_SD_DIR:-}" ] && { echo "9|a folder (test)|$((16 << 30))|SD|False|False|Removable"; return; }
    win "\$ErrorActionPreference='Stop'; \$p = Get-Partition -DriveLetter $DRIVE; \$d = Get-Disk -Number \$p.DiskNumber; \
\$v = Get-Volume -DriveLetter $DRIVE; '{0}|{1}|{2}|{3}|{4}|{5}|{6}' -f \$d.Number, \$d.FriendlyName, \$d.Size, \$d.BusType, \
\$d.IsBoot, \$d.IsSystem, \$v.DriveType" | tail -1
}

# the two PowerShell scripts of the format: launch.ps1 asks Windows for an
# administrator window (UAC) that runs format-sd.ps1 and waits for it
write_ps() {
    cat > "$1/launch.ps1" <<'PS1'
param([string]$Letter, [int]$Disk, [long]$Size)
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$log = Join-Path $here 'format-sd.log'
Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
$a = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -Letter {1} -Disk {2} -Size {3} -Log "{4}"' -f `
     (Join-Path $here 'format-sd.ps1'), $Letter, $Disk, $Size, $log
try {
    Start-Process -FilePath powershell.exe -Verb RunAs -Wait -ArgumentList $a
} catch {
    Write-Output "error: the administrator window was not allowed ($($_.Exception.Message))"
    exit 3
}
if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log } else { Write-Output 'error: no answer from the administrator window' }
PS1
    cat > "$1/format-sd.ps1" <<'PS1'
# Erases the SD card in drive $Letter and makes one FAT32 partition (label
# BM) on it, the whole card up to 31 GB: Windows formats FAT32 no bigger,
# and the Pi starts only from FAT. Run as administrator by launch.ps1.
param([string]$Letter, [int]$Disk, [long]$Size, [string]$Log)
function Say($m) { Write-Host $m; Add-Content -LiteralPath $Log -Value $m }
try {
    $ErrorActionPreference = 'Stop'
    $part = Get-Partition -DriveLetter $Letter
    $d = Get-Disk -Number $part.DiskNumber
    Say ('disk {0}: {1}, {2:N1} GB, {3}' -f $d.Number, $d.FriendlyName, ($d.Size / 1GB), $d.BusType)
    if ($d.IsBoot -or $d.IsSystem) { throw 'this is the disk Windows starts from: not touched' }
    if ($d.Size -gt 256GB) { throw 'bigger than 256 GB: not touched' }
    if ($Disk -ge 0) {
        if ($d.Number -ne $Disk -or $d.Size -ne $Size) { throw 'not the disk checked before: not touched' }
    } else {
        $r = Read-Host ('Erase disk {0} ({1}) and make it bm''s SD card? Type {2} to go on' -f $d.Number, $d.FriendlyName, $Letter)
        if ($r -ne $Letter) { throw 'stopped' }
    }
    $n = $d.Number
    Say "erasing disk $n..."
    Clear-Disk -Number $n -RemoveData -RemoveOEM -Confirm:$false
    if ((Get-Disk -Number $n).PartitionStyle -eq 'RAW') { Initialize-Disk -Number $n -PartitionStyle MBR }
    $size = [Math]::Min([long]($d.Size - 8MB), [long]31GB)
    Say ('new partition, {0:N1} GB...' -f ($size / 1GB))
    New-Partition -DiskNumber $n -Size $size -MbrType FAT32 -IsActive -DriveLetter $Letter | Out-Null
    Say 'formatting FAT32 (label BM)...'
    Format-Volume -DriveLetter $Letter -FileSystem FAT32 -NewFileSystemLabel BM -Confirm:$false -Force | Out-Null
    Say 'OK'
} catch {
    Say ('error: ' + $_.Exception.Message)
    Start-Sleep -Seconds 8
    exit 1
}
PS1
}

format_card() {                                 # erase and format, as administrator
    if [ -n "${EASY_SD_DIR:-}" ]; then          # tests: the folder emptied
        find "$SD" -mindepth 1 -delete
        return 0
    fi
    local wtemp dir out
    wtemp=$(win '[IO.Path]::GetTempPath()' | tail -1)
    [ -n "$wtemp" ] || die "PowerShell does not answer from WSL (is Windows interop on?)"
    dir=$(wslpath -u "$wtemp")/bm-easy-install
    mkdir -p "$dir"
    write_ps "$dir"
    echo "Windows asks for administrator rights (a UAC window): answer Yes."
    echo "If Windows offers to format the disk itself, press Cancel: this script formats it."
    out=$("$PS" -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w "$dir/launch.ps1")" \
          -Letter "$DRIVE" -Disk "$1" -Size "$2" 2>&1 | tr -d '\r') || true
    echo "$out" | sed 's/^/  /'
    [ "$(echo "$out" | tail -1)" = OK ] || die "the card was not formatted (see above)"
}

job_image() {
    if [ -z "${EASY_SD_DIR:-}" ]; then mount_sd; fi
    if rgb30_card; then
        unmount_sd
        die "This is the RGB30's card: its boot loader sits before the partition, and formatting would erase it.
For the RGB30: make TARGET=rgb30 firmware image, then write dist/rgb30/bm-rgb30.img with balenaEtcher or
Raspberry Pi Imager; 2 or 3 update its files."
    fi
    up_to_date
    say "The disk image"
    make -j"$(nproc)"
    make image
    local img=$REPO/dist/bm.img
    [ -f "$img" ] || die "make image made no dist/bm.img"
    ok "$img: $(du -h "$img" | cut -f1)"

    mount_sd
    local old info num name size bus boot sys type bk=
    old=$(kernel_version "$SD/kernel.img")
    info=$(disk_info || true)
    if [ -n "$info" ]; then
        IFS='|' read -r num name size bus boot sys type <<< "$info"
        say "The card"
        echo "  $DRIVE: is disk $num, $name, $(( size / 1000000000 )) GB, bus $bus, $type"
        if [ "$boot" = True ] || [ "$sys" = True ]; then die "$DRIVE: is on the disk Windows starts from: not touched"; fi
        [ "$size" -le $((256 << 30)) ] || die "$DRIVE: is bigger than 256 GB: not an SD card for the Pi"
        [ "$type" = Removable ] || [[ $bus =~ ^(SD|USB|MMC)$ ]] || warn "  it does not look like a removable card: check it is the right one"
    else
        num=-1 size=0
        warn "Windows did not give the card's details: the administrator window shows them and asks"
    fi
    warn "Everything on $DRIVE: is erased: the card is formatted and gets the image's files."
    local typed
    read -r -p "Type $DRIVE to go on: " typed || typed=
    [ "${typed%:}" = "$DRIVE" ] || [ "${typed%:}" = "${DRIVE,,}" ] || die "stopped: nothing touched"

    if [ -n "$(ls -A "$SD" 2>/dev/null)" ] && ask "Keep your settings, saves and games (bm/ and carts/: copied to the PC first, then back)?" y; then
        bk=$BACKUPS/$(date +%Y%m%d-%H%M%S)
        mkdir -p "$bk"
        for f in bm carts kernel.img; do
            if [ -e "$SD/$f" ]; then cp -r "$SD/$f" "$bk/"; fi
        done
        ok "copy of the card in $bk"
        ls -1d "$BACKUPS"/*/ 2>/dev/null | head -n -5 | xargs -r rm -rf     # the last 5 kept
    fi
    unmount_sd
    format_card "$num" "$size"
    mount_sd

    say "The image's files on the card"
    MTOOLS_SKIP_CHECK=1 mcopy -s -n -m -i "$img@@1M" '::*' "$SD/"
    if [ -n "$bk" ]; then                       # what the image has stays the image's
        local keep=-n                           # (newer cp: --update=none)
        cp --update=none /dev/null "$bk/.cp-test" 2>/dev/null && keep=--update=none
        rm -f "$bk/.cp-test"
        if [ -d "$bk/bm" ]; then cp -r $keep "$bk/bm/." "$SD/bm/"; fi
        if [ -d "$bk/carts" ]; then cp -r $keep "$bk/carts/." "$SD/carts/"; fi
        ok "settings, saves and games put back"
    fi
    sync
    cmp -s build/kernel.img "$SD/kernel.img" || die "the kernel on the card is not the one built: try again"
    ls "$SD"
    finish image "$old" "$(kernel_version "$SD/kernel.img")"
}

# ---------------------------------------------------------------- start
BRANCH=$(git rev-parse --abbrev-ref HEAD)
say "bm easy install"
echo "  branch      $(branch_line)"
echo "  repository  $REPO"
echo "  SD card     $DRIVE: ($SD)"
[ -z "$LAST_RUN" ] || echo "  last time   $LAST_RUN"
packages
firmware

case ${1:-} in
    kernel) job_kernel ;;
    install) job_install ;;
    image) job_image ;;
    net) job_net "${2:-}" ;;
    send) job_send "${2:-}" "${3:-}"; exit 0 ;;
    monitor) job_monitor "${2:-}"; exit 0 ;;
    line) [ -n "${2:-}" ] || die "line \"gpu; b3d; send\" [profile]: the line is missing"
          job_monitor "${3:-}" "$2"; exit 0 ;;
    bench) job_monitor "${3:-}" "set overbit_bench=${2:-auto}; play overbit; send"; exit 0 ;;
    config) job_config "${2:-}"; exit 0 ;;
    config-sd) job_config_sd; exit 0 ;;
    release) job_release "${2:-}"; exit 0 ;;
    rollback) job_rollback "${2:-}" ;;
    market) job_market; exit 0 ;;
    "") ;;
    *) die "unknown: $1 (kernel, install, image, net [profile], bench [FLAGS] [profile], send FILE [profile], monitor [profile], \
line \"LINE\" [profile], config [profile], config-sd, release [vX.Y.Z], rollback [vX.Y.Z], market, or nothing for the menu)" ;;
esac

while :; do
    cat <<MENU

  1  [NET] update kernel   (to a console on the network: saved profiles, or a new one)
  2   [SD] update kernel   (kernel.img only; the old one stays in bm/backup)
  3   [SD] full install    (kernel, boot files, games, bm/: make install)
  4   [SD] disk image      (make image, card erased and formatted, the image's files)
  5  [NET] send a file     (a game or a file to a console's SD card: bm_net.py --send)
  6  [NET] monitor         (a console's monitor here: its keys and commands; Ctrl-Q leaves)
  7  [NET] config          (a console's bm/config.txt: shown, keys set or removed)
  8   [SD] config          (bm/config.txt on the card in the reader)
  r  release               (a new version on GitHub: release.sh --no-sd; the consoles update)
  o  older release         (an older signed release on the card: checked first, old kernels in bm/backup)
  m  market                (the games in the bm Market: market.sh; the first time its setup)
  b  branch                (now $BRANCH: change it or update it)
  p  paths                 (repository folder, SD card letter)
  q  quit
MENU
    read -r -p "> " c || exit 0
    case $c in
        1) job_net ;;
        2) job_kernel ;;
        3) job_install ;;
        4) job_image ;;
        5) job_send ;;
        6) job_monitor ;;
        7) job_config ;;
        8) job_config_sd ;;
        r|R) job_release ;;
        o|O) job_rollback ;;
        m|M) job_market ;;
        b|B) change_branch; BRANCH=$(git rev-parse --abbrev-ref HEAD); echo "  branch      $(branch_line)" ;;
        p|P) change_paths ;;
        q|Q|"") exit 0 ;;
        *) warn "1 to 8, r, o, m, b, p or q" ;;
    esac
done
