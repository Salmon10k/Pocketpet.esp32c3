#!/usr/bin/env bash
# Builds the PC preview/test harness for the launcher menus and the new games (no ESP32 toolchain needed).
# Uses the real U8g2 library compiled for the PC, so the frames are pixel-identical to what the OLED buffer holds.
#   ./build.sh            -> builds /tmp/pocketpet-preview/preview
#   /tmp/pocketpet-preview/preview OUTDIR   -> writes raw 1024-byte frames + prints the self-test report
#   python3 ../fluidtest/render.py OUTDIR/star.bin star.png 4   -> contact sheet
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
LIBS="${LIBS:-/tmp/pocketpet-hostlibs}"
OUT="${OUT:-/tmp/pocketpet-preview}"
mkdir -p "$LIBS" "$OUT/obj"
[ -d "$LIBS/u8g2" ] || git clone -q --depth 1 https://github.com/olikraus/u8g2 "$LIBS/u8g2"
if [ ! -f "$OUT/obj/.done" ]; then
  ls "$LIBS"/u8g2/csrc/*.c | xargs -P 8 -I{} sh -c 'gcc -O1 -c -w -I'"$LIBS"'/u8g2/csrc {} -o '"$OUT"'/obj/$(basename {} .c).o'
  touch "$OUT/obj/.done"
fi
g++ -std=gnu++17 -O1 -w -I "$ROOT/src" -I "$HERE/../hostcheck" -I "$LIBS/u8g2/cppsrc" -I "$LIBS/u8g2/csrc" \
  "$HERE/preview.cpp" "$ROOT/src/gfx.cpp" "$ROOT/src/launcher.cpp" "$LIBS/u8g2/cppsrc/U8g2lib.cpp" "$LIBS/u8g2/cppsrc/U8x8lib.cpp" \
  "$HERE/stubs.cpp" "$OUT"/obj/*.o -o "$OUT/preview"
echo "built $OUT/preview"
