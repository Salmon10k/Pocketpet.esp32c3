#!/usr/bin/env bash
# Builds and runs the PC test for src/vibe.cpp (no ESP32 toolchain needed).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${OUT:-/tmp/pocketpet-vibetest}"
mkdir -p "$OUT"
g++ -std=gnu++17 -O1 -Wall -Wextra -fsanitize=address,undefined "$HERE/vibetest.cpp" "$HERE/../../src/vibe.cpp" -o "$OUT/vibetest"
"$OUT/vibetest"
