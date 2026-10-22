# Roadmap / known limits

## Known limits
- Each AI turn is stateless (history re-sent). Long threads cost more tokens; `historyMessages` caps it.
- No streaming of partial AI output: text appears when the CLI finishes (status + sync update live).
- Names are trust-based (no accounts). Fine on localhost; use `token` on a LAN.
- Edits/deletes of human messages are checked by display name only.
- One CLI process per AI at a time (queued). Different AIs run in parallel.
- Verified: Linux + real Claude CLI + Codex arg parsing; Windows paths reviewed but not run in CI.

## Ideas
- Stream output (`claude -p --output-format stream-json`, `codex exec --json`) into the pending bubble.
- Optional session resume (`claude --resume`, `codex exec resume`) per channel to save tokens.
- File/image attachments passed to the AIs.
- Per-channel workdir and per-channel lead.
- Let a single-pinged AI escalate to the other ("ask Codex") without `@Both`.
- Tray app / packaged `.exe` (e.g. Node SEA) so Node needn't be installed separately.
- Full Linux polish: systemd user service, `.desktop` launcher.
- Threads view, pinned messages, read receipts.
