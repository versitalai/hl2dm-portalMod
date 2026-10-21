#!/usr/bin/env bash
# Assemble the ready-to-play mod folder and release zip.
#
#   scripts/package.sh [sdk-dir] [version]
#
# Takes whatever binaries are built in <sdk>/game/mod_hl2mp/bin/{linux64,x64}
# (CI drops both in). Output: dist/hl2dm_portal-<version>.zip containing
#   hl2dm_portal/   -> goes in Steam/steamapps/sourcemods/
#   install.sh, install.bat, README.txt
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
cat > "$STAGE/README.txt" <<TXT
HL2DM Portal $VERSION  (platforms: ${platforms[*]})

1. Install "Source SDK Base 2013 Multiplayer" from Steam (free): steam://install/243750
2. Windows: double-click install.bat
   Linux:   ./install.sh
   (or copy the hl2dm_portal folder into Steam/steamapps/sourcemods/ yourself)
3. Restart Steam and launch "HL2DM Portal" from your Library.

Controls: M1 blue portal, M2 orange portal, R close both. Slot 1 weapon.
Source and docs: https://github.com/versitalai/hl2dm-portalmod
TXT

ZIP="$REPO_ROOT/dist/$MOD-$VERSION.zip"
rm -f "$ZIP"
(cd "$STAGE" && zip -qr "$ZIP" .)
echo ">> $ZIP ($(du -h "$ZIP" | cut -f1), ${platforms[*]})"
