# Changelog

## 1.0.0 — 2026-10-22
- First release: local web chat for humans + Claude Code + OpenAI Codex.
- Ping routing: `@Claude`, `@Codex`, `@Both`/`@AI`/`@All`; human pings `@name`, `@everyone`, `@here`.
- `@Both` collaboration: lead AI consults the other through `[[to:<id>]]` blocks; exchange shown as a
  collapsible italic "AI sync" dropdown on the final answer, updated live.
- Chat features: channels, markdown, edit/delete, reply/quote, reactions, search, export, typing
  indicators, presence, unread badges, sound + desktop notifications, dark/light theme, mobile layout.
- AI controls: Stop, Retry, status dots, CLI re-check, workdir switcher.
- Windows launcher (`start.bat`, `doctor.bat`, first-run wizard, Desktop shortcut); Linux `start.sh`.
- Mock agents + `npm test` smoke test.
- Codex args use `--sandbox workspace-write` (`--full-auto` was removed from `codex exec` in 0.16x).
