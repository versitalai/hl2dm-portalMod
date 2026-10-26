# Roadmap and known limitations

## Known limitations
- **Projectiles** are caught in front of the wall and moved, not carved through it. Players walk into axis-aligned
  portals and physics props slide through them; ragdolls, props wider than the hole and portals on slopes still
  pop through whole.
- **Sliding props ignore the world, not other props**: a prop part way through doesn't collide with the world at all
  (so nothing next to the hole stops it either), and a prop or static prop in the way on the exit side blocks it
  (it backs out after 4 s). Portal clones the geometry around the exit instead.
- **The copy of a sliding prop** is drawn without shadows, and only the prop's own model (no attached effects).
- **Big things fit anyway.** Only the center has to cross inside the ellipse; a prop wider than the portal still goes.
- **Two live views per frame** (render target count). More open portals on screen draw as solid tinted ovals.
- **One level of recursion** (High only). A portal inside a portal inside a portal is solid color.
- **Performance settings aren't measured in game yet.** The quality ladder's numbers are estimates; a play-test on a
  slow PC should tune them. Auto judges by total frame time, so on a PC that's slow without portals it settles on Off.
- **Scissoring saves GPU fill, not CPU**: each live view still culls and submits a full scene. An off-center frustum
  around the portal would cut that too.
- **Screen-space UV approximation.** Fine tessellation hides it at normal distances; very close up, small seams are
  possible, as is a brief wall flash when the near plane crosses the portal.
- **3D skybox** isn't drawn in portal views (2D sky only).
- **Own weapon** isn't drawn on your model in portal views (first-person weapon uses the view model).
- **Placement is server-only**, so a laggy client sees its portal appear after a round trip.
- **Content reuse.** Gun uses the gravity gun models/sounds and HUD icon. Portals are drawn procedurally.
- Players at portals on sloped surfaces snap through on the server, unpredicted.
- **Linux installs don't self-update** (`update.bat` is Windows only); rerun `install.sh` from a new release.
- **Explosion effects** (fireball, sound) stay on the blast's side; the damage and its push go through.
- **Blocked-exit check uses one hull test** centered on the exit hole: a hole blocked only off-center still lets you in,
  then the exit push-out deals with it.
- **Soundscapes switch late after a portal trip**: the old one is kept for `sv_portal_soundscape_hold` seconds (so
  quick back-and-forth trips don't restart it), then the new area's starts as usual.

## Ideas / next steps
1. Play-test pass: tune `sv_portal_lookahead`, exit gap, placement step; check prediction feel through portals.
2. Full collision carving for props (Portal's "portal simulator": clone the geometry around the exit so a prop part
   way through collides on both sides; today it ignores the world while sliding).
3. Stencil-buffer rendering instead of render targets (exact, recursion-capable, no UV approximation).
4. Portal-specific model, sounds, HUD icon, portal open/close particle effects.
5. Teams: team-colored pairs, `mp_portal_*` server rules (friendly portals only, per-map disable via a `func_noportal`).
6. Sounds through portals, and AI hearing/sight through them.
7. Clear portals on death (cvar), and when a player switches team or disconnects.
