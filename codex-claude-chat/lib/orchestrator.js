'use strict';
// Decides who answers a human message and drives the AI<->AI "sync" loop.
//
// Every CLI call is stateless: we hand the agent a prompt holding recent chat history
// (plus, in collab mode, the sync log so far). That keeps both CLIs interchangeable and
// means a crashed run never leaves a half-open session behind.
const { parseMentions, route } = require('./mentions');

const TO_RE = /\[\[to:([a-z][\w-]*)\]\]([\s\S]*?)(?:\[\[\/to\]\]|$)/i;

function fmtTime(ts) {
	const d = new Date(ts);
	return d.toTimeString().slice(0, 5);
}

class Orchestrator {
	constructor({ cfg, store, runners, broadcast }) {
		this.cfg = cfg;
		this.store = store;
		this.runners = runners;          // id -> AgentRunner
		this.broadcast = broadcast;      // (event, data) => void
		this.jobs = new Map();           // pending message id -> { agents:Set, cancelled }
	}

	agentIds() { return Object.keys(this.cfg.agents); }
	agentName(id) { return this.cfg.agents[id].name; }

	onHumanMessage(msg) {
		const r = route(msg.mentions || parseMentions(msg.text), this.agentIds());
		if (r.mode === 'single') this.single(msg, r.agents[0]).catch(e => console.error(e));
		else if (r.mode === 'collab') this.collab(msg).catch(e => console.error(e));
	}

	transcript(channel, beforeTs) {
		const n = this.cfg.historyMessages || 20;
		const list = this.store.list(channel)
			.filter(m => m.ts < beforeTs && !m.pending && m.authorType !== 'system')
			.slice(-n);
		if (!list.length) return '(no earlier messages)';
		return list.map(m => {
			const body = m.text.length > 1500 ? m.text.slice(0, 1500) + ' …[truncated]' : m.text;
			return `[${fmtTime(m.ts)}] ${m.author}: ${body}`;
		}).join('\n');
	}

	preamble(id) {
		const others = this.agentIds().filter(x => x !== id).map(x => this.agentName(x)).join(', ');
		return [
			`You are ${this.agentName(id)}, an AI participant in a group chat app ("Codex-Claude Chat") with human user(s) and another AI (${others}).`,
			`Your reply is posted verbatim as a chat message, so write it as one: markdown is fine, no preamble like "Here is my reply".`,
			`Working directory for any file work: ${this.cfg.workdir}`,
		].join('\n');
	}

	createPending(src, agentId, extra = {}) {
		const m = this.store.add({
			channel: src.channel,
			author: this.agentName(agentId),
			authorId: agentId,
			authorType: 'agent',
			text: '',
			replyTo: src.id,
			pending: true,
			status: 'thinking',
			mentions: [],
			...extra,
		});
		this.broadcast('message:new', m);
		return m;
	}

	patch(id, p) {
		const m = this.store.update(id, p);
		if (m) this.broadcast('message:update', m);
		return m;
	}

	setStatus() {
		const status = {};
		for (const [id, r] of Object.entries(this.runners)) status[id] = r.busy ? 'busy' : 'idle';
		this.broadcast('agents:status', status);
	}

	async runAgent(job, id, prompt) {
		if (job.cancelled) throw new Error('Cancelled.');
		job.agents.add(id);
		try {
			const run = this.runners[id].run(prompt);
			this.setStatus();
			return await run;
		} finally {
			job.agents.delete(id);
			this.setStatus();
		}
	}

	cancel(messageId) {
		const job = this.jobs.get(messageId);
		if (!job) return false;
		job.cancelled = true;
		for (const id of job.agents) this.runners[id].cancel();
		return true;
	}

	// ---- @claude / @codex: exactly one agent answers -------------------------------
	async single(src, id) {
		const pending = this.createPending(src, id);
		const job = { agents: new Set(), cancelled: false };
		this.jobs.set(pending.id, job);
		const prompt = [
			this.preamble(id),
			`${src.author} pinged you directly, so only you are answering.`,
			'',
			'## Recent chat (oldest first)',
			this.transcript(src.channel, src.ts),
			'',
			`## Message from ${src.author} (reply to this)`,
			src.text,
		].join('\n');
		try {
			const text = await this.runAgent(job, id, prompt);
			this.patch(pending.id, { text, pending: false, status: 'done', mentions: parseMentions(text) });
		} catch (e) {
			this.patch(pending.id, { pending: false, status: 'error', error: e.message });
		} finally {
			this.jobs.delete(pending.id);
		}
	}

