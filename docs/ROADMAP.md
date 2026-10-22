# Roadmap and known limitations

## Known limitations
- **Not play-tested yet.** Compiles and links; the math is unit-tested; runtime behavior needs a pass in game.
- **Walls stay solid.** Entities are caught in front of the wall and moved, not carved through it. So nothing is ever
  "half through" a portal, and you can't stand in one or poke a prop through partway.
- **Big things fit anyway.** Only the center has to cross inside the ellipse; a prop wider than the portal still goes.
- **Two live views per frame** (render target count). More open portals on screen draw as solid tinted ovals.
- **No recursion.** A portal seen inside a portal view is solid color.
- **Screen-space UV approximation.** Fine tessellation hides it at normal distances; very close up, small seams are
  possible, as is a brief wall flash when the near plane crosses the portal.
- **3D skybox** isn't drawn in portal views (2D sky only).
- **Own weapon** isn't drawn on your model in portal views (first-person weapon uses the view model).
- **Placement is server-only**, so a laggy client sees its portal appear after a round trip.
- **Content reuse.** Gun uses the gravity gun models/sounds and HUD icon. Portals are drawn procedurally.
- Off-center players (hull outside the hole) and players at non-axis-aligned portals snap through on the server, unpredicted.

## Ideas / next steps
1. Play-test pass: tune `sv_portal_lookahead`, exit gap, placement step; check prediction feel through portals.
2. Collision carving: let entities partially enter (Portal's "portal simulator" approach: clone geometry around the
   exit, disable world collision behind the entry).
3. Stencil-buffer rendering instead of render targets (exact, recursion-capable, no UV approximation).
4. Portal-specific model, sounds, HUD icon, portal open/close particle effects.
5. Teams: team-colored pairs, `mp_portal_*` server rules (friendly portals only, per-map disable via a `func_noportal`).
6. Sounds through portals, and AI hearing/sight through them.
7. Clear portals on death (cvar), and when a player switches team or disconnects.
