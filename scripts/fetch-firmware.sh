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

# Bluetooth firmware patch of the Pi Zero W chip (BCM43438), from Raspberry
# Pi OS's bluez-firmware package; redistributable, not stored in this repo.
# Optional: without it the chip runs its ROM firmware.
for url in \
    "https://github.com/RPi-Distro/bluez-firmware/raw/bookworm/debian/firmware/broadcom/BCM43430A1.hcd" \
    "https://github.com/RPi-Distro/bluez-firmware/raw/master/debian/firmware/broadcom/BCM43430A1.hcd" \
    "https://github.com/RPi-Distro/bluez-firmware/raw/master/broadcom/BCM43430A1.hcd"; do
    echo "fetching BCM43430A1.hcd ($url)"
    if curl -fL --retry 2 -o "$DEST/BCM43430A1.hcd" "$url"; then
        break
    fi
    rm -f "$DEST/BCM43430A1.hcd"
done
[ -f "$DEST/BCM43430A1.hcd" ] || echo "warning: Bluetooth firmware not found (optional)"
