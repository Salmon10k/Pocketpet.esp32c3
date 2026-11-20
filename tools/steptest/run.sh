#!/usr/bin/env bash
# Builds and runs the PC test for src/steps.cpp (no ESP32 toolchain needed).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${OUT:-/tmp/pocketpet-steptest}"
mkdir -p "$OUT"
g++ -std=gnu++17 -O1 -Wall -Wextra -fsanitize=address,undefined "$HERE/steptest.cpp" "$HERE/../../src/steps.cpp" "$HERE/../../src/progress.cpp" -o "$OUT/steptest"
"$OUT/steptest"
