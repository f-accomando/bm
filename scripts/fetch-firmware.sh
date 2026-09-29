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

# WiFi firmware of the same chip (M18), from Raspberry Pi OS's
# firmware-nonfree package (Cypress/Broadcom, redistributable, not stored in
# this repo): the chip's ARM code, its board settings (NVRAM text) and the
# regulatory data (CLM). On GitHub some of these are symbolic links: a
# download of a few bytes is the link's target, fetched in turn.
fetch_nonfree() {  # fetch_nonfree <local name> <path in the repo>...
    name=$1; out="$DEST/$1"; shift
    for p in "$@"; do
        for br in bookworm bullseye master; do
            base="https://github.com/RPi-Distro/firmware-nonfree/raw/$br/debian/config/brcm80211"
            url="$base/$p"
            if curl -sfL --retry 2 -o "$out" "$url"; then
                size=$(wc -c < "$out")
                if [ "$size" -lt 200 ]; then            # a symbolic link: follow it
                    target=$(cat "$out")
                    dir=$(dirname "$p")
                    case "$target" in
                        ../*) parent=$(dirname "$dir")
                              if [ "$parent" = "." ]; then path=${target#../}
                              else path="$parent/${target#../}"; fi ;;
                        *) path="$dir/$target" ;;
                    esac
                    if curl -sfL --retry 2 -o "$out" "$base/$path" && \
                       [ "$(wc -c < "$out")" -ge 200 ]; then
                        echo "fetched $name ($br, $path)"; return 0
                    fi
                    continue
                fi
                echo "fetched $name ($br, $p)"; return 0
            fi
        done
    done
    rm -f "$out"
    echo "warning: $name not found (WiFi will not start)"
    return 0
}
fetch_nonfree brcmfmac43430-sdio.bin brcm/brcmfmac43430-sdio.bin cypress/cyfmac43430-sdio.bin
fetch_nonfree brcmfmac43430-sdio.txt "brcm/brcmfmac43430-sdio.raspberrypi,model-zero-w.txt" \
    brcm/brcmfmac43430-sdio.txt
fetch_nonfree brcmfmac43430-sdio.clm_blob brcm/brcmfmac43430-sdio.clm_blob cypress/cyfmac43430-sdio.clm_blob
