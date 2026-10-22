#!/usr/bin/env bash
# Assemble the ready-to-play mod folder and release zip.
#
#   scripts/package.sh [sdk-dir] [version]
#
# Takes whatever binaries are built in <sdk>/game/mod_hl2mp/bin/{linux64,x64}
# (CI drops both in). Output: dist/hl2dm_portal-<version>.zip containing
#   hl2dm_portal/   -> goes in Steam/steamapps/sourcemods/
#   install.sh, install.bat, README.txt
#   server/         -> adds the mod to an existing HL2DM dedicated server
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$(cd "${1:-$REPO_ROOT/sdk}" && pwd)"
VERSION="${2:-dev}"
MOD=hl2dm_portal
SRC="$SDK/game/mod_hl2mp"
STAGE="$REPO_ROOT/dist/stage"
OUT="$STAGE/$MOD"

rm -rf "$STAGE"
mkdir -p "$OUT/bin"

# Game content from the SDK's mod folder (scripts incl. weapon_portalgun.txt, cfg, maps...).
for d in cfg maps materials resource scripts shaders; do
    [[ -d "$SRC/$d" ]] && cp -r "$SRC/$d" "$OUT/"
done
sed 's/^ProductName=.*/ProductName=hl2dm_portal/' "$SRC/steam.inf" > "$OUT/steam.inf"
cp "$REPO_ROOT/packaging/gameinfo.txt" "$OUT/gameinfo.txt"
mkdir -p "$OUT/custom"

# Binaries, without debug info (168 MB -> 16 MB each).
platforms=()
if [[ -f "$SRC/bin/linux64/client.so" && -f "$SRC/bin/linux64/server.so" ]]; then
    mkdir -p "$OUT/bin/linux64"
    for f in client server; do
        strip --strip-debug -o "$OUT/bin/linux64/$f.so" "$SRC/bin/linux64/$f.so"
    done
    platforms+=(linux)
fi
if [[ -f "$SRC/bin/x64/client.dll" && -f "$SRC/bin/x64/server.dll" ]]; then
    mkdir -p "$OUT/bin/x64"
    cp "$SRC/bin/x64/client.dll" "$SRC/bin/x64/server.dll" "$OUT/bin/x64/"
    platforms+=(windows)
fi
if [[ ${#platforms[@]} -eq 0 ]]; then
    echo "error: no built binaries in $SRC/bin (run build_linux.sh, or the Windows build, first)"; exit 1
fi

cp "$REPO_ROOT/packaging/install.sh" "$REPO_ROOT/packaging/install.bat" "$STAGE/"

# Dedicated server kit.
SERVER="$STAGE/server"
mkdir -p "$SERVER"
cp "$REPO_ROOT"/packaging/server/{install_server.sh,install_server.bat,README_SERVER.txt} "$SERVER/"
# A server has no Steam client to resolve |appid_243750|; it uses its own folder.
sed 's/|appid_243750|/|all_source_engine_paths|/g' "$REPO_ROOT/packaging/gameinfo.txt" > "$SERVER/gameinfo_server.txt"
if [[ " ${platforms[*]} " == *" linux "* ]]; then
    # 64-bit launcher (CI builds it in the Steam Runtime; else build it here).
    if [[ -f "$SRC/bin/linux64/srcds_portal64" ]]; then
        cp "$SRC/bin/linux64/srcds_portal64" "$SERVER/"
    else
        cc -O2 -o "$SERVER/srcds_portal64" "$REPO_ROOT/packaging/server/srcds_portal64.c" -ldl
    fi
    chmod +x "$SERVER/srcds_portal64" "$SERVER/install_server.sh"
    # The server depot has no 64-bit libsteam_api.so; the SDK ships one.
    cp "$SDK/game/bin/linux64/libsteam_api.so" "$SERVER/"
fi
cat > "$STAGE/README.txt" <<TXT
HL2DM Portal $VERSION  (platforms: ${platforms[*]})

1. Install "Source SDK Base 2013 Multiplayer" from Steam (free): steam://install/243750
2. Windows: double-click install.bat
   Linux:   ./install.sh
   (or copy the hl2dm_portal folder into Steam/steamapps/sourcemods/ yourself)
3. Restart Steam and launch "HL2DM Portal" from your Library.

Running a server: see server/README_SERVER.txt. Players still need the mod
installed; a server can send maps and models, but never code.
Shared or no-admin PCs: install.bat asks for the Steam folder if it can't find
it, and puts an "HL2DM Portal" shortcut on the desktop.

Controls: M1 blue portal, M2 orange portal, R close both. Slot 1 weapon.
Source and docs: https://github.com/versitalai/hl2dm-portalmod
TXT

ZIP="$REPO_ROOT/dist/$MOD-$VERSION.zip"
rm -f "$ZIP"
(cd "$STAGE" && zip -qr "$ZIP" .)
echo ">> $ZIP ($(du -h "$ZIP" | cut -f1), ${platforms[*]})"
