#!/usr/bin/env bash
# Add HL2DM Portal to an existing HL2DM dedicated server (Linux, Steam app 232370).
#
#   server/install_server.sh /path/to/server [--port 27016] [--map dm_lockdown] [--maxplayers 16]
#
# Run from the unzipped release. The normal HL2DM server keeps working: this adds
# the hl2dm_portal folder and writes start_portal_server.sh, which runs a second
# server (default port 27016) next to it. Run it again to update.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PKG="$HERE/.."
MOD=hl2dm_portal
PORT=""
MAP=""
MAXPLAYERS=""
SRV=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) PORT="$2"; shift 2 ;;
        --map) MAP="$2"; shift 2 ;;
        --maxplayers) MAXPLAYERS="$2"; shift 2 ;;
        -*) echo "Unknown option: $1"; exit 1 ;;
        *) SRV="$1"; shift ;;
    esac
done

if [[ -z "$SRV" ]]; then
    echo "usage: $0 /path/to/hl2dm-server [--port 27016] [--map dm_lockdown] [--maxplayers 16]"; exit 1
fi
SRV="$(cd "$SRV" && pwd)"
if [[ ! -d "$SRV/hl2mp" ]]; then
    echo "error: $SRV doesn't look like an HL2DM dedicated server (no hl2mp folder)"; exit 1
fi
if [[ ! -f "$SRV/bin/linux64/dedicated_srv.so" ]]; then
    echo "error: no 64-bit server files in $SRV/bin/linux64. Update the server first:"
    echo "  steamcmd +force_install_dir \"$SRV\" +login anonymous +app_update 232370 validate +quit"
    exit 1
fi
if [[ ! -f "$PKG/$MOD/bin/linux64/server.so" ]]; then
    echo "error: $PKG/$MOD/bin/linux64/server.so not found (run this from the unzipped release)"; exit 1
fi

# Updating: keep the port/map/players from the existing start script.
START="$SRV/start_portal_server.sh"
if [[ -f "$START" ]]; then
    [[ -n "$PORT" ]] || PORT="$(sed -n 's/.* -port \([0-9]*\).*/\1/p' "$START")"
    [[ -n "$MAP" ]] || MAP="$(sed -n 's/.* +map \([^ ]*\) .*/\1/p' "$START")"
    [[ -n "$MAXPLAYERS" ]] || MAXPLAYERS="$(sed -n 's/.* +maxplayers \([0-9]*\).*/\1/p' "$START")"
fi
PORT="${PORT:-27016}"; MAP="${MAP:-dm_lockdown}"; MAXPLAYERS="${MAXPLAYERS:-16}"

DEST="$SRV/$MOD"
echo ">> Installing the mod to $DEST"
# Keep the server's own config across updates.
KEEP="$(mktemp -d)"
for f in cfg/server.cfg cfg/mapcycle.txt cfg/motd.txt; do
    if [[ -f "$DEST/$f" ]]; then mkdir -p "$KEEP/$(dirname "$f")"; cp "$DEST/$f" "$KEEP/$f"; fi
done
mkdir -p "$DEST"
# Replace files instead of writing into them: a running server has the .so files mapped.
cp -r --remove-destination "$PKG/$MOD/." "$DEST/"
cp -r "$KEEP/." "$DEST/" && rm -rf "$KEEP"

# No Steam client on a server to resolve |appid_243750|; it finds the HL2 and
# HL2DM content in its own folder instead.
cp "$HERE/gameinfo_server.txt" "$DEST/gameinfo.txt"
# The 64-bit dedicated server loads the game DLL as server_srv.so.
ln -sf server.so "$DEST/bin/linux64/server_srv.so"

# The depot's srcds_linux is 32-bit; this is the 64-bit launcher.
cp --remove-destination "$HERE/srcds_portal64" "$SRV/srcds_portal64"
chmod +x "$SRV/srcds_portal64"
# The depot has no 64-bit libsteam_api.so; the SDK's goes next to the engine.
[[ -f "$SRV/bin/linux64/libsteam_api.so" ]] || cp "$HERE/libsteam_api.so" "$SRV/bin/linux64/"
# The mod's server.so is built against the engine modules' plain names
# (libtier0.so, soundemittersystem.so...); the server ships them as *_srv.so.
# Extra links only: the stock server keeps loading the *_srv.so files.
for f in "$SRV"/bin/linux64/*_srv.so; do
    plain="${f%_srv.so}.so"
    [[ -e "$plain" ]] || ln -s "$(basename "$f")" "$plain"
done

# The engine loads the 64-bit steamclient.so from ~/.steam/sdk64.
if [[ ! -f "$HOME/.steam/sdk64/steamclient.so" ]]; then
    for c in "$SRV/linux64/steamclient.so" "$HOME/steamcmd/linux64/steamclient.so" "$HOME/Steam/linux64/steamclient.so" \
             "$HOME/.steam/steamcmd/linux64/steamclient.so" "$HOME/.local/share/Steam/linux64/steamclient.so" \
             "/usr/games/steamcmd/linux64/steamclient.so"; do
        if [[ -f "$c" ]]; then
            mkdir -p "$HOME/.steam/sdk64"
            cp "$c" "$HOME/.steam/sdk64/steamclient.so"
            break
        fi
    done
    if [[ ! -f "$HOME/.steam/sdk64/steamclient.so" ]]; then
        echo "!! No 64-bit steamclient.so found. Copy linux64/steamclient.so from your steamcmd folder"
        echo "   to $HOME/.steam/sdk64/ (the server won't start without it)."
    fi
fi

cat > "$START" <<START
#!/usr/bin/env bash
# HL2DM Portal server. Extra arguments go to the server, e.g. +hostname "Portals" +sv_password x
cd "\$(dirname "\$0")"
export LD_LIBRARY_PATH="\$PWD/bin/linux64\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
exec ./srcds_portal64 -game $MOD -console -port $PORT +maxplayers $MAXPLAYERS +map $MAP "\$@"
START
chmod +x "$START"

echo ">> Done. Start it with: $START"
echo "   Players connect to <this server's address>:$PORT (open UDP $PORT in the firewall)."
echo "   Server settings go in $DEST/cfg/server.cfg."
