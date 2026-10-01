#!/bin/sh
# Downloads what the RGB30's SD card needs and bm does not store:
#  - the boot loader (Rockchip DDR init + U-Boot 2026.01 + BL31) from the
#    official ROCKNIX image for the RK3566 consoles: only its first MiB
#    (an HTTP range request), from which the two pieces are cut out;
#  - the Realtek RTL8821CS firmware (Bluetooth, WiFi) from linux-firmware.
# Every file is checked against its SHA-256. Sources and licences:
# docs/RGB30.md. Usage: fetch-rgb30.sh [dest-dir]   (default firmware/rgb30)
set -eu

DEST=${1:-firmware/rgb30}
ROCKNIX=${ROCKNIX:-20260901}
IMG_URL="https://github.com/ROCKNIX/distribution/releases/download/$ROCKNIX/ROCKNIX-RK3566.aarch64-$ROCKNIX-Generic.img.gz"
FW_URL="https://gitlab.com/kernel-firmware/linux-firmware/-/raw"
FW_REF=${FW_REF:-3653d692bd0da291af02e9e4a613049315e241b9}

mkdir -p "$DEST"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

check() {  # check <file> <sha256>
    echo "$2  $1" | sha256sum -c --quiet - || { echo "fetch-rgb30: $1: unexpected content" >&2; exit 1; }
}

echo "fetching the boot loader (first MiB of ROCKNIX $ROCKNIX for RK3566)"
curl -fL --retry 3 -r 0-1048575 -o "$tmp/head.gz" "$IMG_URL"
gzip -dc "$tmp/head.gz" > "$tmp/head.img" 2>/dev/null || true    # truncated on purpose
dd if="$tmp/head.img" of="$DEST/idbloader.img" bs=512 skip=64 count=388 status=none
dd if="$tmp/head.img" of="$tmp/itb" bs=512 skip=16384 count=3185 status=none
head -c 1630680 "$tmp/itb" > "$DEST/u-boot.itb"
check "$DEST/idbloader.img" 266b42538ff8ee7215c5e6a8e9ec2bd3c092541440fb56a03e1123ad6b13b91a
check "$DEST/u-boot.itb" 1f7407a0c57b7affc120e519df3724153f2f6e7884e754a643fe7d9177696d2a

fetch_fw() {  # fetch_fw <path in linux-firmware> <local name> <sha256>
    echo "fetching $1"
    curl -fL --retry 3 -o "$DEST/$2" "$FW_URL/$FW_REF/$1"
    check "$DEST/$2" "$3"
}
fetch_fw rtl_bt/rtl8821cs_fw.bin rtl8821cs_fw.bin \
    3baa2eeaa43c959054687a67771e7435e73b2ff3e79dfb765121d8b7dc719391
fetch_fw rtl_bt/rtl8821cs_config.bin rtl8821cs_config.bin \
    6ddeb15f23588053e00cb08d25588bd7cf98d60fa93d9478efcef4ae8064a7ac
FW_REF=${WIFI_FW_REF:-07338f9d3308}
fetch_fw rtw88/rtw8821c_fw.bin rtw8821c_fw.bin \
    2ef409bc418549fcf294061dd0cae1fc22fd9da79b60524950b25de18732f3f0
curl -fL --retry 3 -o "$DEST/LICENCE.rtlwifi_firmware.txt" \
    "$FW_URL/main/LICENSES/LICENCE.rtlwifi_firmware.txt" || echo "warning: licence text not fetched"
echo "done: $(ls "$DEST" | tr '\n' ' ')"
