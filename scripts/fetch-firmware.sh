#!/bin/sh
# Downloads the closed-source VideoCore boot files needed by the Pi Zero.
# Usage: fetch-firmware.sh [dest-dir]   (FW_REF=<git ref> to pin a version)
set -eu

DEST=${1:-firmware}
REF=${FW_REF:-master}
BASE="https://github.com/raspberrypi/firmware/raw/$REF/boot"

mkdir -p "$DEST"
for f in bootcode.bin start.elf fixup.dat LICENCE.broadcom; do
    echo "fetching $f ($REF)"
    curl -fL --retry 3 -o "$DEST/$f" "$BASE/$f"
done
