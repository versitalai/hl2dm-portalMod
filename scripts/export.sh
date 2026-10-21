#!/usr/bin/env bash
# Developer helper: after editing code inside an SDK tree made by setup.sh,
# write the changes back into this repo (patch + overlay).
#
#   scripts/export.sh [sdk-dir]
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$(cd "${1:-$REPO_ROOT/sdk}" && pwd)"

# New files live in overlay/, edits to stock SDK files in the patch.
OVERLAY_FILES=$(cd "$REPO_ROOT/overlay" && find . -type f | sed 's|^\./||')

for f in $OVERLAY_FILES; do
    cp "$SDK/$f" "$REPO_ROOT/overlay/$f"
done

# Stock files only: overlay files are untracked in the SDK checkout.
git -C "$SDK" diff -- src/game > "$REPO_ROOT/patches/hl2dm-portalmod.patch"

echo ">> Exported overlay ($(echo "$OVERLAY_FILES" | wc -l) files) and patch"
echo "   New files must be added to overlay/ by hand the first time."
