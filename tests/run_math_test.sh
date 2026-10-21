#!/usr/bin/env bash
# Build and run the portal transform test against the SDK's mathlib.
#   tests/run_math_test.sh [sdk-dir]
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$(cd "${1:-$REPO_ROOT/sdk}" && pwd)"
S="$SDK/src"
OUT="$(mktemp -d)"
g++ -std=c++11 -DGNUC -DPOSIX -D_LINUX -DLINUX -DPLATFORM_64BITS -DCOMPILER_GCC \
    -I"$S/public" -I"$S/public/tier0" -I"$S/public/tier1" \
    "$REPO_ROOT/tests/portal_math_test.cpp" "$S/lib/public/linux64/mathlib.a" \
    -L"$S/lib/public/linux64" -ltier0 -lvstdlib -Wl,-rpath,"$S/lib/public/linux64" -o "$OUT/portal_math_test"
"$OUT/portal_math_test"
