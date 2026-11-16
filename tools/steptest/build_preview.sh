#!/usr/bin/env bash
# Renders the step counter screens (Stats "STEPS" page, walking / goal pet) on the PC with the real U8g2 library.
#   bash tools/steptest/build_preview.sh [OUTDIR]  -> frames in OUTDIR (raw 1024-byte), then
#   python3 tools/fluidtest/render.py OUTDIR/page.bin page.png 4
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
LIBS="${LIBS:-/tmp/pocketpet-hostlibs}"
OUT="${OUT:-/tmp/pocketpet-stepview-build}"
SAN="${SAN:-}"
mkdir -p "$LIBS" "$OUT/obj"
[ -d "$LIBS/u8g2" ] || git clone -q --depth 1 https://github.com/olikraus/u8g2 "$LIBS/u8g2"
if [ ! -f "$OUT/obj/.done" ]; then
  ls "$LIBS"/u8g2/csrc/*.c | xargs -P 8 -I{} sh -c 'gcc -O1 -c -w -I'"$LIBS"'/u8g2/csrc {} -o '"$OUT"'/obj/$(basename {} .c).o'
  touch "$OUT/obj/.done"
fi
g++ -std=gnu++17 -O1 -w $SAN -I "$ROOT/src" -I "$HERE/../hostcheck" -I "$LIBS/u8g2/cppsrc" -I "$LIBS/u8g2/csrc" \
  "$HERE/preview.cpp" "$ROOT/src/steps.cpp" "$ROOT/src/steps_view.cpp" "$ROOT/src/pet_face.cpp" "$ROOT/src/gfx.cpp" \
  "$LIBS/u8g2/cppsrc/U8g2lib.cpp" "$LIBS/u8g2/cppsrc/U8x8lib.cpp" "$HERE/../gamepreview/stubs.cpp" "$OUT"/obj/*.o -o "$OUT/steppreview"
mkdir -p "${1:-/tmp/pocketpet-stepview}"
"$OUT/steppreview" "${1:-/tmp/pocketpet-stepview}"
