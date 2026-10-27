# Changelog

## 0.5.3 — Unreleased

### Fixed
- **Gravity gun: held object slid nearer and farther while you turned.** The hold distance used the object's current
  angles; those lag behind while you turn, so its depth along your aim (corner-on vs face-on) kept changing. It now
  uses the angles it's being turned to.
- **Held object glitching while hovered inside a portal.** It went across (and back) when the hold point was 2 units
  past the plane, so a hold point wobbling at the plane flipped it between the portals. Now 4 units each way (more stops it coming back when pulled).
- **Portal Settings still too big.** It used the main menu's proportional fonts (which grow with the screen); it now
  uses the same fonts as the game's Options dialogs.

## 0.5.2 — 2026-10-27

### Fixed
- **Stuck in a portal whose exit is blocked by a prop.** Loose physics props at the exit no longer block players: they
  get pushed out of the way along the exit's normal. Frozen props still block (they can't move).
- **Props snapping back and forth across a portal.** The smooth-crossing handover now needs the prop's center 1 unit
  past the plane (hysteresis) before swapping sides, so a prop resting on the plane no longer flips every tick.
- **Held box twitching at a portal.** A box held right at a portal's plane (not yet through) was pushed against the
  real wall around the hole by the hold, and shook. It now slides into the hole like a loose prop (smooth crossing)
  and only goes across once the hold point does. Self-test: `gravgun_hold_through_portal_steady`.
- **Gravity gun: held objects shook (everywhere).** Your game simulated the held object too and fought the server's
  position as it arrived. It now shows the server's (steady) position, smoothed; `cl_physcannon_predict_held 1` brings
  back the stock behavior.
- **Portal Settings window rebuilt.** Plain names (Automatic / Best / Balanced / Fast / Off, sharpness, FPS to keep),
  changes apply immediately with a status line showing what's drawn and your FPS, Cancel restores. Console-only
  overrides (`cl_portal_render_views`, `cl_portal_max_views`, `cl_portal_recursion`) that could silently undo the
  menu are reset by it. The layout is measured from the font in use, so it fits at any resolution and UI scale.
  The potato preset runs, reopens the window and saves in one go.
- **Portal views froze.** When a portal's live view stopped being drawn (Automatic dropping to Off when the FPS dipped,
  portal-in-portal views turning off, too many portals on screen) it kept showing its last image instead of turning
  solid. Each frame now invalidates last frame's views first.
- **Flicker walking through a portal in a thin wall.** The tunnel drawn behind the portal surface (it covers the gap
  when the camera's near plane cuts into the wall) was only as deep as the wall, so in a thin wall the near plane
  reached past it and the room behind flashed for a frame. It's now at least 16 units deep, and a camera a little
  behind the plane (inside the hole) keeps the live view and the tunnel instead of losing both.
- A player standing near a portal no longer shoves props flying past the exit (the blocked-exit push only applies
  while moving in).

### Added
- **Interact through portals.** `+use` (buttons, chargers, picking things up), gravity gun pickup, pull and punt,
  SLAM placement, the RPG laser dot (rockets follow it through the portals) and player names now work on things seen
  through a portal. Self-tests: `gravgun_pickup_through_portal`, `gravgun_punt_through_portal`,
  `use_pickup_through_portal`.
- Self-tests `gravgun_hold_steady` and `gravgun_hold_through_portal_steady`: hold a box with the gravity gun, away from
  and through a portal, and measure how much it shakes.

## 0.5.1 — 2026-10-27

### Fixed
- **Invisible wall at some portals (players and props).** The local collision world copies static props and
  displacements (terrain) whole: they can't be cut like brushes. One reaching through the plane at the other portal (a
  prop pushed into the wall, terrain dipping under the floor next to it) stuck out in front of this portal, right
  across the hole; this side's terrain or props reaching into the hole weren't cut out either. They now only collide
  with the part of a hull on their own side of the plane. Props can't be clipped like that (physics uses whole
  shapes), so where any such piece would be in the way, props pop through instead of sliding. Self-test:
  `local_world_front_clear`.
