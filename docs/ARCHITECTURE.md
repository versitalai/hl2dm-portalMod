# Architecture

How the portal mod works, file by file. Paths are relative to the SDK tree (`sdk/`).

## Files

| File | Kind | Role |
|---|---|---|
| `src/game/shared/hl2mp/hl2mp_portal.h/.cpp` | new | `prop_hl2mp_portal` entity: placement, teleporting, bullet tracing, client drawing |
| `src/game/shared/hl2mp/weapon_portalgun.cpp` | new | `weapon_portalgun`: fires/places portals, owns one linked pair |
| `src/game/client/hl2mp/hl2mp_rendertargets.cpp` | new | Exposes `IClientRenderTargets` for HL2MP; allocates `_rt_HL2MPPortal0/1` |
| `game/mod_hl2mp/scripts/weapon_portalgun.txt` | new | Weapon script (models, sounds, HUD slot) |
| `src/game/client/viewrender.cpp/.h` | patched | `CViewRender::DrawHL2MPPortalViews()`: renders portal views before the main view |
| `src/game/client/c_baseplayer.cpp` | patched | `ShouldDrawThisPlayer()` is true during portal views (see yourself) |
| `src/game/shared/baseentity_shared.cpp` | patched | `FireBullets()` traces through portals; TE gets the muzzle origin; tracer from exit |
| `src/game/server/hl2mp/grenade_tripmine.cpp/.h` | patched | SLAM laser traces/draws through portals; break check compares total length + portal count |
| `src/game/client/hl2mp/c_te_hl2mp_shotgun_shot.cpp` | patched | Client impact/tracer re-simulation traces through portals |
| `src/game/server/hl2mp/hl2mp_player.cpp/.h` | patched | Gives the gun on spawn / `impulse 101`; `SetupVisibility()` adds portal exits to the PVS |
| `src/game/{client,server}/*_hl2mp.vpc` | patched | Adds the new files to the projects |

Everything new is guarded by `HL2MP` where it touches shared engine-facing code, so the HL2 / TF projects are unaffected.

## Portal frame and the core matrix

A portal's angles make its local **forward = surface normal** (out of the wall), local up = portal "top". Placement picks
up as world up on walls, and the shooter's view direction on floors/ceilings (so the portal faces you, like Portal).

All teleport/render math uses one matrix (`CHL2MPPortal::MatrixThisToLinked`):

```
M = LinkedToWorld * Rz(180) * WorldToThis
```

`Rz(180)` flips local forward and right. So a point *behind* this portal maps *in front of* the linked one, and a
velocity *into* this portal comes *out of* the linked one. The same `M` moves points (`VMul4x3`), directions
(`VMul3x3`), and whole frames (`M * entityMatrix` for props and the virtual camera). `tests/portal_math_test.cpp`
checks these conventions against the SDK's mathlib (`tests/run_math_test.sh`).

## Teleporting (server)

`PortalThink` runs every tick on open portals (both ends placed):

1. Enumerate entities in a box from 8 units behind the plane to 160 in front, covering the ellipse.
2. `ShouldTeleport` filters: no world, parented, `MOVETYPE_NONE/PUSH/NOCLIP/OBSERVER/LADDER`, non-physics brush
   entities, motion-disabled physics, non-physics things standing still (they're attached, e.g. a placed tripmine),
   dead/observing/driving players, cooldown.
3. It goes through if its center is in front of the plane, it crosses the plane inside the ellipse, and either
   - **touching**: AABB leading face within 1 unit of the plane and not moving away (walking into a wall portal,
     resting on a floor portal), or
   - **arriving**: moving toward the plane fast enough to reach it within `sv_portal_lookahead` ticks. This is what
     catches rockets and crossbow bolts before their own touch code explodes/sticks them on the wall.
4. `TeleportEntity` transforms center, orientation and velocity with `M`, pushes the box out in front of the exit by
   its support distance + 2 units, then hull-tests candidate spots (as-is, centered on the portal, raised) and uses the
   first free one. If none is free it doesn't teleport (the portal acts like the wall it's on).
   Players keep an axis-aligned hull; only their view rotates (roll dropped, pitch clamped). VPhysics objects get
   their velocity re-applied (angular velocity is local-space, so it carries over unchanged) and are woken.
   Physics props that can cross smoothly (below) don't teleport here; they start a crossing instead.

### Smooth prop crossing (server + client)

