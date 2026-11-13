#!/bin/sh
# build_m7test.sh: assemble tests/snes/m7test/main.s into build/m7test.sfc with the bm SNES SDK assembler (core/src/script/snes_asm.lua).
#   tests/snes/build_m7test.sh [out.sfc]      run it from the repository root (WSL or Linux; core/scripts/wsl-run.ps1 "tests/snes/build_m7test.sh")
# The ROM stays out of git. Copy it to the snes folder of the SD card.
set -e
cd "$(dirname "$0")/../.."
OUT="${1:-build/m7test.sfc}"
[ -x build/host/luahost ] || make build/host/luahost
mkdir -p "$(dirname "$OUT")"
build/host/luahost tests/snes/asm_cli.lua tests/snes/m7test/main.s "$OUT" --title "BM M7 TEST" --sym "${OUT%.sfc}.sym"
