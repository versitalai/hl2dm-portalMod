# Codex-Claude Chat

A local chat room where you, **Claude Code** and **OpenAI Codex** talk to each other.

- `@Claude` → only Claude answers.
- `@Codex` → only Codex answers.
- `@Both` (or `@AI`, `@All`, or naming both) → the **lead** AI (Claude by default) gets the message,
  consults the other AI as often as it needs, then posts one final answer. Their back-and-forth is
  attached to that answer as a collapsible, italic **AI sync** dropdown, so you can hide it.
- No AI ping → plain chat between people. AIs stay quiet.

Runs on **Windows** (primary) and **Linux** (macOS works too). No npm packages to install: plain Node.js.

## Setup on Windows (5 minutes)

1. **Install Node.js 18+ (LTS)** from <https://nodejs.org>, and **Git for Windows** from
   <https://git-scm.com/download/win> (Claude Code on Windows uses its Git Bash). Accept the defaults.
2. **Download this folder** (or `git clone` the repo) anywhere, for example `Documents\codex-claude-chat`.
3. **Double-click `start.bat`.** The first run walks you through setup:
   - checks Node, `claude` and `codex`;
   - offers to install missing CLIs (`npm install -g @anthropic-ai/claude-code @openai/codex`);
   - asks which folder the AIs should work in (your project folder);
   - offers a Desktop shortcut.
4. **Sign each CLI in once** (in any terminal): run `claude` and log in, then run `codex login`.
5. The chat opens in your browser at <http://127.0.0.1:4747>. Pick a name, type `@Both hello`.

After that, just double-click the **Codex-Claude Chat** shortcut (or `start.bat`).
Run `doctor.bat` anytime to re-check setup or change the AI working folder.

No CLIs yet? `start.bat --mock` runs the whole UI with fake AIs so you can try it.

## Setup on Linux

```sh
# Node 18+ from your package manager or nodejs.org
npm install -g @anthropic-ai/claude-code @openai/codex
claude          # log in once, then /exit
codex login
./start.sh      # or: node server.js --open
```

## Chat features

| | |
|---|---|
| Pings | `@Claude`, `@Codex`, `@Both`; `@name` pings a person; `@everyone` / `@here` all people. Autocomplete pops up as you type `@`. Quick-ping buttons above the box. |
| Notifications | Ping sound, desktop notifications (toggle in sidebar), highlighted mentions, unread badges per channel, unread count in the tab title. You're also notified when an AI finishes replying to you. |
| Messages | Markdown (bold, italic, lists, links, quotes, code blocks with Copy), edit (✎ or ↑ in an empty box), delete, reply/quote (click the quote to jump), emoji reactions, copy text, "(edited)" marker, day separators. |
| AI controls | Live "thinking…" / "syncing…" status, **Stop** button to cancel a run, **Retry** on errors, AI status dots (ready / working / not installed) with ↻ re-check. |
| Rooms | Multiple channels (`+` or `/channel name`), delete channel, clear channel (`/clear`), search, export a channel to Markdown (⤓). |
| People | Online list, typing indicators, multiple people can join from other PCs (see LAN below). |
| Other | Dark/light theme, mobile layout, history saved to `data/chat.json`, `/nick`, `/help`. |

## How the AIs are run

Each reply runs the CLI headless, in your chosen **workdir**:

- Claude: `claude -p --output-format text --permission-mode acceptEdits`
- Codex: `codex exec --skip-git-repo-check --sandbox workspace-write ... -`

The prompt (recent chat + the message) goes in through **stdin**. Both AIs can read and **edit files
in the workdir** with these defaults. Want read-only? Override `args` in `config.json`:
drop `--permission-mode acceptEdits` for Claude and use `--sandbox read-only` for Codex.

Tip: put a `CLAUDE.md` and an `AGENTS.md` in your workdir. Claude reads the first, Codex the second,
so both learn your project conventions.

## Configuration

`config.json` (created on first run) holds **only your overrides**; every option and its default is in
`config.default.json`. Copy a key over to change it.

| Key | Default | Meaning |
|---|---|---|
| `port` / `host` | `4747` / `127.0.0.1` | Where the server listens. |
| `workdir` | folder you started from | Folder the AIs work in. Also changeable from the sidebar. |
| `collab.lead` | `claude` | Who leads `@Both`. Set `codex` to flip roles. |
| `collab.maxRounds` | `4` | Max lead → helper consultations per `@Both`. |
| `historyMessages` | `20` | How many recent messages each AI sees as context. |
| `agents.<id>.command` / `args` | see file | CLI binary and flags. `{outfile}` becomes a temp file path. |
| `agents.<id>.timeoutSec` | `900` | Kill a run after this long. |
| `agents.<id>.mock` / `mock` | `false` | Fake one AI / all AIs (testing without a CLI). |
| `token` | empty | If set, the API requires it (`http://host:4747/?token=...`). |

Env overrides: `PORT`, `HOST`, `WORKDIR`, `MOCK=1`, `MOCK_AGENTS=codex`, `DATA_DIR`.

### Using it from other PCs (LAN)

Set `"host": "0.0.0.0"` **and** a `"token"`, then open `http://<your-ip>:4747/?token=<token>` on the
other machine. Anyone with the link can make the AIs edit files in your workdir, so keep the token secret.

## Troubleshooting

| Problem | Fix |
|---|---|
| "Claude/Codex CLI not found" banner | Install it (`doctor.bat`), open a **new** terminal so PATH refreshes, click ↻. |
| AI replies with a login/auth error | Run `claude` or `codex login` once in a terminal. |
| "Port 4747 is already in use" | Chat is already running (check the taskbar), or set another `port`. |
| AI says it can't edit files | Check the `args` permissions above and that `workdir` is right. |
| Run hangs | Press **Stop** on the message; raise/lower `timeoutSec`. |
| Codex errors about the sandbox on Windows | In `config.json` set Codex `args` with `"--sandbox", "read-only"`, or run Codex under WSL. |
| Claude says it needs Git Bash | Install Git for Windows, or set `CLAUDE_CODE_GIT_BASH_PATH` to your `bash.exe`. |

## Development

```sh
npm test          # end-to-end smoke test with mock AIs (no CLIs needed)
npm run mock      # UI with mock AIs
```

Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) first. Changes go in [docs/CHANGELOG.md](docs/CHANGELOG.md),
ideas and known limits in [docs/ROADMAP.md](docs/ROADMAP.md).
