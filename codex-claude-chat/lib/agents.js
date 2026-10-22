'use strict';
// Runs one headless CLI turn (claude -p / codex exec) and returns its final text.
//
// Why stdin for the prompt: on Windows, `claude`/`codex` are .cmd shims, which Node
// can only launch through cmd.exe (shell: true). cmd.exe re-parses arguments, so a
// chat message containing quotes, %, &, | or newlines would break or even inject
// commands. Piping the prompt over stdin sidesteps all of that: args stay fixed.
const { spawn, spawnSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const IS_WIN = process.platform === 'win32';

function quoteWin(a) {
	return /[\s"&|<>^()%!]/.test(a) ? '"' + a.replace(/"/g, '""') + '"' : a;
}

function killTree(child) {
	if (!child || child.exitCode !== null) return;
	if (IS_WIN) {
		// child.kill() would only kill cmd.exe and orphan the real CLI process.
		spawnSync('taskkill', ['/pid', String(child.pid), '/T', '/F'], { windowsHide: true });
	} else {
		try { process.kill(-child.pid, 'SIGTERM'); } catch { try { child.kill('SIGTERM'); } catch { /* gone */ } }
	}
}

class AgentRunner {
	constructor(agent, cfg) {
		this.agent = agent;
		this.cfg = cfg;
		this.queue = Promise.resolve();
		this.current = null;   // { child, cancelled }
		this.pending = 0;      // queued + running jobs
	}

	get busy() { return this.pending > 0; }

	// Serialise runs per agent: one CLI process per agent at a time.
	run(prompt, onStart) {
		this.pending++;
		const job = this.queue.then(() => {
			if (onStart) onStart();
			return this._exec(prompt);
		}).finally(() => { this.pending--; });
		this.queue = job.catch(() => {});
		return job;
	}

	cancel() {
		if (this.current) { this.current.cancelled = true; killTree(this.current.child); }
	}

	_exec(prompt) {
		const a = this.agent;
		const outfile = path.join(os.tmpdir(), `ccchat-${a.id}-${process.pid}-${Date.now()}.txt`);
		const usesOutfile = a.args.some(x => x.includes('{outfile}'));
		const args = a.args.map(x => x.replace('{outfile}', outfile));
		let cmd = a.command;
		let spawnArgs = args;
		if (IS_WIN) { cmd = quoteWin(cmd); spawnArgs = args.map(quoteWin); }

		return new Promise((resolve, reject) => {
			let child;
			try {
				child = spawn(cmd, spawnArgs, {
					cwd: this.cfg.workdir,
					shell: IS_WIN,
					windowsHide: true,
					detached: !IS_WIN, // own process group on POSIX so cancel kills the whole tree
					env: { ...process.env, CCCHAT_AGENT: a.id },
				});
			} catch (e) {
				return reject(e);
			}
			const state = { child, cancelled: false };
			this.current = state;
			let stdout = '';
			let stderr = '';
			child.stdout.on('data', d => { stdout += d; });
			child.stderr.on('data', d => { stderr += d; if (stderr.length > 20000) stderr = stderr.slice(-20000); });
			const timer = setTimeout(() => { state.timedOut = true; killTree(child); }, (a.timeoutSec || 900) * 1000);

			child.on('error', e => {
				clearTimeout(timer);
				this.current = null;
				if (e.code === 'ENOENT') e = new Error(`"${a.command}" not found. Is the ${a.name} CLI installed and on PATH? Run doctor.`);
				reject(e);
			});
			child.on('close', code => {
				clearTimeout(timer);
				this.current = null;
				let text = '';
				if (usesOutfile) {
					try { text = fs.readFileSync(outfile, 'utf8'); fs.unlinkSync(outfile); } catch { /* fall back to stdout */ }
				}
				if (!text.trim()) text = stdout;
				text = text.trim();
				if (state.cancelled) return reject(new Error('Cancelled.'));
				if (state.timedOut) return reject(new Error(`Timed out after ${a.timeoutSec}s.`));
				if (code !== 0 && !text) {
					const tail = stderr.trim().split('\n').slice(-8).join('\n');
					// cmd.exe exit 9009 / sh exit 127 = command not found
					if (code === 9009 || code === 127) return reject(new Error(`"${a.command}" not found on PATH. Install the ${a.name} CLI or fix "command" in config.json.`));
					return reject(new Error(`${a.name} exited with code ${code}.\n${tail}`));
				}
				resolve(text || '(empty reply)');
			});
			child.stdin.on('error', () => {}); // CLI may exit before reading stdin
			child.stdin.end(prompt);
		});
	}
}

// Quick "is the CLI installed?" probe used by the doctor + status dots.
function probe(agent, cfg) {
	if (cfg.mock || agent.mock) return { ok: true, version: 'mock' };
	const r = spawnSync(IS_WIN ? quoteWin(agent.command) : agent.command, ['--version'], {
		shell: IS_WIN, windowsHide: true, encoding: 'utf8', timeout: 20000,
	});
	if (r.error || r.status !== 0) return { ok: false, error: (r.error && r.error.message) || (r.stderr || '').trim() || `exit ${r.status}` };
	return { ok: true, version: (r.stdout || '').trim().split('\n')[0] };
}

module.exports = { AgentRunner, probe };
