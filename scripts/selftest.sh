#!/usr/bin/env bash
# Run the portal self-test on a real dedicated server.
#
#   scripts/selftest.sh <srcds-dir> [map]
#
# <srcds-dir>: "Source SDK Base 2013 Dedicated Server" (app 244310), e.g.
#   steamcmd +force_install_dir ~/srcds +login anonymous +app_update 244310 +quit
# with the packaged mod copied in as <srcds-dir>/hl2dm_portal (scripts/package.sh).
# Exit code 0 only if every PORTALTEST passed.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRCDS="$(cd "$1" && pwd)"
MAP="${2:-dm_lockdown}"
LOG="$SRCDS/portal_selftest.log"

cd "$SRCDS"

# The 2013 dedicated server has 64-bit engine libraries but only a 32-bit
# launcher, so use our own 64-bit one (tests/srcds64.c).
if [[ ! -x "$SRCDS/srcds64" ]]; then
    cc -O2 -o "$SRCDS/srcds64" "$REPO_ROOT/tests/srcds64.c" -ldl || exit 1
fi
# It also lacks a 64-bit libsteam_api.so; the SDK ships one.
if [[ ! -f "$SRCDS/bin/linux64/libsteam_api.so" ]]; then
    cp "$REPO_ROOT/sdk/game/bin/linux64/libsteam_api.so" "$SRCDS/bin/linux64/" || exit 1
fi
# 64-bit steamclient.so from steamcmd, where the engine looks for it.
if [[ ! -f "$HOME/.steam/sdk64/steamclient.so" && -f "$HOME/steamcmd/linux64/steamclient.so" ]]; then
    mkdir -p "$HOME/.steam/sdk64"
    cp "$HOME/steamcmd/linux64/steamclient.so" "$HOME/.steam/sdk64/"
fi
export LD_LIBRARY_PATH="$SRCDS/bin/linux64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

echo ">> srcds64 -game hl2dm_portal +map $MAP"
timeout 300 ./srcds64 -game hl2dm_portal -console -norestart -nobreakpad -insecure \
    +sv_lan 1 +maxplayers 4 +sv_portal_selftest 1 +path +map "$MAP" > "$LOG" 2>&1
echo ">> server exited ($?)"

grep -E "PORTALTEST|Segmentation|crash|Couldn't|Failed to load|error" "$LOG" | head -60 || true

if ! grep -q "PORTALTEST DONE" "$LOG"; then
    echo ">> self-test did not finish. Last 80 lines:"
    tail -80 "$LOG"
    exit 1
fi
grep -q "PORTALTEST DONE pass=[0-9]* fail=0" "$LOG"
