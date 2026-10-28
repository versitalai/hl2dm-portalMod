# Handoff: everything a new agent/developer needs

Read this first, then `CLAUDE.md`, `docs/ARCHITECTURE.md` (how each system works), `docs/CHANGELOG.md` (why
each change was made, newest first) and `docs/ROADMAP.md` (known limits). State as of **v0.5.5 (2026-10-28)**.

## 1. What this is

A portal mod for **Half-Life 2: Deathmatch** on **Source SDK 2013 MP** (64-bit, 2025 SDK update). Repo
`versitalai/hl2dm-portalmod`. It does **not** contain the SDK: it holds

- `patches/hl2dm-portalmod.patch`: every edit to stock SDK files (one unified diff).
- `overlay/`: files that are new (copied over the SDK as is), source and game content.
- `SDK_COMMIT`: the pinned SDK commit (`be57e3d01ac7d7aa6a2cacbfef187c0b46616db0`).

## 2. Working loop (do exactly this)

```bash
scripts/setup.sh <sdk-dir>          # clone SDK @ SDK_COMMIT, apply patch, copy overlay (default ./sdk, gitignored)
# edit files inside <sdk-dir>/src/...
scripts/build_linux.sh <sdk-dir>    # native ninja + host gcc; full ~6 min/4 cores, incremental seconds
tests/run_math_test.sh <sdk-dir>    # portal matrix math unit test
tests/run_carve_test.sh <sdk-dir>   # hole-carving geometry unit test
scripts/export.sh <sdk-dir>         # writes your SDK edits back into patches/ + overlay/
git add -A && git commit && git push
```

Rules that bite:
- **A brand-new file** must be copied into `overlay/` (same relative path) by hand the first time; `export.sh`
  only refreshes files already there. New .cpp files must also be added to `src/game/client/client_hl2mp.vpc` or
  `src/game/server/server_hl2mp.vpc` (shared files: both) — those vpc edits live in the patch.
- `vpc` needs `VPC_NINJA_BUILD_MODE` set or it segfaults (build_linux.sh does it).
- Never hand-edit `patches/hl2dm-portalmod.patch`; edit in the SDK tree and export.
- Keep SDK-touching edits behind `#ifdef HL2MP` (or in hl2mp files), Valve style: tabs, `m_` members, `C_` client
  classes, `#ifdef CLIENT_DLL` / `GAME_DLL` for side-specific code. Comments explain *why* in plain words.
- The build box has **no game content**: only compile/link and the two unit tests run locally. Gameplay is verified
  by the **server self-test in CI** (section 5) and by the user play-testing.
- Agents in this repo's cloud sessions: no `gh` CLI; use the GitHub MCP tools. Codex is **not** installed (needs an
  OpenAI API key as an environment secret).

## 3. Release process (standing user rule: do it without asking once CI is green)

1. Add a `## X.Y.Z — Unreleased` section at the top of `docs/CHANGELOG.md` while working; commit + push to the
   working branch (`claude/hl2-deathmatch-portal-mod-wdzrn8`, or whatever branch the session names).
2. Wait for the `build` workflow on that push to be **green** (linux, windows, selftest, package, test-windows-installer).
   Docs-only pushes don't trigger CI (`paths-ignore: docs/**, **.md`).
3. Change the heading to `## X.Y.Z — <date>`, commit, push.
4. Open a PR to `main`, merge it with a **merge commit**.
5. Trigger `build.yml` via `workflow_dispatch` on `main` with input `release_tag: vX.Y.Z` — it builds and publishes the
   GitHub Release (zips from `scripts/package.sh`, installers in `packaging/`).
6. Reset the working branch to `origin/main` and force-push it (the PR is merged; never stack on merged history).

## 4. Code map (where everything lives)

All paths under `src/game/`. (P) = in the patch (stock file edited), (O) = overlay (new file).

