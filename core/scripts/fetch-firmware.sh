#!/bin/sh
# Downloads the closed-source VideoCore boot files needed by the Pi Zero
# (the same files start the Pi Zero 2 W), and the firmware of the WiFi /
# Bluetooth chips of the Zero W and Zero 2 W.
# Usage: fetch-firmware.sh [dest-dir]   (FW_REF=<git ref> to pin a version)
set -eu

DEST=${1:-firmware}
REF=${FW_REF:-master}
BASE="https://raw.githubusercontent.com/raspberrypi/firmware/$REF/boot"

mkdir -p "$DEST"
for f in bootcode.bin start.elf fixup.dat LICENCE.broadcom; do
    echo "fetching $f ($REF)"
    curl -fL --retry 3 -o "$DEST/$f" "$BASE/$f"
done

# Bluetooth firmware patches, from Raspberry Pi OS's bluez-firmware package;
# redistributable, not stored in this repo. Optional: without one the chip
# runs its ROM firmware. BCM43430A1.hcd: the Pi Zero W (BCM43438); the
# Zero 2 W's CYW43436 comes in two versions, SYN43430A1 and SYN43430B0.
fetch_bt() {  # fetch_bt <name> <dir in the repo>
    for path in "bookworm/debian/firmware/$2/$1" "master/debian/firmware/$2/$1" "master/$2/$1"; do
        url="https://raw.githubusercontent.com/RPi-Distro/bluez-firmware/$path"
        echo "fetching $1 ($url)"
        if curl -fL --retry 2 -o "$DEST/$1" "$url"; then
            return 0
        fi
        rm -f "$DEST/$1"
    done
    echo "warning: Bluetooth firmware $1 not found (optional)"
}
fetch_bt BCM43430A1.hcd broadcom
fetch_bt SYN43430A1.hcd synaptics
fetch_bt SYN43430B0.hcd synaptics

# WiFi firmware of the same chips (M18), from Raspberry Pi OS's
# firmware-nonfree package (Cypress/Broadcom, redistributable, not stored in
# this repo): the chip's ARM code, its board settings (NVRAM text) and the
# regulatory data (CLM). On GitHub some of these are symbolic links: a
# download of a few bytes is the link's target, fetched in turn.
fetch_nonfree() {  # fetch_nonfree <local name> <path in the repo>...
    name=$1; out="$DEST/$1"; shift
    for p in "$@"; do
        for br in bookworm bullseye master; do
            base="https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/$br/debian/config/brcm80211"
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
# the Pi Zero 2 W: CYW43436 (chip rev 2 and later) or 43436s (rev 1, with
# the 43430's regulatory data)
fetch_nonfree brcmfmac43436-sdio.bin brcm/brcmfmac43436-sdio.bin
fetch_nonfree brcmfmac43436-sdio.txt brcm/brcmfmac43436-sdio.txt
fetch_nonfree brcmfmac43436-sdio.clm_blob brcm/brcmfmac43436-sdio.clm_blob
fetch_nonfree brcmfmac43436s-sdio.bin brcm/brcmfmac43436s-sdio.bin
fetch_nonfree brcmfmac43436s-sdio.txt brcm/brcmfmac43436s-sdio.txt
