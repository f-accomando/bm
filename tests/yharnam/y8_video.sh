#!/bin/sh
# A video of Yharnam 8 (carts/nano8/roms/yharnam8.p8) played by the bot of
# tests/yharnam/y8_bot.lua in nano8 on the PC (n8host: the console's
# machine and sound), from the title to the Butcher slain. The run is the
# same every time: once to know its length, once for the sound, once for
# the pictures (the cart 3x, 30 frames a second).
#
#   tests/yharnam/y8_video.sh BUILD OUT.mp4
set -e
B=$1 OUT=$2
SD=$(mktemp -d)
trap 'rm -rf "$SD"' EXIT
mkdir -p "$SD/carts/nano8"
cp carts/nano8/roms/yharnam8.p8 "$SD/carts/nano8/"
run() {
    "$B/host/n8host" "$B/nano8/main.lua" --root "$SD" --exec "NANO8.Ui.play(1)" \
        --at "5:exec=dofile('tests/yharnam/y8_bot.lua')" --quiet "$@"
}
END=$(run --frames 60000 2>&1 | sed -n 's/.*slain at tick \([0-9]*\).*/\1/p')
[ -n "$END" ] || { echo "the bot did not slay the Butcher" >&2; exit 1; }
N=$((END + 240))
Y8_NOEXIT=1 run --frames $N --wav "$SD/sound.wav" >/dev/null 2>&1
Y8_NOEXIT=1 run --frames $N --video - --every 2 2>/dev/null |
  ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgb24 -s 640x360 -r 30 -i - -i "$SD/sound.wav" \
    -vf "crop=256:256:192:52,scale=768:768:flags=neighbor" -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p \
    -c:a aac -b:a 128k -shortest -movflags +faststart "$OUT"
echo "video: $OUT ($((N / 60)) s)"
