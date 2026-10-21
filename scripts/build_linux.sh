#!/usr/bin/env bash
# Build the HL2MP server.so + client.so natively (host gcc, no podman/Steam Runtime).
#
#   scripts/build_linux.sh [sdk-dir] [release|debug]
#
# Verified with Ubuntu 24.04 gcc 13. Binaries for distribution should be built
# with the SDK's own src/buildallprojects (Steam Runtime "sniper" container).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$(cd "${1:-$REPO_ROOT/sdk}" && pwd)"
MODE="${2:-release}"

cd "$SDK/src"
export VPC_NINJA_BUILD_MODE="$MODE"
SLN="_vpc_/ninja/sdk_hl2mp_$MODE"

# Regenerate when any .vpc is newer than the ninja file.
if [[ ! -e "$SLN.ninja" ]] || [[ -n "$(find . -name '*.vpc' -newer "$SLN.ninja" -print -quit)" ]]; then
    devtools/bin/vpc /hl2mp /linux64 /ninja /define:SOURCESDK +everything /mksln "$SLN"
fi

ninja -f "$SLN.ninja" -j"$(nproc)" \
    ../game/mod_hl2mp/bin/linux64/server.so \
    ../game/mod_hl2mp/bin/linux64/client.so

echo ">> Built: $SDK/game/mod_hl2mp/bin/linux64/{server,client}.so"
