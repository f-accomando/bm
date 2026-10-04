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
#   b  branch              change it, or bring it up to date (git pull)
#   p  paths               the repository's folder, the card's drive letter
# The same as an argument: ./easy_install.sh kernel | install | image | net [profile].
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
    git fetch -q origin 2>/dev/null || true
    echo "on GitHub:"
    git branch -r --format='  %(refname:lstrip=3)' | grep -v '^  HEAD$' | head -20
    local b
    read -r -p "Branch to use (Enter: stay on $BRANCH and update it): " b || b=
    if [ -z "$b" ] || [ "$b" = "$BRANCH" ]; then
        git pull --ff-only || warn "git pull failed"
        return
    fi
    [ -z "$(git status --porcelain --untracked-files=no)" ] || die "there are changes not committed: commit or git stash them first"
    git checkout "$b" && git pull --ff-only || warn "could not move to $b"
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
        read -r -p "Which one to update (Enter: back)? " c || c=
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

job_net() {
    pick_profile "${1:-}" || return 0
    local p=${PROFILES[$PICK]} name ip code board file old new out x
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
    while :; do                                 # the IP may have changed (the router gives it)
        old=$(net_version "$ip")
        [ -z "$old" ] || break
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
    echo "$name runs bm $old"
    while :; do
        out=$(python3 tools/bm_net.py "$ip" -p "$code" --kernel "$file" 2>&1 | tee /dev/stderr) || true
        grep -q "wrong password" <<< "$out" || break
        read -r -p "Not its code: the Console password now (6 digits, Enter: stop): " x || x=
        [[ $x =~ ^[0-9]{6}$ ]] || die "stopped"
        code=$x
    done
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
    "") ;;
    *) die "unknown: $1 (kernel, install, image, net [profile], or nothing for the menu)" ;;
esac

while :; do
    cat <<MENU

  1  [NET] update kernel   (to a console on the network: saved profiles, or a new one)
  2   [SD] update kernel   (kernel.img only; the old one stays in bm/backup)
  3   [SD] full install    (kernel, boot files, games, bm/: make install)
  4   [SD] disk image      (make image, card erased and formatted, the image's files)
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
        b|B) change_branch; BRANCH=$(git rev-parse --abbrev-ref HEAD); echo "  branch      $(branch_line)" ;;
        p|P) change_paths ;;
        q|Q|"") exit 0 ;;
        *) warn "1, 2, 3, 4, b, p or q" ;;
    esac
done