- From a review of the local-world code:
  - A fast move through the plane (falling into a floor portal) clipped most of the hull away for those one-sided
    props and terrain, so the player could end up inside them: the sweep is now traced in steps of at most 4 units.
  - Re-placing a portal while a prop slid through it could leave the prop without the terrain it was resting on
    (and fall out of the map); the slide now ends safely.
  - Floor portals turned by the shooter's view checked too big an area (props popped through needlessly), and the
    other side's props far from the hole stopped props sliding; both now check only where a crossing prop can be.
  - `sv_portal_world_draw` kept working after a map change; `sv_portal_world_info` says whether props slide.

### Added
- `sv_portal_world_draw 1` (cheat, listen server) outlines every piece of each portal's local world: this side's brush
  pieces green, the other side's blue, uncut props and terrain yellow (red where props can't use them).

## 0.5.0 — 2026-10-27

### Changed
- **Players collide with a real hole near portals.** Each portal builds a local collision world: the map around it with
  the hole cut out of the wall, and the other portal's surroundings behind it. Moving into or through a portal is
  traced against that instead of the old split-hull approximation, so the rim, the exit floor and walls behave like
  real geometry. `sv_portal_local_world 0` goes back to the old way. `sv_portal_world_info` / `cl_portal_world_info`
  print what each portal built. Self-tests: `local_world_built`, `local_world_hole`, `local_world_other_side`.
- **Props crossing a portal collide with its local world too.** A prop part way through used to ignore the world
  entirely (pushed straight through, no spin, no gravity through wall portals, backed out after 4 s). Now it hits the
  rim, lands on the floor on the other side, tumbles and rests part way through like it would in a real hole. The
  local world is built as static physics objects that only crossing props collide with. Self-test: `prop_collider_built`.
- CI runs the unit tests (`tests/run_math_test.sh`, new `tests/run_carve_test.sh`).

## 0.4.4 — 2026-10-26

### Added
- **Props slide through portals** instead of popping out whole. A physics prop whose cross-section fits the hole
  stops colliding with the world as it reaches the portal, moves straight through the plane, and is moved to the
  other side when its center crosses (still half through). Clients draw the part that's through coming out of the
  other portal, so it looks like one prop passing through. Objects you're holding still pop through (the hold steers
  them, not a straight slide). `sv_portal_smooth_props` (server) and
  `cl_portal_prop_clones` (client) turn it off. Self-test: `prop_crosses_smoothly`.

## 0.4.3 — 2026-10-25

From the v0.4.2 play-test. Each fix except the settings window and the camera ease has a matching self-test check.

### Fixed
- **Portal Settings text overlapped at high resolutions.** The window inherits proportional (resolution-scaled) fonts
  from the main menu, but its layout was in fixed pixels; positions and sizes now scale with the fonts.
- **Falling fast into a floor portal sometimes stopped you dead.** Two causes, both with a wall portal as the exit:
  - Feet first into the floor portal, the part of you already through sticks out of the wall up to a body length
    before your eye crosses. Anything within that distance in front of the exit (a pillar, a railing) stopped you
    inside the hole, and you sank through with no speed. Now, stopped like that while going in fast, you go through
    at once with the speed you came in with, and meet the obstacle on the other side.
  - Coming out, the mapped eye put the feet below the hole (in the floor), or the floor in front of the wall was a
    little higher than the hole's bottom edge. The exit now lines the hull up strictly inside the hole, pushes out,
    steps up (up to a stair step) if needed, and finishes the tick's move. The camera never ends up behind the exit.
- **Stuck at a floor portal's edge / snapped in when standing there.** The funnel pulled anyone standing with their
  center over the hole toward its middle at 150-600 u/s, faster than walking away. Standing players are now funneled
  only when well over the hole (ellipse x0.8), not while walking away from it, and slowly (80 u/s). Dropping onto
  it at under 50 u/s (stepping off a ledge) counts as standing.
- **Walking into a portal whose exit is blocked froze you (even mid-air).** Each portal checks for room in front of
  the other one (world, players and fixed props; loose props never block). With no room it's a plain wall/floor.
