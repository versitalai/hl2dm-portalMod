# Changelog

## 0.3.1 — 2026-10-21

### Fixed
- Blue portal (M1) never fired, and R never closed portals. The gun has no ammo type, and the stock
  `CBaseCombatWeapon::ItemPostFrame` sends primary fire on a no-ammo, no-clip weapon to `HandleFireOnEmpty()` and
  only reloads clip weapons. Secondary has no ammo check, which is why orange worked. The gun now has its own
  `ItemPostFrame` (as the gravity gun does). Found in the first play-test.

## 0.3.0 — 2026-10-21

### Added
- Ready-to-play release zip: `hl2dm_portal/` sourcemod folder (sourcemods `gameinfo.txt`, stripped binaries),
  `install.sh` (Linux: native/Flatpak Steam auto-detect) and `install.bat` (Windows: Steam path from registry).
- `scripts/package.sh` (builds the zip), `scripts/install.sh` (source → installed in one command).
- GitHub Actions: Linux build in the Steam Runtime sniper container, Windows MSVC build, packaged zip artifact on
  every push, Release on `v*` tags.

### Verified
- Package + installer tested against a fake Steam tree (auto-detect, reinstall, missing Steam, missing SDK Base).
- First CI run green: Linux (sniper) and Windows (MSVC) builds, packaged zip has both (24 MB).
- Sourcemods layout follows the SDK's own `gameinfo.txt`; Valve's distribution docs weren't reachable from the
  build box and it hasn't been launched in Steam yet.

## 0.2.0 — 2026-10-21

### Added
- SLAM tripmine laser passes through portals: traced and drawn per segment, trips on anything crossing any segment
  and when a portal on its path opens, closes or moves.
- `UTIL_HL2MPPortal_TraceRay` can return the traced path (start/end pair per segment).

### Fixed
- A placed tripmine inside a portal's area would itself be teleported (it's `MOVETYPE_FLY` with zero velocity,
  touching the plane). Non-physics, non-player entities that aren't moving are now never teleported.

### Verified
- Builds clean (gcc 13), no warnings in changed code; math test passes. Not play-tested.

## 0.1.0 — 2026-10-21

First version, on Source SDK 2013 commit `be57e3d01ac7d7aa6a2cacbfef187c0b46616db0`.

### Added
- `weapon_portalgun`: M1 blue, M2 orange, R closes both. Given on spawn and with `impulse 101`.
- `prop_hl2mp_portal`: linked portal pair on world surfaces, with fit checks that slide the portal off edges,
  inside corners and its partner.
- Teleporting of players, physics props, projectiles (with lookahead for fast ones), NPCs; velocity and orientation
  transformed; gravity-gunned objects dropped.
- Bullets and portal shots continue through portals (shared server/client, plus the HL2MP impact TE).
- Live see-through rendering via render-to-texture, custom clip plane and custom vis; local player drawn in portal views.
- Server PVS includes the exit side of visible portals.
- ConVars: `cl_portal_render_views`, `cl_portal_max_views`, `cl_portal_draw_local_player`, `sv_portal_lookahead`,
  `sv_portal_cooldown`, `sv_portal_debug`.
- `scripts/setup.sh`, `scripts/build_linux.sh`, `scripts/export.sh`; `tests/portal_math_test.cpp`.

### Fixed (stock SDK)
- `FireBullets` sent `tr.startpos` as the shot origin to `TE_HL2MPFireBullets`. Same value as the muzzle in stock
  HL2MP, but wrong once traces can leave a portal, so it now sends `info.m_vecSrc`.

### Verified
- Stock SDK `server.so` + `client.so` built clean before any change (gcc 13, native ninja).
- Modified tree builds clean with no warnings in new/changed code; no unresolved symbols.
- Fresh `setup.sh` + `build_linux.sh` from an empty directory builds.
- Transform math test passes.
- **Not** play-tested in game: the build machine has no Steam / Source SDK Base 2013 content.
