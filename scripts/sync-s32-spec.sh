#!/bin/sh
# Copies the shared s32 spec and conformance vectors from a lua32 checkout
# into spec/s32 and records the source commit.
# Usage: scripts/sync-s32-spec.sh /path/to/lua32
set -eu

SRC=${1:?usage: $0 /path/to/lua32}
DST=spec/s32

test -f "$SRC/docs/spec/s32-spec.md" || { echo "no docs/spec in $SRC"; exit 1; }
mkdir -p "$DST/conformance"
cp "$SRC/docs/spec/s32-spec.md" "$DST/"
cp "$SRC"/docs/spec/conformance/*.cart "$SRC"/docs/spec/conformance/*.vec "$DST/conformance/"
printf 'lua32 %s @ %s\n' "$(git -C "$SRC" branch --show-current)" \
    "$(git -C "$SRC" rev-parse --short HEAD)" > "$DST/SOURCE"
cat "$DST/SOURCE"
echo "now run: make test-s32"
