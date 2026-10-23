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

# The folder Steam lists mods from; it can differ from <Steam>/steamapps/sourcemods.
SOURCEMODS="$STEAM_DIR/steamapps/sourcemods"
for reg in "$STEAM_DIR/../registry.vdf" "$HOME/.steam/registry.vdf"; do
    if [[ -f "$reg" ]]; then
        p="$(sed -n 's/.*"SourceModInstallPath"[[:space:]]*"\([^"]*\)".*/\1/p' "$reg" | head -1)"
        if [[ -n "$p" && -d "$(dirname "$p")" ]]; then SOURCEMODS="$p"; fi
        break
    fi
done
DEST="$SOURCEMODS/$MOD"
mkdir -p "$SOURCEMODS"
if [[ ! -w "$SOURCEMODS" ]]; then
    echo "error: can't write to $SOURCEMODS (Steam lists mods from there)"; exit 1
fi

# Keep the player's own stuff (custom/, cfg/config.cfg) on reinstall.
if [[ -d "$DEST" ]]; then
    echo ">> Updating $DEST"
else
    echo ">> Installing to $DEST"
fi
mkdir -p "$DEST"
# Replace files instead of writing into them: a running game has the .so files
# mapped, and overwriting them in place can crash it.
cp -r --remove-destination "$HERE/$MOD/." "$DEST/"

# Source SDK Base 2013 Multiplayer can be in any Steam library.
SDKBASE=""
while IFS= read -r lib; do
    if [[ -f "$lib/steamapps/appmanifest_243750.acf" ]]; then SDKBASE="$lib"; break; fi
done < <(echo "$STEAM_DIR"; sed -n 's/.*"path"[[:space:]]*"\([^"]*\)".*/\1/p' "$STEAM_DIR/steamapps/libraryfolders.vdf" 2>/dev/null)
if [[ -z "$SDKBASE" ]]; then
    echo "!! 'Source SDK Base 2013 Multiplayer' doesn't look installed in this library."
    echo "   Install it: steam://install/243750"
fi

echo ">> Done. Restart Steam, then launch 'HL2DM Portal' from your Library."
