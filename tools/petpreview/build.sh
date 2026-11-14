#!/usr/bin/env bash
# Builds the PC preview of the pet's music reactions (no ESP32 toolchain needed). Uses the real U8g2 library.
#   ./build.sh                          -> builds $OUT/petpreview
#   $OUT/petpreview OUTDIR              -> writes raw 1024-byte frames
#   python3 ../fluidtest/render.py OUTDIR/dance_3.bin dance_hype.png 4   -> contact sheet
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
LIBS="${LIBS:-/tmp/pocketpet-hostlibs}"
OUT="${OUT:-/tmp/pocketpet-petpreview-build}"
SAN="${SAN:-}"                                     # SAN="-fsanitize=address,undefined" for the torture run
mkdir -p "$LIBS" "$OUT/obj"
[ -d "$LIBS/u8g2" ] || git clone -q --depth 1 https://github.com/olikraus/u8g2 "$LIBS/u8g2"
if [ ! -f "$OUT/obj/.done" ]; then
  ls "$LIBS"/u8g2/csrc/*.c | xargs -P 8 -I{} sh -c 'gcc -O1 -c -w -I'"$LIBS"'/u8g2/csrc {} -o '"$OUT"'/obj/$(basename {} .c).o'
  touch "$OUT/obj/.done"
fi
g++ -std=gnu++17 -O1 -w $SAN -I "$ROOT/src" -I "$HERE/../hostcheck" -I "$LIBS/u8g2/cppsrc" -I "$LIBS/u8g2/csrc" \
  "$HERE/petpreview.cpp" "$ROOT/src/pet_face.cpp" "$ROOT/src/pet_music.cpp" "$ROOT/src/vibe.cpp" "$ROOT/src/gfx.cpp" \
  "$LIBS/u8g2/cppsrc/U8g2lib.cpp" "$LIBS/u8g2/cppsrc/U8x8lib.cpp" "$HERE/../gamepreview/stubs.cpp" "$OUT"/obj/*.o -o "$OUT/petpreview"
echo "built $OUT/petpreview"
