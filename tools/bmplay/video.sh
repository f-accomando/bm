#!/bin/sh
# A video of a cartridge played by a bot, with the console's own drawing and
# sound (bmplay): the sound first, then the same run again for the pictures
# (the run is the same every time: the seed and the bot decide it).
#
#   tools/bmplay/video.sh BMPLAY CART.bm BOT.lua OUT.mp4 [seed] [scale]
#
# The pictures at 30 frames a second, every pixel scale x scale (default 3).
set -e
BMPLAY=$1 CART=$2 BOT=$3 OUT=$4 SEED=${5:-1} SCALE=${6:-3}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
"$BMPLAY" "$CART" --bot "$BOT" --seed "$SEED" --audio "$TMP/sound.raw"
SIZE=$("$BMPLAY" "$CART" --size)
W=${SIZE%x*} H=${SIZE#*x}
"$BMPLAY" "$CART" --bot "$BOT" --seed "$SEED" --video - --every 2 2>/dev/null |
  ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgb24 -s "${W}x${H}" -r 30 -i - \
    -f s16le -ar 48000 -ac 1 -i "$TMP/sound.raw" \
    -vf "scale=$((W * SCALE)):$((H * SCALE)):flags=neighbor" -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p \
    -c:a aac -b:a 128k -shortest -movflags +faststart "$OUT"
echo "video: $OUT"