| Area | File(s) | Notes |
|---|---|---|
| Portal entity, teleport, crossings, traces | `shared/hl2mp/hl2mp_portal.cpp/.h` (O) | `CHL2MPPortal`: placement `PlaceOnSurface`, `Fizzle`, `FreePlayersInHoles`, `ShouldTeleport`, `TeleportEntity`, smooth prop crossings (`CanCrossSmoothly`, `StartCrossing`, `UpdateCrossings`, `EndCrossing`), `UTIL_HL2MPPortal_TraceRay` (rays/hulls through portals; outputs path + portals entered), `UTIL_HL2MPPortal_PathLength`, `UTIL_HL2MPPortal_TracePlayerHull` (player movement through portals), `HL2MPPortal_Set/GetUsePortals` (+use stash), client `DrawModel` (fill, rim, tunnel), prop clones (`C_HL2MPPortalPropClone`). |
| Local collision world | `shared/hl2mp/hl2mp_portal_world.cpp` (O), `shared/hl2mp/hl2mp_portal_carve.h` (O) | Per-portal brush copy with the hole carved out + the other side's geometry mapped behind it; prop collider (static physics objects). **Axis-aligned portals only** (`CanCarve`). |
| Portal gun | `shared/hl2mp/weapon_portalgun.cpp` (O), `overlay/game/mod_hl2mp/scripts/weapon_portalgun.txt` | Creates/links/places the two portals; reload fizzles. |
| Gravity gun (+use pickup controller) | `shared/hl2mp/weapon_physcannon.cpp/.h` (P) | `CGrabController` portal stack (`m_hPortals`, `SetPortals`, `TransformThroughPortals`, `OnAttached/HolderWentThroughPortal`), hold target in `UpdateObject` (per-usercmd on server, clearance blend `m_flHoldDistBlend`), `FindObject`/`CheckForTarget`/punt through portals, `cl_physcannon_predict_held` (client, default 0). |
| +use through portals | `shared/baseplayer_shared.cpp` `FindUseEntity` (P) | HL2MP branch traces through portals; reach = path length. |
| Bullets, melee, SLAM, RPG | `shared/baseentity_shared.cpp` FireBullets (P), `shared/hl2mp/weapon_hl2mpbasebasebludgeon.cpp` (P), `weapon_slam.cpp` `SLAM_TraceAim` (P), `weapon_rpg.cpp/.h` (laser dot `m_hPortals`, missile `m_hPortalsPassed`, `RPG_MissileWentThroughPortal`) (P), `server/hl2mp/grenade_tripmine.cpp` (P), `client/hl2mp/c_te_hl2mp_shotgun_shot.cpp` (P) | |
| Player movement | `shared/hl2/hl_gamemovement.cpp/.h` (P), `server/hl2mp/hl2mp_player.cpp/.h` (P), `client/hl2mp/c_hl2mp_player.cpp/.h` (P) | Seamless walk-through (predicted), view turn ease `cl_portal_view_ease`, player ID through portals (`UpdateIDTarget`), PVS for the exit side. |
| Rendering | `client/viewrender.cpp/.h` (P), `client/hl2mp/hl2mp_rendertargets.cpp` (O) | `DrawHL2MPPortalViews` (bumps view stamps every frame), quality ladder `s_HL2MPPortalQuality`, auto quality, `RenderHL2MPPortalView` (clip plane, scissor, custom vis), `HL2MPPortal_GetRenderStatus` for the settings window. |
| Settings window | `client/hl2mp/hl2mp_portal_settings.cpp` (O), `overlay/game/mod_hl2mp/resource/GameMenu.res`, `cfg/potato.cfg` | `portal_settings` command; non-proportional fonts; layout measured from font metrics; live apply, Cancel restores. Every client portal cvar has a control. |
| Self-test | `server/hl2mp/hl2mp_portal_selftest.cpp` (O), `scripts/selftest.sh` | See section 5. |
| Explosions / soundscapes / gamerules | `server/soundscape.cpp`, `shared/hl2mp/hl2mp_gamerules.cpp/.h`, `server/gameinterface.cpp`, `shared/props_shared.cpp` (P) | Radius damage through portals, soundscape hold, small props kept server-side. |

### Key constants (hl2mp_portal.cpp)
`PORTAL_HELD_THROUGH_DIST 4` (held object crosses when hold point is this far past plane; 2 flip-flopped, 8 broke
`held_prop_comes_back`), `PORTAL_CROSS_HYSTERESIS 1` (smooth prop handover), `PORTAL_TUNNEL_DEPTH 24` /
`PORTAL_TUNNEL_MIN_DEPTH 16` (near-plane cover), `PORTAL_RENDER_OFFSET 0.8`, exit push 150 u/s for loose props
blocking an exit (only while the player moves in), floor-exit min bounce speed `sqrt(2·g·2·extent)`.

