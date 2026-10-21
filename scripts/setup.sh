#!/usr/bin/env bash
# Fetch Source SDK 2013 at the pinned commit and apply the portal mod to it.
#
#   scripts/setup.sh [target-dir]     (default: ./sdk)
#
# Result: a full SDK tree with the mod applied, ready for build_linux.sh
# (or createallprojects.bat on Windows).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK_URL="https://github.com/ValveSoftware/source-sdk-2013.git"
SDK_COMMIT="$(cat "$REPO_ROOT/SDK_COMMIT")"
TARGET="${1:-$REPO_ROOT/sdk}"

if [[ ! -d "$TARGET/.git" ]]; then
    echo ">> Fetching source-sdk-2013 @ $SDK_COMMIT into $TARGET"
    mkdir -p "$TARGET"
    git -C "$TARGET" init -q
    git -C "$TARGET" remote add origin "$SDK_URL"
    git -C "$TARGET" fetch -q --depth 1 origin "$SDK_COMMIT"
    # LF checkout so the patch applies on Windows (core.autocrlf) too.
    git -C "$TARGET" -c core.autocrlf=false checkout -q FETCH_HEAD
fi

if git -C "$TARGET" apply --reverse --check "$REPO_ROOT/patches/hl2dm-portalmod.patch" 2>/dev/null; then
    echo ">> Patch already applied"
else
    echo ">> Applying patches/hl2dm-portalmod.patch"
    git -C "$TARGET" apply "$REPO_ROOT/patches/hl2dm-portalmod.patch"
fi

echo ">> Copying overlay (new files)"
cp -r "$REPO_ROOT/overlay/." "$TARGET/"

echo ">> Done. Build with: scripts/build_linux.sh $TARGET"
