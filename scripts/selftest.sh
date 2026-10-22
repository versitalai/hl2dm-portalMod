#!/usr/bin/env bash
# Run the portal self-test on a real dedicated server.
#
#   scripts/selftest.sh <srcds-dir> [map]
#
# <srcds-dir>: HL2DM dedicated server (app 232370; same engine branch as the SDK), e.g.
#   steamcmd +force_install_dir ~/srcds +login anonymous +app_update 232370 +quit
# and the release built by scripts/package.sh (its server/install_server.sh sets the server up).
# Exit code 0 only if every PORTALTEST passed.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRCDS="$(cd "$1" && pwd)"
MAP="${2:-dm_lockdown}"
LOG="$SRCDS/portal_selftest.log"

# Set the server up exactly as a server owner would: the release's server kit
# (scripts/package.sh output, default dist/stage).
PKG="${PKG:-$REPO_ROOT/dist/stage}"
"$PKG/server/install_server.sh" "$SRCDS" || exit 1

cd "$SRCDS"
export LD_LIBRARY_PATH="$SRCDS/bin/linux64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

echo ">> srcds_portal64 -game hl2dm_portal +map $MAP"
ARGS=( -game hl2dm_portal -console -condebug -norestart -nobreakpad -insecure
    +sv_lan 1 +maxplayers 4 +sv_portal_selftest 1 +map "$MAP" )
if command -v gdb > /dev/null; then
    # Under gdb so a crash prints a backtrace into the log.
    timeout 300 gdb -batch -ex "set pagination off" -ex run -ex bt -ex "info sharedlibrary" \
        --args ./srcds_portal64 "${ARGS[@]}" > "$LOG" 2>&1
else
    timeout 300 stdbuf -oL -eL ./srcds_portal64 "${ARGS[@]}" > "$LOG" 2>&1
fi
echo ">> server exited ($?)"

grep -E "PORTALTEST|SIGSEGV|Segmentation|crash|Couldn't|Failed to load|error|^#[0-9]" "$LOG" | head -80 || true

# Engine console output (-condebug).
CONLOG="$SRCDS/hl2dm_portal/console.log"
if [[ -f "$CONLOG" ]]; then
    echo ">> console.log (last 60 lines):"
    tail -60 "$CONLOG"
    cat "$CONLOG" >> "$LOG"
fi

if ! grep -q "PORTALTEST DONE" "$LOG"; then
    echo ">> self-test did not finish. Last 80 lines:"
    tail -80 "$LOG"
    exit 1
fi
grep -q "PORTALTEST DONE pass=[0-9]* fail=0" "$LOG"