	// ---- @both: lead answers, consulting the helper through [[to:...]] blocks ------
	async collab(src) {
		const lead = this.cfg.collab.lead in this.cfg.agents ? this.cfg.collab.lead : this.agentIds()[0];
		const helpers = this.agentIds().filter(x => x !== lead);
		const maxRounds = Math.max(1, this.cfg.collab.maxRounds || 4);
		const pending = this.createPending(src, lead, { sync: [], collab: true });
		const job = { agents: new Set(), cancelled: false };
		this.jobs.set(pending.id, job);
		const sync = [];
		const history = this.transcript(src.channel, src.ts);
		const helperList = helpers.map(h => `"${h}" (${this.agentName(h)})`).join(', ');

		const pushSync = (from, to, text) => {
			sync.push({ from, to, fromName: this.agentName(from), toName: this.agentName(to), text, ts: Date.now() });
			this.patch(pending.id, { sync: sync.slice() });
		};
		const syncLog = () => sync.length
			? sync.map(s => `--- ${s.fromName} -> ${s.toName} ---\n${s.text}`).join('\n\n')
			: '(nothing yet)';

		try {
			for (let round = 1; ; round++) {
				const last = round > maxRounds;
				const leadPrompt = [
					this.preamble(lead),
					`${src.author} pinged BOTH AIs. You are the lead: you own the final answer, and you can consult ${helperList}.`,
					`To send a message to another AI, include exactly one block:`,
					`[[to:${helpers[0]}]]`,
					`your message to them`,
					`[[/to]]`,
					`and stop there; their reply comes back to you in the next turn. Split work sensibly (e.g. one implements, the other reviews).`,
					`When you have what you need, reply with the final chat message for ${src.author} and NO [[to:...]] block.`,
					last ? `\nIMPORTANT: the consultation limit is reached. Give your final answer now, with no [[to:...]] block.` : '',
					'',
					'## Recent chat (oldest first)',
					history,
					'',
					`## Message from ${src.author}`,
					src.text,
					'',
					'## AI sync log so far',
					syncLog(),
				].join('\n');

				const out = await this.runAgent(job, lead, leadPrompt);
				const m = out.match(TO_RE);
				const target = m && helpers.includes(m[1].toLowerCase()) ? m[1].toLowerCase() : null;
				if (!m || !target || last) {
					const text = out.replace(TO_RE, '').trim() || out;
					this.patch(pending.id, { text, pending: false, status: 'done', sync, mentions: parseMentions(text) });
					return;
				}
				const note = (out.slice(0, m.index) + out.slice(m.index + m[0].length)).trim();
				const toHelper = m[2].trim();
				pushSync(lead, target, note ? `${note}\n\n${toHelper}` : toHelper);

				const helperPrompt = [
					this.preamble(target),
					`${src.author} pinged both AIs. ${this.agentName(lead)} is leading and has sent you a message.`,
					`Reply to ${this.agentName(lead)} (not to the user): your reply goes back to them, and they write the final answer.`,
					`Be concrete. If you changed files, list them.`,
					'',
					'## Recent chat (oldest first)',
					history,
					'',
					`## Original message from ${src.author}`,
					src.text,
					'',
					'## AI sync log so far',
					syncLog(),
				].join('\n');
				let reply;
				try {
					reply = await this.runAgent(job, target, helperPrompt);
				} catch (e) {
					if (job.cancelled) throw e;
					reply = `(${this.agentName(target)} failed: ${e.message})`;
				}
				pushSync(target, lead, reply);
			}
		} catch (e) {
			this.patch(pending.id, { pending: false, status: 'error', error: e.message, sync });
		} finally {
			this.jobs.delete(pending.id);
		}
	}
}

module.exports = { Orchestrator, TO_RE };
