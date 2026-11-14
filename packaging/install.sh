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

FLATPAK_HOME="$HOME/.var/app/com.valvesoftware.Steam"

# Libraries of a Steam root (itself + libraryfolders.vdf entries).
libraries() {
    echo "$1"
    sed -n 's/.*"path"[[:space:]]*"\([^"]*\)".*/\1/p' "$1/steamapps/libraryfolders.vdf" 2>/dev/null
}
has_sdkbase() {
    local lib
    while IFS= read -r lib; do
        [[ -f "$lib/steamapps/appmanifest_243750.acf" ]] && return 0
    done < <(libraries "$1")
    return 1
}
# Steam writes config.vdf while running; the newest one is the Steam in use.
mtime() { stat -c %Y "$1/config/config.vdf" 2>/dev/null || echo 0; }

# Native, Flatpak and older symlinked Steam installs. Several can exist (a leftover
# native folder next to a Flatpak, or the other way round): installing into the
# one you don't run is why a mod "installs fine" but never shows in the Library.
# Prefer the one that has Source SDK Base 2013 MP, then the most recently used.
if [[ -z "$STEAM_DIR" ]]; then
    declare -A seen=()
    best=""; best_score=-1; found=()
    for d in "$HOME/.local/share/Steam" "$HOME/.steam/steam" "$HOME/.steam/root" \
             "$FLATPAK_HOME/.local/share/Steam" "$FLATPAK_HOME/.steam/steam"; do
        [[ -d "$d/steamapps" ]] || continue
        r="$(readlink -f "$d")"
        [[ -n "${seen[$r]:-}" ]] && continue
        seen[$r]=1; found+=("$r")
        score="$(mtime "$r")"
        has_sdkbase "$r" && score=$((score + 10000000000))
        if (( score > best_score )); then best="$r"; best_score=$score; fi
    done
    STEAM_DIR="$best"
    if (( ${#found[@]} > 1 )); then
        echo ">> Found ${#found[@]} Steam installs: ${found[*]}"
        echo "   Using $STEAM_DIR (override with --steam-dir)"
    fi
fi
if [[ -z "$STEAM_DIR" || ! -d "$STEAM_DIR/steamapps" ]]; then
    echo "error: Steam not found. Pass it: ./install.sh --steam-dir /path/to/Steam"; exit 1
fi
STEAM_DIR="$(readlink -f "$STEAM_DIR")"

# The folder Steam lists mods from; it can differ from <Steam>/steamapps/sourcemods.
# registry.vdf lives in ~/.steam (native) or the Flatpak's own ~/.steam, never
# next to the Steam folder; read the one belonging to the Steam picked above.
SOURCEMODS="$STEAM_DIR/steamapps/sourcemods"
case "$STEAM_DIR" in
    "$(readlink -f "$FLATPAK_HOME" 2>/dev/null || echo "$FLATPAK_HOME")"/*) REG="$FLATPAK_HOME/.steam/registry.vdf" ;;
    *) REG="$HOME/.steam/registry.vdf" ;;
esac
if [[ -f "$REG" ]]; then
    p="$(sed -n 's/.*"SourceModInstallPath"[[:space:]]*"\([^"]*\)".*/\1/p' "$REG" | head -1)"
    # Older Steam builds store it with Windows-style escaped backslashes.
    p="${p//\\\\//}"
    if [[ -n "$p" && -d "$(dirname "$p")" ]]; then SOURCEMODS="$p"; fi
fi
echo ">> Steam: $STEAM_DIR"
echo ">> Steam lists mods from: $SOURCEMODS"
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
if ! has_sdkbase "$STEAM_DIR"; then
    echo "!! 'Source SDK Base 2013 Multiplayer' isn't installed in this Steam. Steam hides"
    echo "   mods whose base game is missing. Install it: steam://install/243750"
fi

# Steam scans sourcemods only at startup; closing the window leaves it running.
echo ">> Done. Fully quit Steam (steam -shutdown, or Steam > Exit), start it again,"
echo "   then launch 'HL2DM Portal' from your Library."
