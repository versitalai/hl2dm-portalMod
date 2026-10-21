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

SRCDS="$(cd "$1" && pwd)"
MAP="${2:-dm_lockdown}"
LOG="$SRCDS/portal_selftest.log"

cd "$SRCDS"
LAUNCHER=""
for l in srcds_run_64 srcds_run; do
    [[ -x "$SRCDS/$l" ]] && { LAUNCHER="$l"; break; }
done
[[ -n "$LAUNCHER" ]] || { echo "error: no srcds launcher in $SRCDS"; ls "$SRCDS"; exit 1; }

echo ">> $LAUNCHER -game hl2dm_portal +map $MAP"
timeout 300 "./$LAUNCHER" -game hl2dm_portal -console -norestart -nobreakpad \
    +sv_lan 1 +maxplayers 4 +sv_portal_selftest 1 +path +map "$MAP" > "$LOG" 2>&1
echo ">> server exited ($?)"

grep -E "PORTALTEST|Segmentation|crash|Couldn't|Failed to load|error" "$LOG" | head -60 || true

if ! grep -q "PORTALTEST DONE" "$LOG"; then
    echo ">> self-test did not finish. Last 80 lines:"
    tail -80 "$LOG"
    exit 1
fi
grep -q "PORTALTEST DONE pass=[0-9]* fail=0" "$LOG"
