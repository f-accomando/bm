#!/bin/sh
# Frames of a cartridge on the PC build and on the WebAssembly build of bmhost, one in every 60, compared byte
# for byte. Used for the 3D cartridge of the stages (the ARM rasterizer r3d.c in wasm).
#   sh tests/webplayer/compare3d.sh BUILD CART.bm INPUT.txt SECONDS
# Needs: make build/host/bmhost-bin build/web/bmhost.js, and node (source ~/emsdk/emsdk_env.sh).
B=${1:-build}; CART=$2; INPUT=$3; SECS=${4:-20}
W=$B/webcmp
rm -rf "$W"; mkdir -p "$W/n" "$W/w" "$W/nsd" "$W/wsd"
"$B/host/bmhost-bin" "$CART" --quiet --sd "$W/nsd" --input "$INPUT" --seconds "$SECS" --shots "$W/n" --every 60 > "$W/n.log" 2>&1
node "$B/web/bmhost.js" "$CART" --quiet --sd "$W/wsd" --input "$INPUT" --seconds "$SECS" --shots "$W/w" --every 60 > "$W/w.log" 2>&1
same=0; diff=0
for f in "$W"/n/*.png; do
  n=${f##*/}
  if cmp -s "$f" "$W/w/$n"; then same=$((same + 1)); else diff=$((diff + 1)); echo "differs: $n"; fi
done
echo "compare3d: $same frames equal, $diff differ"
[ "$diff" -eq 0 ] && [ "$same" -gt 0 ]
