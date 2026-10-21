#!/usr/bin/env bash
# One command from source to playable (Linux): fetch SDK, build, package, install.
#
#   scripts/install.sh [--steam-dir DIR]
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

"$REPO_ROOT/scripts/setup.sh"
"$REPO_ROOT/scripts/build_linux.sh"
"$REPO_ROOT/scripts/package.sh" "$REPO_ROOT/sdk" "local"
"$REPO_ROOT/dist/stage/install.sh" "$@"
