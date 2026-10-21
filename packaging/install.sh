#!/usr/bin/env bash
# Install HL2DM Portal into Steam's sourcemods folder (Linux).
# Run from the unzipped release:   ./install.sh  [--steam-dir DIR]
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
MOD=hl2dm_portal
STEAM_DIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --steam-dir) STEAM_DIR="$2"; shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

if [[ ! -f "$HERE/$MOD/gameinfo.txt" ]]; then
    echo "error: $HERE/$MOD not found (run this from the unzipped release)"; exit 1
fi

# Native, Flatpak and older symlinked Steam installs.
if [[ -z "$STEAM_DIR" ]]; then
    for d in "$HOME/.local/share/Steam" "$HOME/.steam/steam" "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"; do
        if [[ -d "$d/steamapps" ]]; then STEAM_DIR="$d"; break; fi
    done
fi
if [[ -z "$STEAM_DIR" || ! -d "$STEAM_DIR/steamapps" ]]; then
    echo "error: Steam not found. Pass it: ./install.sh --steam-dir /path/to/Steam"; exit 1
fi

DEST="$STEAM_DIR/steamapps/sourcemods/$MOD"
mkdir -p "$STEAM_DIR/steamapps/sourcemods"

# Keep the player's own stuff (custom/, cfg/config.cfg) on reinstall.
if [[ -d "$DEST" ]]; then
    echo ">> Updating $DEST"
else
    echo ">> Installing to $DEST"
fi
mkdir -p "$DEST"
cp -r "$HERE/$MOD/." "$DEST/"

if ! ls "$STEAM_DIR"/steamapps/appmanifest_243750.acf >/dev/null 2>&1; then
    echo "!! 'Source SDK Base 2013 Multiplayer' doesn't look installed in this library."
    echo "   Install it: steam://install/243750"
fi

echo ">> Done. Restart Steam, then launch 'HL2DM Portal' from your Library."
