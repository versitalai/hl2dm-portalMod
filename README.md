# HL2DM Portal Mod

Portal gun for Half-Life 2: Deathmatch, built on Valve's
[Source SDK 2013](https://github.com/ValveSoftware/source-sdk-2013) (HL2DM = the SDK's `hl2mp` game).
Every player spawns with a portal gun. Shoot a blue and an orange portal on any world surface.
Anything that moves goes through them. You see through them live.

## Install (players)

1. Install **Source SDK Base 2013 Multiplayer** from Steam (free): `steam://install/243750`
2. Download `hl2dm_portal-<version>.zip` from [Releases](https://github.com/versitalai/hl2dm-portalmod/releases)
   (or the `hl2dm_portal` artifact of the latest [build](https://github.com/versitalai/hl2dm-portalmod/actions) run) and unzip it.
3. **Windows:** double-click `install.bat`. **Linux:** `./install.sh`
   (finds native and Flatpak Steam; `--steam-dir DIR` otherwise). Or copy the `hl2dm_portal` folder into
   `Steam/steamapps/sourcemods/` yourself.
4. Restart Steam, launch **HL2DM Portal** from your Library.

From source on Linux, one command does it all (fetch SDK, build, package, install): `scripts/install.sh`

## What goes through

| Thing | How |
|---|---|
| Players | Position, view angles, velocity carried through (walk, fall, fling) |
| Physics props, dropped weapons, items | Position, orientation, linear + angular velocity |
| Projectiles: rockets, crossbow bolts, SMG grenades, frags, AR2 energy balls, SLAM satchels | Caught up to `sv_portal_lookahead` ticks early, so fast ones don't hit the wall first |
| Bullets (all hitscan weapons, shotgun pellets) | Trace continues out of the linked portal (up to 4 portals deep), with impacts and tracers on the far side |
| Portal gun shots | Go through existing portals too, so you can place a portal through a portal |
| SLAM tripmine laser | Beam continues through portals (drawn on every segment). Anyone crossing any segment trips it; opening, closing or moving a portal on its path trips it too |

Gravity-gunned objects are dropped as they go through. Props with motion disabled, and things stuck to walls (a
placed tripmine), stay put.

## See-through rendering

The client renders the scene from a virtual camera behind the exit portal into a render target, every frame, then maps
it onto the portal surface. You see live action through portals, and your own player model in them. Through-portal
recursion (a portal seen inside a portal view) draws as a solid tinted portal.

## Multiplayer

Built for it: HL2DM is multiplayer-only, and everything runs server-authoritative.
- Every player has their own blue/orange pair. Anyone can use anyone's portals.
- Portals are always networked to every client (`FL_EDICT_ALWAYS`), so each client can draw any portal and its view.
- Teleporting happens on the server and replicates like `trigger_teleport`. Clients don't predict it, so with latency
  your view snaps through after a round trip.
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

## ConVars

| ConVar | Default | Side | Meaning |
|---|---|---|---|
| `cl_portal_render_views` | 1 | client | Render live views through portals (0 = solid color) |
| `cl_portal_max_views` | 2 | client | Max portals rendered see-through per frame, closest first (max 2) |
| `cl_portal_draw_local_player` | 1 | client | Draw your own model in portal views |
| `sv_portal_lookahead` | 3 | server | Ticks of lookahead for fast movers |
| `sv_portal_cooldown` | 0.05 | server | Seconds before an entity can go through again |
| `sv_portal_debug` | 0 | server (cheat) | Draw detection volumes, placement traces, exit boxes |

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
artifact; pushing a `v*` tag publishes a Release.

## Working on the code

Edit inside `sdk/`, rebuild, then run `scripts/export.sh` to write your changes back to `patches/` and `overlay/`.
See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how it works, [docs/CHANGELOG.md](docs/CHANGELOG.md) for
history, and [docs/ROADMAP.md](docs/ROADMAP.md) for limitations and next steps.

## License

Code is derived from Source SDK 2013 and covered by the
[SOURCE 1 SDK LICENSE](https://github.com/ValveSoftware/source-sdk-2013/blob/master/LICENSE) (non-commercial).
