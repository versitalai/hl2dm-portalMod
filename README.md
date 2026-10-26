# HL2DM Portal Mod

Portal gun for Half-Life 2: Deathmatch, built on Valve's
[Source SDK 2013](https://github.com/ValveSoftware/source-sdk-2013) (HL2DM = the SDK's `hl2mp` game).
Every player spawns with a portal gun. Shoot a blue and an orange portal on any world surface.
Anything that moves goes through them. You see through them live.

## Install (players)

1. Install **Source SDK Base 2013 Multiplayer** from Steam (free): `steam://install/243750`
2. Download `hl2dm_portal-<version>.zip` from [Releases](https://github.com/versitalai/hl2dm-portalmod/releases)
   (or the `hl2dm_portal` artifact of the latest [build](https://github.com/versitalai/hl2dm-portalmod/actions) run) and unzip it.
3. **Windows:** double-click `install.bat`. No admin needed. It finds Steam through Steam's own per-user settings
   (including where Steam lists mods from, `SourceModInstallPath`), the running Steam, the usual folders, Steam
   shortcuts and a search of all drives, so Steam can be anywhere. Only if all that fails does it ask you to pick the
   Steam folder. It puts an **HL2DM Portal** shortcut on the desktop.
   **Linux:** `./install.sh` (native and Flatpak Steam; `--steam-dir DIR` otherwise).
   Or copy the `hl2dm_portal` folder into Steam's `steamapps/sourcemods/` yourself.
4. Launch it from the desktop shortcut, or restart Steam and launch **HL2DM Portal** from your Library.

**Updates (Windows):** the desktop shortcuts check for a newer release every time you start the game and install it
by themselves (one quick request; a download only when there's a new version). `hl2dm_portal\update.bat` checks by
hand; a file named `no_auto_update.txt` next to it turns the automatic check off. Joining a server that runs another
release shows a warning in chat.

Normal HL2DM is untouched: play regular servers with HL2DM, portal servers with HL2DM Portal.
Servers can't install the mod for players (a server can send maps and models, never code).

**Sharing it with your server's players:** put your server's address (e.g. `203.0.113.5:27016`) as the only line of
a `server.txt` next to `install.bat` before you share the folder. The installer then also makes a
**Join HL2DM Portal server** desktop shortcut.

From source on Linux, one command does it all (fetch SDK, build, package, install): `scripts/install.sh`

## What goes through

| Thing | How |
|---|---|
| Players | Position, view angles, velocity carried through (walk, fall, fling) |
| Physics props, dropped weapons, items | Position, orientation, linear + angular velocity. Props that fit the hole slide through a piece at a time (the part through is drawn coming out of the other portal) |
| Projectiles: rockets, crossbow bolts, SMG grenades, frags, AR2 energy balls, SLAM satchels | Caught up to `sv_portal_lookahead` ticks early, so fast ones don't hit the wall first |
| Bullets (all hitscan weapons, shotgun pellets) | Trace continues out of the linked portal (up to 4 portals deep), with impacts and tracers on the far side |
| Portal gun shots | Go through existing portals too, so you can place a portal through a portal |
| SLAM tripmine laser | Beam continues through portals (drawn on every segment). Anyone crossing any segment trips it; opening, closing or moving a portal on its path trips it too |

Props you're holding (gravity gun or `+use`) stay held through portals, from either side. Walk into a wall or floor
portal and you go through in one smooth move (no snap) when your eye crosses it; a little off-center, you slide into line
with the hole on the way in (funneling). Melee weapons (crowbar, stunstick) reach through portals too, and so do
explosions. A portal whose exit is blocked (something solid right in front of the other one) is just a wall or floor. Props with
motion disabled, and things stuck to walls (a placed tripmine), stay put.

## See-through rendering

The client renders the scene from a virtual camera behind the exit portal into a render target, every frame, then maps
it onto the portal surface. You see live action through portals, and your own player model in them. Through-portal
recursion (a portal seen inside a portal view) draws as a solid tinted portal.

## Multiplayer

Built for it: HL2DM is multiplayer-only, and everything runs server-authoritative.
- Every player has their own blue/orange pair. Anyone can use anyone's portals.
- Portals are always networked to every client (`FL_EDICT_ALWAYS`), so each client can draw any portal and its view.
- Walking through a portal is part of player movement, so it's client-predicted: no snap-back with latency. Everything
  else teleports on the server and replicates like `trigger_teleport`.
- Bullets are traced through portals the same way on server and client, so impacts line up.
- Each player's PVS gets the exit side of portals they can see, so players/props over there are sent and show up in
  the portal view.
- Each client renders its own portal views (up to 2 per frame).

## Controls

| Input | Action |
|---|---|
| `+attack` (M1) | Blue portal |
| `+attack2` (M2) | Orange portal |
| `+reload` (R) | Close both portals |

The gun is in weapon slot 1 (with crowbar / gravity gun). Portals are removed when the gun is removed (e.g. on death).

## Graphics settings (slow PCs)

Seeing through a portal means drawing the scene again, so portals are the mod's main cost. Pick a quality in
**Portal Settings** in the main menu (every option explained there), the `portal_settings` console command, or with
`cl_portal_quality` in the console:

| `cl_portal_quality` | Live views | Portal-in-portal | View resolution | Solid if smaller than |
|---|---|---|---|---|
| 0 Off | none (solid tinted portals) | | | |
| 1 Low | 1 | no | 50% | 12% of the screen |
| 2 Medium | 2 | no | 75% | 6% |
| 3 High | 2 | yes | 100% | always live |
| **4 Auto** (default) | moves between these one step at a time to hold `cl_portal_auto_fps` (60) | | | |

At every level only the part of a view the portal covers on screen is drawn, and water seen through a portal is
drawn cheap (except on High). For the whole game, start once with the **HL2DM Portal (low graphics)** desktop shortcut
(or `exec potato` in the console): lowest textures, models, shadows, water, effects, and auto portals at 45 fps. It's
saved. On a weak GPU, a lower screen resolution (Options > Video) helps most.

## ConVars

| ConVar | Default | Side | Meaning |
|---|---|---|---|
| `cl_portal_quality` | 4 | client | 0 off, 1 low, 2 medium, 3 high, 4 auto (see above) |
| `cl_portal_auto_fps` | 60 | client | Frame rate auto quality tries to hold |
| `cl_portal_resolution` | 1 | client | Cap on portal view resolution (0.25-1 of the screen) |
| `cl_portal_scissor` | 1 | client | Only render the part of a view the portal covers |
| `cl_portal_quality_debug` | 0 | client | Print auto quality changes |
| `cl_portal_render_views` | 1 | client | Render live views through portals (0 = solid color) |
| `cl_portal_max_views` | 2 | client | Cap on portals rendered see-through per frame, closest first (max 2) |
| `cl_portal_recursion` | 1 | client | Allow live portal-in-portal views (High only) |
| `cl_portal_draw_local_player` | 1 | client | Draw your own model in portal views |
| `sv_portal_lookahead` | 3 | server | Ticks of lookahead for fast movers |
| `sv_portal_smooth_props` | 1 | server | Physics props that fit the hole slide through (0 = they pop through whole) |
| `cl_portal_prop_clones` | 1 | client | Draw the part of a sliding prop that's come out of the other portal |
| `sv_portal_cooldown` | 0.05 | server | Seconds before an entity can go through again |
| `sv_portal_debug` | 0 | server (cheat) | Draw detection volumes, placement traces, exit boxes |
| `sv_portal_debug_moves` | 0 | server | Log each player portal transition (eye depth, height and speed before/after) |
| `cl_portal_view_ease` | 0.2 | client | Seconds the camera takes to settle after a portal turns your view (0 = instant) |
| `sv_portal_soundscape_hold` | 2 | server | Seconds a player's ambient soundscape is kept after a portal trip (stops restarts) |
| `sv_portal_mod_version` | (version.txt) | server, replicated | Release the server runs; clients on another release get a chat warning |

## Dedicated server

Add the mod to an existing Half-Life 2: Deathmatch Dedicated Server (Steam app 232370) with the `server/` folder of
the release. Your normal HL2DM server keeps running; the portal server runs next to it on port 27016.

- **Linux:** `server/install_server.sh /path/to/server [--port 27016] [--map dm_lockdown]`, then
  `start_portal_server.sh` in the server folder.
- **Windows:** `server\install_server.bat "C:\path\to\server" [port]`, then `start_portal_server.bat`
  (needs `srcds_win64.exe`; only tested against a fake server so far).
- Not sure which? The server folder has `srcds_run`/`srcds_linux` on Linux, `srcds.exe`/`srcds_win64.exe` on Windows.
  See `server/README_SERVER.txt`, also for hosted servers.

CI runs the Linux kit on every push and then the self-test on it (`scripts/selftest.sh`, `+sv_portal_selftest 1`:
portals, props, rockets, bullets and a bot walking through holding a box, on `dm_lockdown`). What the Linux kit does:

- A 64-bit launcher, `srcds_portal64`: the depot's `srcds_linux` is 32-bit. It loads `bin/linux64/dedicated_srv.so`
  (`packaging/server/srcds_portal64.c`).
- The SDK's 64-bit `libsteam_api.so` next to it, and `steamclient.so` in `~/.steam/sdk64`.
- Plain-name links for the engine's `bin/linux64/*_srv.so` modules, and the mod's `server_srv.so` -> `server.so`.
- In the mod's `gameinfo.txt`, `|appid_243750|` -> `|all_source_engine_paths|` (no Source SDK Base on a server).

## Build

The repo holds only the mod. `scripts/setup.sh` fetches the SDK at the pinned commit (`SDK_COMMIT`), applies
`patches/hl2dm-portalmod.patch` (edits to stock SDK files) and copies `overlay/` (new files).

| Script | Does |
|---|---|
| `scripts/install.sh` | Linux, all-in-one: setup → build → package → install into Steam |
| `scripts/setup.sh [dir]` | Fetch SDK into `./sdk` and apply the mod |
| `scripts/build_linux.sh [dir]` | Native build with host gcc (no podman), → `sdk/game/mod_hl2mp/bin/linux64/` |
| `scripts/package.sh [dir] [version]` | Ready-to-play folder + `dist/hl2dm_portal-<version>.zip` (debug info stripped) |

Windows: run `scripts/setup.sh` from Git Bash, then `sdk\src\createallprojects.bat`, open `everything.sln` in
Visual Studio 2022 and build (DLLs land in `sdk\game\mod_hl2mp\bin\x64`).

**CI** (`.github/workflows/build.yml`) builds Linux inside the Steam Runtime "sniper" container (what Valve requires
for distributed Linux binaries) and Windows with MSVC, then packages both into one zip. Every push uploads it as an
artifact; pushing a `v*` tag publishes a Release, or use **Actions → build → Run workflow** with `release_tag` (no git tag needed).

## Working on the code

Edit inside `sdk/`, rebuild, then run `scripts/export.sh` to write your changes back to `patches/` and `overlay/`.
See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how it works, [docs/CHANGELOG.md](docs/CHANGELOG.md) for
history, and [docs/ROADMAP.md](docs/ROADMAP.md) for limitations and next steps.

## License

Code is derived from Source SDK 2013 and covered by the
[SOURCE 1 SDK LICENSE](https://github.com/ValveSoftware/source-sdk-2013/blob/master/LICENSE) (non-commercial).
