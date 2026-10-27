# Notes for AI assistants / new developers

- Repo holds only the mod: `patches/hl2dm-portalmod.patch` (edits to stock SDK files) + `overlay/` (new files).
  The SDK is fetched at `SDK_COMMIT` by `scripts/setup.sh` into `sdk/` (gitignored).
- Workflow: `scripts/setup.sh` → edit in `sdk/` → `scripts/build_linux.sh` → `scripts/export.sh` → commit.
  New files go in `overlay/` by hand the first time; export only refreshes files already there.
- Packaging: `scripts/package.sh` → `dist/`; installers live in `packaging/` (server kit: `packaging/server/`). CI: `.github/workflows/build.yml`.
- Build: native ninja + host gcc works (no podman). `vpc` needs `VPC_NINJA_BUILD_MODE` set or it segfaults.
  Full build ~6 min on 4 cores; incremental seconds.
- No game content on the build box: only compile/link + `tests/run_math_test.sh` / `tests/run_carve_test.sh` can be verified here.
- Read `docs/ARCHITECTURE.md` first; log changes in `docs/CHANGELOG.md`; limitations/ideas in `docs/ROADMAP.md`.
- Keep SDK-touching edits behind `#ifdef HL2MP` and match Valve style (tabs, `m_` members, `C_` client aliases).