### Cvars
Server: `sv_portal_debug`, `sv_portal_lookahead`, `sv_portal_smooth_props`, `sv_portal_cooldown`,
`sv_portal_local_world`, `sv_portal_world_draw` (cheat), `sv_portal_soundscape_hold`, `sv_portal_selftest`,
`sv_portal_debug_moves`, `hl2mp_allow_pickup` (stock, default 0 = no hand pickup).
Client: `cl_portal_quality` (0 off,1 fast,2 balanced,3 best,4 auto), `cl_portal_auto_fps`, `cl_portal_resolution`,
`cl_portal_max_views`, `cl_portal_recursion`, `cl_portal_scissor`, `cl_portal_render_views`, `cl_portal_draw_local_player`,
`cl_portal_prop_clones`, `cl_portal_view_ease`, `cl_portal_quality_debug`, `cl_physcannon_predict_held`.
Debug commands: `sv_portal_world_info` / `cl_portal_world_info` (collision world checksums must match).

## 5. Testing

- **Unit:** `tests/run_math_test.sh`, `tests/run_carve_test.sh` (local + CI).
- **Self-test (CI only, needs game content):** CI installs an HL2DM dedicated server (cached weekly), runs
  `dm_lockdown` with `sv_portal_selftest 1`. A bot is driven by `DriveScenario` (`m_angBot` view, `m_flBotForward`,
  `m_flAttackUntil`/`m_flAttack2Until`/`m_flUseUntil` buttons). `RunStep()` is a `switch ( m_iStep++ )` state machine;
  each case sets up, calls `Wait(seconds)` and the next case checks with `Result( "name", pass, fmt... )`. Output
  lines `PORTALTEST name PASS|FAIL ...`, ends `PORTALTEST DONE pass=N fail=M`. Currently **34 checks, all pass**.
  To add a test: insert a case, renumber the following ones, keep the final floor-loop case last. Read failing
  logs with the GitHub MCP `get_job_logs` (job `selftest`), they include `info` lines and traces.
- **What CI cannot test:** anything client-side (rendering, prediction, UI, interpolation). Those need the user.

## 6. Open issues (from play-tests) and leads

1. **Gravity gun hold** — v0.5.5 changed the server to update the hold target on every usercmd (was first command
   per tick) and blended the near/far clearance switch. If it still swings when turning: ask the user to try
   `cl_physcannon_predict_held 1`. If that fixes it, the remaining issue is interpolation lag of the server-driven
   prop; proper fix = keep client prediction on and stop the server correction fighting it. No self-test turns
   while holding yet — adding one (sweep `m_angBot.y` while holding, measure eye→box distance variance, also with
   several usercmds per tick) is a good next step.
2. **Slanted portals** — no local collision world (needs axis-aligned plane); players/props snap-teleport, unpredicted.
   v0.5.5 added exit spots along the exit normal and ramp up-vector downhill. Remaining ideas: earlier pre-teleport
   for players on slopes in `ShouldTeleport`; or snap near-axis normals; full fix = oriented carving (large).
3. **Camera flicker walking through** — parked by the user. v0.5.2 made the tunnel ≥16 deep and drew it while the
   camera is slightly behind the plane; still reported.
4. **Ragdolls can't be moved** — unanswered question: player corpses (client-only in stock HL2DM → would need
   server ragdolls) or map `prop_ragdoll` (bug to fix)?
5. **Settings UI size** — now non-proportional fonts; user asked whether their UI scale setting is respected; GameUI
   scaling is closed source, nothing found to read it from.
6. Prop hovering inside a portal glitching — hysteresis raised to 4; re-check with the user.

## 7. User preferences (important)

Terse replies, technical substance kept; a short "teaching moment" each reply; offer a handoff about every 10
messages; keep CHANGELOG/ARCHITECTURE/ROADMAP/README/this file updated; delegate to Sonnet/Haiku when it helps but
don't over-spawn; release automatically after green CI (section 3); don't re-clone/fetch the repo unprompted.
