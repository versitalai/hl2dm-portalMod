#!/usr/bin/env bash
# Build and run the convex carve test (hl2mp_portal_carve.h) against the SDK's mathlib.
#   tests/run_carve_test.sh [sdk-dir]
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$(cd "${1:-$REPO_ROOT/sdk}" && pwd)"
S="$SDK/src"
OUT="$(mktemp -d)"
g++ -std=c++11 -DGNUC -DPOSIX -D_LINUX -DLINUX -DPLATFORM_64BITS -DCOMPILER_GCC \
    -I"$S/public" -I"$S/public/tier0" -I"$S/public/tier1" -I"$S/game/shared/hl2mp" \
    "$REPO_ROOT/tests/portal_carve_test.cpp" "$S/lib/public/linux64/mathlib.a" \
    -L"$S/lib/public/linux64" -ltier0 -lvstdlib -Wl,-rpath,"$S/lib/public/linux64" -o "$OUT/portal_carve_test"
"$OUT/portal_carve_test"