A prop goes through the hole a piece at a time instead of popping out whole (`sv_portal_smooth_props`, default 1).
`CanCrossSmoothly`: an axis-aligned portal, one moveable physics object (no ragdolls), not held (the grab controller
turns and steers it, and backing it out of a crossing jumps it away from the hold point: `+use` drops it), its AABB
footprint fits the hole, room for it out of the other portal, and heading in (or on a floor portal: gravity takes it in; a prop resting
against a wall portal still pops through, it would sit half in the wall).
- `StartCrossing` records it and turns off its collisions with the world (`PhysDisableEntityCollisions` with
  `g_PhysWorldObject`): the wall around and behind the hole stops blocking it. Other props and players still do.
- `UpdateCrossings` (a game system, every tick after entity think): while it's straddling, velocity is kept along the
  portal normal and spin is stopped (it can't drift into the walls it no longer hits); through a wall portal gravity is
  off (it would sink into the bottom of the hole). One picked up part way through follows the grab controller,
  and goes across only once the hold point is 2 units past the plane (like `ShouldTeleport`; else it flips).
- When its center crosses the plane, `TeleportEntity( bExact )` maps it exactly (still straddling, now the other
  portal's plane) and the crossing continues there. It ends, world collisions back on, once it's 2 units clear of the
  plane (moving away), or with a push-out if it turned out of line with the hole, a portal closed, or 4 s passed.
- Client: `C_HL2MPPortalCloneSystem` keeps a client-only copy (`C_HL2MPPortalPropClone`) of each physics prop within
  24 units of an open portal's plane, in line with the hole. Every frame after interpolation (`Simulate`) it's put at
  `M * prop` for whichever portal of the pair the prop is through, and drawn only while the prop is through the plane.
  Each portal's opaque surface hides what's behind its plane (the part gone in, and the part of the copy not out yet),
  and portal views already clip at the exit plane, so no clip planes are needed. The copy swaps sides in the same
  frame the prop does, so the hand-over at the center is invisible. `cl_portal_prop_clones 0` turns copies off.

## Walking through (shared, predicted)

Axis-aligned portals (`CanCarve`: both normals along a world axis) let players move into them as part of normal player
movement, so the client predicts it. `CHL2GameMovement` (HL2MP) routes `TracePlayerBBox`, `TryTouchGround` and
`TestPlayerPosition` through `UTIL_HL2MPPortal_TracePlayerHull`:
- If the hull's footprint is inside the hole (the portal's bounding rectangle + 1 unit) and it touches the plane, the
  hull is split at the plane. The front part traces normally; the part through the hole is mapped through `M` and
  traced on the exit side. The wall behind the hole never blocks.
- Not yet through the plane but outside the hole: normal collision (the wall). Through the plane and moving sideways
  into the rim: blocked, with a normal opposing the sideways motion (never a zero normal: that froze players).
- `PlayerMove` moves the player through when the eye crosses the plane: origin/velocity/view through `M`, floor exits
  get at least 250 u/s out. The client turns the view with `engine->SetViewAngles` on the first prediction.
- Server: `CHL2MP_Player::VPhysicsShadowUpdate` keeps the player's physics shadow with the player while in a hole.
  VPhysics doesn't know about portals (the wall stops the shadow), and the base class's stuck checks use plain traces;
  together they put the player back on the shadow every tick.
- The server never teleports players at these portals (it used to snap off-center players, which fought client
  prediction: snaps at angles and edges, between floor portals, lost speed). Instead `UTIL_HL2MPPortal_PlayerFunnelMove`
  (called at the start of `PlayerMove`) slides a player heading into the hole a little off-center into line: center
  over the portal (ellipse x1.1), moving toward the plane (or standing on a floor portal), within 0.25 s of it; moved
  sideways at up to 600 u/s by a hull trace. Centered outside that, the rim is solid (stand on a floor portal's edge).
  Standing on a floor portal (not moving into it) is funneled only when well over the hole (ellipse x0.8), not while
  walking away from it, at 80 u/s: faster pulls trapped players on the edge. Approaching at under 50 u/s counts as
  standing (a small drop onto the edge isn't heading in).
- No room in front of the linked portal (`HasRoomForHull`: a hull test just out of it against the world, players and
  fixed props; loose physics props never count) and not yet through the plane: the carving is off, the portal is a
  plain wall or floor. Cached per tick per portal.
- The transition maps the position at the moment the eye was on the plane (interpolated along the tick's move), then
  finishes the rest of the move on the exit side with a hull trace. The eye stays continuous, so floor<->wall portals
  don't jump the camera, and loops between floor portals don't gain height. Only if that spot is blocked is the hull
  first lined up with the exit hole (`GetHoleFitShift`, strictly inside: floor -> wall portal puts the feet below the
  hole's bottom edge), then stepped out of the exit 2 units at a time and up to a stair step, then the rest of the move
  traced. The eye is moved in front of the exit plane if it ended behind it.
- A fast entry (>300 u/s into the plane) stopped inside the hole (`UTIL_HL2MPPortal_FindBlockedEntry`: the part through
  the hole hit something in front of the exit before the eye crossed) goes through right away with its pre-move
  velocity; it meets the obstacle on the exit side. Gravity for the part of the
  tick after the crossing is redone in the exit frame, and height gained pushing out of a floor exit is taken out of
  the exit speed, so floor loops keep their energy.
- Client: the view turn's dropped part (roll, pitch past vertical) is stored as a rotation and eased out over
  `cl_portal_view_ease` in `C_HL2MP_Player::CalcView` (camera only).
- Client: on the first prediction of a transition the prediction-error smoothing offset is cleared and
  `C_HL2MP_Player::PostThink` resets position interpolation, so the camera never blends across the portal.
- Only portals on sloped surfaces (normal not along a world axis) still teleport players on the server.

## Held objects through portals (server, shared grab controller)

`CGrabController` (gravity gun and `+use` pickup) keeps a stack of portals between holder and object. The hold target
is computed in the holder's space as usual and mapped through the stack (`TransformThroughPortals`). The wall-clearance
trace for the hold distance uses the portal trace, so it reaches through a portal instead of stopping at the wall.
`TeleportEntity` calls `PhysCannon_HeldObjectWentThroughPortal` (push / pop at the object end) and
`PhysCannon_HolderWentThroughPortal` (pop / push at the holder end; also called from the seamless walk). Punts and
throws are aimed through the stack. When the mapped target is behind the far portal (the holder pulls it back), it's
moved along that portal's plane into the hole (`ClampBehindPointToHole`, ellipse x0.6), so it comes back through
instead of being pressed against the wall beside the hole. The gravity gun networks `m_nHeldPortalCount`; the client stops predicting the
held object while it's non-zero. Only a chain deeper than 4 portals drops the object.

## Self-test (dedicated server)

`hl2mp_portal_selftest.cpp` (`sv_portal_selftest 1`) runs on map load, prints `PORTALTEST <name> PASS|FAIL`, then
`PORTALTEST DONE pass= fail=`, and quits. `scripts/selftest.sh` runs it on the real HL2DM dedicated server in CI.
- An empty server hibernates and doesn't advance `curtime`: HL2MP has no `sv_hibernate_when_empty`, so
  `GetServerGCLobby` returns a stub whose `ShouldHibernate()` is false during the test, and the test bot connects on
  the first frame (parked, noclip, non-solid) so the server simulates. A 180 s real-time watchdog reports stalls.
- Floor + wall portals near the first spawn: small props stay server-side, a sleeping prop falls through a portal
  opened under it, held-flag props don't crash, a trace and a rocket go through, and the bot walks into the wall
  portal carrying a box (`+use`), steered to its center: it must come out of the floor portal without a snap, still
  holding the box. Later scenarios: fast falls, floor-portal edges, blocked exits, explosions, a box thrown into the
  wall portal that must be moved across while straddling both planes (`prop_crosses_smoothly`), floor loops.

## Explosions (server)

`CHL2MPRules::RadiusDamage` runs the normal blast, then `UTIL_HL2MPPortal_RadiusDamage`: for each open portal whose
hole is within the radius, in front of it and in sight (brush trace), the blast goes off again 2 units out of the
linked portal with radius `R - d` and damage `D * (R - d) / R` (`d` = distance to the nearest point of the hole), so
falloff over the whole path is the same. One hop (it calls the base `CGameRules::RadiusDamage`).

## Soundscapes (server)

`CEnvSoundscape::UpdateForPlayer` (HL2MP) doesn't switch a player's soundscape within `sv_portal_soundscape_hold`
seconds of their last portal trip (`UTIL_HL2MPPortal_NotePlayerTransit`, called by the movement transition), so a
trip into another area and back doesn't restart ambient loops and announcements.

## Versions and updates

`scripts/package.sh` writes the release tag to `hl2dm_portal/version.txt`. The server reads it into the replicated
`sv_portal_mod_version`; the client compares it with its own when the local player is created and warns in chat on a
mismatch (release tags only). `packaging/update.bat` (in the mod folder) asks the GitHub API for the latest release,
downloads and runs that release's `install.bat` with the current sourcemods folder; the desktop launchers call it in
quiet mode. It runs from a copy in `%TEMP%` and the launchers do their work on one line, because the install rewrites
both files while they run.

## Bullets (shared)

`UTIL_HL2MPPortal_TraceRay` traces, and when the trace hits a world surface where an open portal lies, re-starts the
trace from the mapped hit point on the exit portal with the mapped direction (max 4 portals). `CBaseEntity::FireBullets`
uses it in HL2MP for both line and hull pellets. HL2MP draws impacts client-side via `C_TEHL2MPFireBullets`, which
re-simulates the shot, so that file uses the same function. Both DLLs see the same portals because portals are always
transmitted (`FL_EDICT_ALWAYS`).

## SLAM tripmine laser (server)

`CTripmineGrenade::TraceBeam` uses the same portal trace and returns the beam's total length and portal count.
`MakeBeam` draws the first segment as before (mine attachment to first hit) plus one `CBeam` per segment after each
portal. `BeamBreakThink` re-traces every 0.05 s and detonates when it hits a living thing, the total length changes by
more than the stock tolerance (0.001 of 2048 units), or the number of portals on the path changes.

## Seeing through (client)

Classic render-to-texture portal, done with stock SDK pieces (no custom shaders, no extra content):

1. **Render targets**: `CHL2MPRenderTargets` creates four full-framebuffer render targets at startup (two views, two
   portal-in-portal views).
2. **Pick portals**: `DrawHL2MPPortalViews` (called from `CViewRender::RenderView` right before the main scene) takes
   open, front-facing, on-screen portals, closest first, up to `cl_portal_max_views`.
3. **Virtual camera**: main view origin/angles transformed by `M`. Same FOV and viewport size as the main view.
4. **Render**: `Push3DView` into the target, push a **custom clip plane** at the exit portal (hides whatever is
   between the virtual camera and the exit wall), and draw with **custom vis** from just in front of the exit (the
   camera itself is usually inside or behind a wall, where normal vis would cull the room). Then draw the local player
   explicitly, since first-person players aren't in the render lists.
5. **Show it**: the portal's `DrawModel` draws a finely tessellated ellipse whose texture coordinates are each vertex's
   main-view **screen position**. Since the target was rendered with the same projection, each screen pixel samples the
   matching target pixel, so the portal looks like a hole into the other side. Tessellation keeps the error small
   because the rasterizer interpolates UVs perspective-correctly, not linearly in screen space.
6. **Server side of it**: `CHL2MP_Player::SetupVisibility` adds the exit side of every open portal in the player's PVS,
   so entities over there are networked and actually show up in the view.

Views run every frame, so what you see through a portal is live.

### Cost and quality levels

Each live view redraws the scene, so views are budgeted by `cl_portal_quality` (`viewrender.cpp`,
`s_HL2MPPortalQuality`): a ladder of levels, cheapest first, each setting the number of views, portal-in-portal
recursion, view resolution and a minimum on-screen size. Presets 1-3 are entries of it; 4 (auto) moves one step down
when the smoothed frame time is 10% over `1/cl_portal_auto_fps` while views are drawn, and one step up after 2 s with
20% headroom. A raise followed by a drop within 3 s blocks raising for 30 s, so it doesn't flicker between levels.
- **Resolution**: views render into the top left of their target at a fraction of the screen size with the same
  projection, so the screen-space mapping just scales (`g_flHL2MPPortalUScale/VScale`). Fill cost ~ resolution².
- **Scissor**: the portal's screen rect (from its corners, padded 2 px) scissors the view; pixels outside it are never
  sampled. CPU cost (culling, draw calls) is still that of a full view.
- **Small portals** (below the level's minimum size) draw solid, as do portals beyond the view budget.
- **Water** in portal views is forced cheap below the top level (like Valve's Portal), skipping its own
  reflection/refraction views.
- **Meshes** (fill, tunnel, rim) are indexed dynamic meshes built from a precomputed unit circle; the live fill uses
  10 rings within 256 units, 6 within 1024, 3 beyond; the solid fill 1.

### Frame cache

`CHL2MPPortal` caches its forward/right/up, the world axis of its normal and `MatrixThisToLinked()`, keyed on its
origin/angles (`UpdateFrameCache`, compared on each read) and a global version counter, so a re-created portal never
reuses a stale matrix. Every accessor (`GetPortalVectors`, `GetSurfaceNormal`, `GetPlaneAxis`, `IsPointInEllipse`,
`MatrixThisToLinked`) goes through it; it works the same on server and client (no hooks on placement or networking).

## Portal ownership

Each `weapon_portalgun` creates its two portals lazily on first shot and links them. Placing re-uses the same entity
(moved with `Teleport` so clients don't interpolate). `+reload` fizzles both. Removing the gun removes its portals.
