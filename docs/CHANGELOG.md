# Changelog

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
