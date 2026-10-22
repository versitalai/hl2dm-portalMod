# AGENTS.md — notes for Codex / AI assistants (same as CLAUDE.md)

- Zero dependencies on purpose: Node >= 18 stdlib only. Don't add npm packages without a strong reason.
- Read `docs/ARCHITECTURE.md` first. Log changes in `docs/CHANGELOG.md`; limits/ideas in `docs/ROADMAP.md`.
- Ping rules live in `lib/mentions.js`; the `@Both` loop in `lib/orchestrator.js`; CLI spawning in `lib/agents.js`.
- Windows is the primary target: never pass user text as CLI args (cmd.exe re-parses); use stdin.
- Verify with `npm test` (mock agents, no CLIs needed). UI: `npm run mock`.
- Style: tabs, single quotes, `'use strict'`, small comments explaining *why*.
