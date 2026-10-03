#!/bin/sh
# Copies the files of the RGB30's BM partition (dist/rgb30/sd, from
# `make TARGET=rgb30 sdcard`) onto a card made with `make TARGET=rgb30 image`,
# mounted at DIR (WSL: /mnt/<the drive's letter>), saying what is wrong when
# it cannot: not mounted, read-only, not the RGB30's card. The card's
# bm/config.txt is replaced only if the build has one (RGB30_CONFIG).
# Usage: copy-sd-rgb30.sh SRC DIR
set -eu

SRC=$1
DIR=${2%/}
letter=$(basename "$DIR" | tr a-z A-Z)
[ ${#letter} = 1 ] || letter=X

mount_hint() {
    echo "  On WSL, with the card in the PC (Windows shows it as a drive named BM):" >&2
    echo "    sudo umount $DIR 2>/dev/null; sudo mkdir -p $DIR && sudo mount -t drvfs $letter: $DIR -o uid=$(id -u),gid=$(id -g)" >&2
    echo "  ($letter: being the drive's letter in Windows' File Explorer), then run make again." >&2
}

if [ ! -d "$DIR" ] || [ -z "$(ls -A "$DIR" 2>/dev/null)" ]; then
    echo "copy-sd-rgb30: nothing at $DIR: the card is not mounted there." >&2
    mount_hint
    exit 1
fi
if [ ! -f "$DIR/extlinux/extlinux.conf" ]; then
    if [ -f "$DIR/bootcode.bin" ] || [ -f "$DIR/kernel.img" ]; then
        echo "copy-sd-rgb30: $DIR is the Raspberry Pi's card, not the RGB30's:" >&2
    else
        echo "copy-sd-rgb30: $DIR is not the RGB30's card (no extlinux/extlinux.conf):" >&2
    fi
    ls "$DIR" | head -8 | sed 's/^/    /' >&2
    echo "  Choose the drive named BM, or write dist/rgb30/bm-rgb30.img to the card first." >&2
    exit 1
fi
probe="$DIR/.bm-write-test"
if ! touch "$probe" 2>/dev/null; then
    echo "copy-sd-rgb30: cannot write to $DIR." >&2
    echo "  - an SD adapter with its lock switch down (towards LOCK) is read-only: slide it up;" >&2
    echo "  - or the drive is mounted only for root:" >&2
    mount_hint
    exit 1
fi
rm -f "$probe"

cp -r "$SRC/." "$DIR/"
sync
echo "Copied onto the card at $DIR:"
(cd "$SRC" && find . -type f | sed 's|^\./|    |' | sort)
echo "Eject the card in Windows before taking it out."
