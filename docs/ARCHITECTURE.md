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

1. **Render targets**: `CHL2MPRenderTargets` creates two full-framebuffer render targets at startup.
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

## Portal ownership

Each `weapon_portalgun` creates its two portals lazily on first shot and links them. Placing re-uses the same entity
(moved with `Teleport` so clients don't interpolate). `+reload` fizzles both. Removing the gun removes its portals.
