#!/bin/bash
# icache_search.sh TAG [extra icache_sim.py arguments]
# The whole I-cache measure for one build of the SNES core, on Linux or in WSL (arm-none-eabi-gcc, qemu-system-arm, gcc, python3):
#   1. plays SMW and SMW2 from the ROMs in $SFC (default the Desktop/snes (or sfc) folder) and records the instruction fetches of 10 frames of each,
#   2. lists the executed sections, takes the SNES ones as the movable set,
#   3. searches the order and the gaps of the movable sections (tests/snes/icache_layout.c) and writes the result to build/icache_$TAG/best.ld.
# The numbers: misses a frame of the model (see docs/progress/snes-cpu.md, "I-cache work"); "after the search" is what the searched layout gives on the
# first 2500000 runs of both traces. Copy best.ld into src/bm/snes_hot.ld (keep its header comment) when the numbers are good.
# Pad scripts: tests/snes/icache_pad_smw.txt and icache_pad_smw2.txt.
set -e
cd "$(dirname "$0")/.."
TAG=$1; shift
SFC=${SFC:-$(ls -d /mnt/c/Users/*/Desktop/snes ~/Desktop/snes /mnt/c/Users/*/Desktop/sfc ~/Desktop/sfc 2>/dev/null | head -1)}
mkdir -p build
OUT=build/icache_$TAG
python3 scripts/icache_sim.py "$SFC/smw.sfc" --warm 900 --frames 10 --pad tests/snes/icache_pad_smw.txt --out ${OUT}_smw "$@" > ${OUT}_smw.log 2>&1 &
python3 scripts/icache_sim.py "$SFC/smw2.smc" --warm 1200 --frames 10 --pad tests/snes/icache_pad_smw2.txt --out ${OUT}_smw2 "$@" > ${OUT}_smw2.log 2>&1 &
wait
grep -h '^frames' ${OUT}_smw.log ${OUT}_smw2.log
mkdir -p $OUT
gcc -O2 -o $OUT/icache_layout tests/snes/icache_layout.c -lm
python3 scripts/icache_units.py ${OUT}_smw/run.map > $OUT/units.txt
touch $OUT/empty.txt
LIST=1 $OUT/icache_layout $OUT/units.txt $OUT/empty.txt $OUT/x.ld ${OUT}_smw/trace.bin ${OUT}_smw2/trace.bin | sort -rn > $OUT/executed.txt
python3 scripts/icache_hotlist.py $OUT/executed.txt > $OUT/hot.txt
MAXRUNS=2500000 ITERS=${ITERS:-3000} $OUT/icache_layout $OUT/units.txt $OUT/hot.txt $OUT/best.ld ${OUT}_smw/trace.bin ${OUT}_smw2/trace.bin 2> $OUT/search.log
