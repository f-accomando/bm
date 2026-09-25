#!/bin/sh
# Boots the kernel in QEMU (raspi0) headless and dumps the framebuffer.
# Usage: qemu-screenshot.sh <kernel.elf> <out.png> [seconds]
set -eu

ELF=$1
OUT=$2
SECS=${3:-3}
QEMU=${QEMU:-qemu-system-arm}

{
    sleep "$SECS"
    echo "screendump $OUT -f png"
    sleep 1
    echo "quit"
} | "$QEMU" -M raspi0 -kernel "$ELF" -display none -monitor stdio -serial null >/dev/null

test -s "$OUT" && echo "saved $OUT"
