# Architecture

Zero-dependency Node.js app. One process serves the UI, a JSON API, and a Server-Sent Events stream,
and spawns the AI CLIs as child processes.

```
 Browser (public/)                       server.js
 ───────────────────                     ─────────────────────────────────────────
 app.js  ── POST /api/messages ───────▶  store.add() ─▶ broadcast('message:new')
         ◀── SSE /api/events ──────────  orchestrator.onHumanMessage()
                                            │ route(mentions)
                                            ├─ none    → nothing
                                            ├─ single  → AgentRunner(id).run(prompt)
                                            └─ collab  → lead/helper loop (below)
                                          AgentRunner: spawn CLI, prompt on stdin,
                                          read stdout / --output-last-message file
```

## Files

| File | Role |
|---|---|
| `server.js` | HTTP server: static files, REST API, SSE hub, startup banner, `--open`. |
| `lib/config.js` | Loads `config.default.json` + `config.json` overrides + env; mock wiring. |
| `lib/store.js` | In-memory chat state, debounced atomic save to `data/chat.json`. |
| `lib/mentions.js` | `parseMentions()` and `route()` = the ping rules. |
| `lib/agents.js` | `AgentRunner`: one serial queue per AI, spawn/cancel/timeout, Windows-safe. `probe()` = `--version` check. |
| `lib/orchestrator.js` | Builds prompts, runs single replies and the `@Both` sync loop, updates the pending message live. |
| `public/` | UI: `index.html`, `style.css`, `app.js` (state, render, SSE, composer), `markdown.js` (safe renderer). |
| `doctor.js` | Setup wizard / diagnostics (install CLIs, workdir, desktop shortcut). |
| `mock/mock-agent.js` | Fake CLI for demo and tests. |
| `test/smoke.js` | Unit + end-to-end test using mock agents. |

## Message lifecycle

1. Human POSTs a message. Server stores it (with parsed `mentions`) and broadcasts `message:new`.
2. `route()` decides: `none` / `single` / `collab`. **Only human messages trigger AIs**, so AIs
   mentioning each other in replies can never cause loops.
3. The orchestrator immediately creates a **pending** AI message (`pending: true`, `replyTo` = source),
   so the UI shows "thinking…" with a Stop button.
4. When the CLI finishes, the pending message is patched with `text` (or `error`) and
   `message:update` is broadcast.

## The @Both sync protocol

The lead is told it may include one block per turn:

```
[[to:codex]]
message for Codex
[[/to]]
```

Loop (max `collab.maxRounds`):

1. Run lead with: chat history + user message + sync log so far.
2. If the output has a `[[to:<helper>]]` block → append `{from: lead, to: helper, text}` to `sync`,
   run the helper with the same context + sync log, append its reply `{from: helper, to: lead}`, repeat.
3. No block (or round limit hit) → that output is the final answer.

`sync` lives on the final message and renders as `<details class="sync">` with italic content,
collapsed by default (sidebar option to expand). It updates live while the AIs talk.

Every CLI call is **stateless** (no `--resume`): context is rebuilt from chat history each time.
Simpler, crash-proof, and identical for both CLIs. Cost: some repeated tokens per turn.

## Process handling (Windows notes)

- `claude`/`codex` are `.cmd` shims on Windows → must spawn with `shell: true` (cmd.exe).
  cmd.exe re-parses args, so user text is **never** put in args: it goes through stdin.
  Fixed args with spaces are quoted by `quoteWin()`.
- Cancel/timeout: `taskkill /T /F` on Windows (kills cmd.exe *and* the CLI under it);
  POSIX spawns `detached` and kills the whole process group with `process.kill(-pid)`.
- Exit code 9009 (cmd) / 127 (sh) = command not found → friendly error.

## API

| Method | Path | Body | |
|---|---|---|---|
| GET | `/api/state` | | channels, last 500 messages/channel, agents, online, workdir |
| GET | `/api/events?name=` | | SSE: `message:new/update/delete`, `channel:cleared`, `channels`, `presence`, `typing`, `agents`, `agents:status`, `workdir` |
| POST | `/api/messages` | `{channel, author, text, replyTo?}` | send (triggers routing) |
| PATCH | `/api/messages/:id` | `{author, text}` | edit own |
| DELETE | `/api/messages/:id` | `{author}` | delete own / any AI message (cancels if pending) |
| POST | `/api/messages/:id/react` | `{author, emoji}` | toggle reaction |
| POST | `/api/messages/:id/cancel` | | stop pending AI run |
| POST | `/api/messages/:id/retry` | | rerun failed AI reply |
| POST | `/api/channels` | `{name}` | create |
| DELETE | `/api/channels/:id` | | delete (not `general`) |
| POST | `/api/channels/:id/clear` | | clear messages |
| POST | `/api/typing` | `{name, channel, on}` | typing indicator |
| POST | `/api/health` | | re-probe CLIs |
| POST | `/api/workdir` | `{workdir}` | change + persist AI folder |

Scripts or other tools can post into the chat with the same API (e.g. `curl` a message).
If `token` is set, send it as `x-token` header or `?token=`.