- **Hard to bring a held object back through.** Pulled back toward you, the object was dragged at a point behind the
  far portal, i.e. at the wall next to the hole unless you aimed right at the portal. It's now steered into the hole.

### Added
- **Explosions go through portals.** A blast that can see a portal goes off again just out of the linked one, with
  the radius and damage left after the distance to the hole (same falloff over the whole path).
- **Camera eases after a portal turns it** (roll dropped, pitch past vertical) instead of snapping:
  `cl_portal_view_ease` (seconds, default 0.2, 0 = off). View only; aim and movement are exact.
- **Self-updating installs (Windows).** The desktop shortcuts run `update.bat quiet` first: one request to GitHub,
  and only if a newer release is out it downloads it and runs its installer (no admin). `update.bat` checks by hand;
  `no_auto_update.txt` turns the automatic check off. Releases carry `version.txt`.
- Servers publish their release in `sv_portal_mod_version`; joining with a different release shows a chat warning.
- **Ambient sound no longer restarts on portal trips** (dm_lockdown's announcer replaying): for
  `sv_portal_soundscape_hold` seconds (default 2) after going through, the player's soundscape stays as it was. Going
  back within that time restarts nothing; staying switches as usual afterwards.
- Self-test: held prop comes back, walking off a floor portal's edge, 1200 u/s falls (3 offsets), blocked exit acts
  solid, explosion through a portal. CI tests `update.bat` updating an old install to the latest release.

## 0.4.2 — 2026-10-24

From the v0.4.1 play-test. Each fix has a matching check in the dedicated-server self-test.

### Fixed
- Snapping when walking into a portal at an angle, next to another portal, on floor/ceiling portals and on a floor
  portal's edge, and losing speed going through fast: the server no longer snap-teleports players at wall/floor/ceiling
  portals (it fought client prediction). Coming in a little off-center now slides you into line (funneling).
- Going higher and higher between two floor portals: the transition maps the moment the eye crossed the plane and
  finishes the move on the exit side. The gravity of the rest of that tick is redone in the exit frame (it had sped
  the fall up where it should slow the climb: one tick of speed gained per loop), and height from being pushed out
  of the exit comes out of the exit speed.
- Camera jump between floor and wall portals (the eye is carried through, not the hull center), and camera blending
  across the portal (prediction smoothing and interpolation reset on the way through).
- Held props snapping back and forth between the portals: they go through only when the hold point is past the plane.
- Crowbar and stunstick didn't hit through portals.
- A black shape outside the portal near the edge of thin walls/pillars: the tunnel behind the portal is fitted to the
  wall's thickness and only drawn up close.

### Added
- **Portal Settings** window in the main menu (`portal_settings`), each option explained. The Options > Multiplayer >
  Advanced entries didn't exist in HL2DM and are removed.
- Self-test: off-center angled walk-in, 900 u/s fling, held box near the plane, crowbar through a portal, floor-portal
  edge, floor funnel, floor-to-floor loop height.

## 0.4.1 — 2026-10-23

### Added
- Graphics settings for slow PCs: `cl_portal_quality` 0 off / 1 low / 2 medium / 3 high / 4 auto (default), also in
  Options > Multiplayer > Advanced. Levels set the number of live views, portal-in-portal views, view resolution and a
  minimum on-screen size; auto steps between them to hold `cl_portal_auto_fps`. `cl_portal_resolution` caps view
  resolution.
- Portal views only render the pixels the portal covers on screen (`cl_portal_scissor`), and water in them is drawn
  cheap below High.
- `cfg/potato.cfg` (lowest settings for the whole game) and a "HL2DM Portal (low graphics)" desktop launcher.

### Changed (faster)
- Portals cache their frame (directions, wall axis) and the portal-to-portal matrix, rebuilt only when they move.
  These were recomputed with trig on every player movement trace (many per tick, and again in client prediction),
  every bullet hitting a wall, every held-object update and every rendered portal.
- Portal meshes are indexed and use a precomputed circle: each vertex is computed and projected once instead of up to
  four times. Live fill: 1520 vertex computations -> 401 close up, 121 beyond 1024 units (fewer rings far away, where
  the screen-space mapping error is invisible); solid fill 1520 -> 41; tunnel and rim about half.
- Server kit in the release (`server/`): `install_server.sh` (Linux) and `install_server.bat` (Windows) add the mod to
  an existing HL2DM dedicated server and write `start_portal_server.*` for a second server on port 27016. Updates keep
  `server.cfg` and the port/map. CI sets up its self-test server with it, so the Linux kit is tested every push.
- `install.bat` for shared / no-admin PCs, with Steam anywhere: reads `SourceModInstallPath` (where Steam actually lists
  mods from), then looks at the running `steam.exe`, more no-admin folders, Steam shortcuts (desktop, Start Menu) and
  every drive (4 folders deep), and asks with a folder picker only as a last resort; checks write access, finds Source SDK Base
  in any Steam library, and makes desktop launchers (`steam.exe -applaunch 243750 -game <mod>`), so no Steam restart.
  A `server.txt` (address:port) next to it adds a "Join HL2DM Portal server" shortcut.
- `install.sh`: `SourceModInstallPath` from `registry.vdf`, Source SDK Base in any library.

### Fixed
- `install.bat` messages broke on Steam paths with parentheses, e.g. `C:\Program Files (x86)\Steam`.

## 0.4.0 — 2026-10-22

### Fixed
- Crash when a held prop reached a portal (HL2's drop helper uses `UTIL_GetLocalPlayer()`, NULL in multiplayer).
- Props resting where a portal opened stayed frozen until touched: HL2MP made small props client-only, so the
  server never had them. `sv_pushaway_clientside_size` defaults to 0 in HL2MP, so every prop is a real server prop.
- Portals seen through a portal drew flat grey; they now show their own live view (one level, `cl_portal_recursion`).
- Bullets out of a portal can hit their shooter.
- Found by the dedicated-server self-test:
  - Walking into a portal stopped 1 unit short: the server put the player back on their physics shadow every tick
    (the wall stops the shadow; the stuck checks use plain traces). The shadow now follows the player in a hole.
  - Walking straight into the wall right beside a portal could freeze a player (zero collision normal).
  - A prop held toward a portal was pulled back short of it by the wall behind the hole (hold-distance trace).

### Added
- Held props stay held through portals (gravity gun and `+use`): the hold target follows the portals between holder
  and prop, punts/throws aim through them, and the client pauses prediction while the prop is on the far side.
- Players walk into portals seamlessly: no wall collision inside the hole, the part through it collides on the exit
  side, and they go through when the eye crosses the plane (predicted). Off-center players still snap through.
- Wall portals just above the floor drop onto it so players can walk in.
- Tunnel mesh behind live portals: the far side stays visible with the camera right at the portal.
- Real-server self-test (`sv_portal_selftest`, `scripts/selftest.sh`, CI) on the HL2DM dedicated server: props,
  held props, traces, rockets, small props, and a bot walking through while carrying a box. Releases need it to pass.
- README: running on a dedicated server (64-bit launcher, libraries, gameinfo).

### Verified
- Self-test on the HL2DM dedicated server (dm_lockdown): 11/11 pass, including a seamless (unsnapped) walk-through
  with a held box. Client rendering and feel still need a play-test.

## 0.3.2 — 2026-10-21

### Fixed
- `install.bat` failed with "sharing violation" when reinstalling while the game was running (it has
  `bin\x64\client.dll`/`server.dll` open). It now detects a running game and asks to close it, and copies with
  robocopy, retrying locked files (10 x 2 s) with a clear retry prompt instead of aborting.
- `install.sh` replaces files instead of overwriting them in place, so a running game can't crash on reinstall.

### Added
- CI tests `install.bat` on Windows: fresh install, reinstall over a briefly locked DLL, and a DLL that stays
  locked (must fail cleanly). Also tests `install.sh`. Releases publish only after these pass.

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
