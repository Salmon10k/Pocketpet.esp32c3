#!/usr/bin/env bash
# Host-side SYNTAX check for the firmware (no ESP32 toolchain needed).
# Uses mock Arduino headers in this folder plus the real U8g2 / SoftWire /
# AsyncDelay / ArduinoJson headers cloned from GitHub into $LIBS.
# This catches typos, wrong types and wrong library API calls. It does NOT prove
# the firmware works on the board: only a real PlatformIO build + flash does.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
LIBS="${LIBS:-/tmp/pocketpet-hostlibs}"
mkdir -p "$LIBS"
clone() { [ -d "$LIBS/$2" ] || git clone -q --depth 1 "https://github.com/$1" "$LIBS/$2"; }
clone olikraus/u8g2 u8g2
clone stevemarple/SoftWire SoftWire
clone stevemarple/AsyncDelay AsyncDelay
clone bblanchon/ArduinoJson ArduinoJson
rc=0
for f in "$ROOT"/src/*.cpp; do
  echo "== $(basename "$f")"
  g++ -std=gnu++17 -fsyntax-only -Wall -Wextra -Wno-unused-parameter \
    -I "$ROOT/src" -I "$HERE" \
    -I "$LIBS/SoftWire/src" -I "$LIBS/AsyncDelay/src" \
    -I "$LIBS/u8g2/cppsrc" -I "$LIBS/u8g2/csrc" \
    -I "$LIBS/ArduinoJson/src" \
    "$f" || rc=1
done
[ $rc -eq 0 ] && echo "host syntax check: OK" || echo "host syntax check: FAILED"
exit $rc
