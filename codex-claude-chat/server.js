#!/usr/bin/env node
'use strict';
// Codex-Claude Chat server: static UI + JSON API + Server-Sent Events. No dependencies.
//
// SSE vs WebSocket: the browser only needs a push channel *from* the server; sends go
// over ordinary POSTs. SSE (EventSource) gives that over plain HTTP, reconnects on its
// own, and needs no library on either side.
const http = require('http');
const fs = require('fs');
const path = require('path');
const { loadConfig } = require('./lib/config');
const { Store } = require('./lib/store');
const { AgentRunner, probe } = require('./lib/agents');
const { Orchestrator } = require('./lib/orchestrator');
const { parseMentions } = require('./lib/mentions');

const cfg = loadConfig();
const store = new Store(cfg.dataDir);
const runners = {};
for (const a of Object.values(cfg.agents)) runners[a.id] = new AgentRunner(a, cfg);

// ---- SSE hub --------------------------------------------------------------------
const clients = new Set(); // { res, name }

function send(res, event, data) {
	res.write(`event: ${event}\ndata: ${JSON.stringify(data)}\n\n`);
}
function broadcast(event, data) {
	for (const c of clients) send(c.res, event, data);
}
function presence() {
	const names = [...new Set([...clients].map(c => c.name).filter(Boolean))];
	broadcast('presence', names);
}
setInterval(() => { for (const c of clients) c.res.write(': ping\n\n'); }, 25000).unref();

const orch = new Orchestrator({ cfg, store, runners, broadcast });

// Probe CLIs once at start (and on demand) so the UI can show "not installed".
let health = {};
function refreshHealth() {
	for (const a of Object.values(cfg.agents)) health[a.id] = probe(a, cfg);
	return health;
}
refreshHealth();

// ---- helpers ----------------------------------------------------------------------
const MIME = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.svg': 'image/svg+xml', '.png': 'image/png', '.ico': 'image/x-icon', '.json': 'application/json' };
const PUBLIC = path.join(__dirname, 'public');

function json(res, code, obj) {
	res.writeHead(code, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
	res.end(JSON.stringify(obj));
}
function body(req) {
	return new Promise((resolve, reject) => {
		let b = '';
		req.on('data', d => { b += d; if (b.length > 2e6) { reject(new Error('Body too large')); req.destroy(); } });
		req.on('end', () => { try { resolve(b ? JSON.parse(b) : {}); } catch (e) { reject(e); } });
	});
}
function cleanName(n) { return String(n || '').trim().replace(/[^\w .-]/g, '').slice(0, 32); }
function publicAgents() {
	return Object.values(cfg.agents).map(a => ({
		id: a.id, name: a.name, color: a.color,
		installed: !!(health[a.id] && health[a.id].ok),
		version: health[a.id] && health[a.id].version,
		error: health[a.id] && health[a.id].error,
		status: runners[a.id].busy ? 'busy' : 'idle',
	}));
}
function authorized(req, url) {
	if (!cfg.token) return true;
	return req.headers['x-token'] === cfg.token || url.searchParams.get('token') === cfg.token;
}

// ---- routes -----------------------------------------------------------------------
async function api(req, res, url) {
	const p = url.pathname;
	const m = req.method;

	if (p === '/api/events' && m === 'GET') {
		res.writeHead(200, { 'Content-Type': 'text/event-stream', 'Cache-Control': 'no-cache', Connection: 'keep-alive', 'X-Accel-Buffering': 'no' });
		res.write('retry: 2000\n\n');
		const c = { res, name: cleanName(url.searchParams.get('name')) };
		clients.add(c);
		presence();
		req.on('close', () => { clients.delete(c); presence(); });
		return;
	}

	if (p === '/api/state' && m === 'GET') {
		const messages = {};
		for (const ch of store.channels()) messages[ch.id] = store.list(ch.id).slice(-500);
		return json(res, 200, {
			channels: store.channels(), messages, agents: publicAgents(),
			online: [...new Set([...clients].map(c => c.name).filter(Boolean))],
			workdir: cfg.workdir, mock: !!cfg.mock, lead: cfg.collab.lead,
		});
	}

	if (p === '/api/health' && m === 'POST') {
		refreshHealth();
		broadcast('agents', publicAgents());
		return json(res, 200, publicAgents());
	}

	if (p === '/api/workdir' && m === 'POST') {
		const b = await body(req);
		const dir = path.resolve(String(b.workdir || '').trim());
		if (!b.workdir || !fs.existsSync(dir) || !fs.statSync(dir).isDirectory()) return json(res, 400, { error: 'Folder does not exist' });
		cfg.workdir = dir;
		// Persist into config.json so it survives restarts.
		const userPath = path.join(cfg.root, 'config.json');
		const user = JSON.parse(fs.readFileSync(userPath, 'utf8'));
		user.workdir = dir;
		fs.writeFileSync(userPath, JSON.stringify(user, null, 2) + '\n');
		broadcast('workdir', dir);
		return json(res, 200, { workdir: dir });
	}

	if (p === '/api/channels' && m === 'POST') {
		const b = await body(req);
		const ch = store.addChannel(String(b.name || ''));
		broadcast('channels', store.channels());
		return json(res, 200, ch);
	}

	let mm;
	if ((mm = p.match(/^\/api\/channels\/([\w-]+)$/)) && m === 'DELETE') {
		store.deleteChannel(mm[1]);
		broadcast('channels', store.channels());
		return json(res, 200, { ok: true });
	}
	if ((mm = p.match(/^\/api\/channels\/([\w-]+)\/clear$/)) && m === 'POST') {
		store.clear(mm[1]);
		broadcast('channel:cleared', { channel: mm[1] });
		return json(res, 200, { ok: true });
	}

	if (p === '/api/messages' && m === 'POST') {
		const b = await body(req);
		const author = cleanName(b.author);
		const text = String(b.text || '').slice(0, 100000);
		if (!author) return json(res, 400, { error: 'Name required' });
		if (!text.trim()) return json(res, 400, { error: 'Empty message' });
		if (Object.values(cfg.agents).some(a => a.name.toLowerCase() === author.toLowerCase())) {
			return json(res, 400, { error: 'That name belongs to an AI. Pick another.' });
		}
		const msg = store.add({
			channel: String(b.channel || 'general'), author, authorType: 'human', text,
			replyTo: b.replyTo || null, mentions: parseMentions(text),
		});
		broadcast('typing', { channel: msg.channel, name: author, on: false });
		broadcast('message:new', msg);
		orch.onHumanMessage(msg);
		return json(res, 200, msg);
	}

	if ((mm = p.match(/^\/api\/messages\/(\w+)$/))) {
		const msg = store.get(mm[1]);
		if (!msg) return json(res, 404, { error: 'Not found' });
		const b = m === 'GET' ? {} : await body(req);
		const who = cleanName(b.author);
		if (m === 'PATCH') {
			if (msg.authorType !== 'human' || msg.author !== who) return json(res, 403, { error: 'You can only edit your own messages' });
			const text = String(b.text || '');
			if (!text.trim()) return json(res, 400, { error: 'Empty message' });
			const u = store.update(msg.id, { text, editedAt: Date.now(), mentions: parseMentions(text) });
			broadcast('message:update', u);
			return json(res, 200, u);
		}
		if (m === 'DELETE') {
			// Humans delete their own messages; anyone may delete AI messages (it's your chat).
			if (msg.authorType === 'human' && msg.author !== who) return json(res, 403, { error: 'You can only delete your own messages' });
			if (msg.pending) orch.cancel(msg.id);
			store.remove(msg.id);
			broadcast('message:delete', { id: msg.id, channel: msg.channel });
			return json(res, 200, { ok: true });
		}
	}

	if ((mm = p.match(/^\/api\/messages\/(\w+)\/react$/)) && m === 'POST') {
		const b = await body(req);
		const who = cleanName(b.author);
		const emoji = String(b.emoji || '').slice(0, 16);
		const msg = store.get(mm[1]);
		if (!msg || !who || !emoji) return json(res, 400, { error: 'Bad reaction' });
		const r = msg.reactions || (msg.reactions = {});
		const set = new Set(r[emoji] || []);
		set.has(who) ? set.delete(who) : set.add(who);
		if (set.size) r[emoji] = [...set]; else delete r[emoji];
		store.save();
		broadcast('message:update', msg);
		return json(res, 200, msg);
	}

	if ((mm = p.match(/^\/api\/messages\/(\w+)\/cancel$/)) && m === 'POST') {
		return json(res, 200, { ok: orch.cancel(mm[1]) });
	}

	if ((mm = p.match(/^\/api\/messages\/(\w+)\/retry$/)) && m === 'POST') {
		// Re-run the AI turn for a failed reply: drop the failed message, re-route its source.
		const failed = store.get(mm[1]);
		const src = failed && failed.replyTo && store.get(failed.replyTo);
		if (!failed || failed.pending || !src) return json(res, 400, { error: 'Original message is gone; send it again.' });
		store.remove(failed.id);
		broadcast('message:delete', { id: failed.id, channel: failed.channel });
		orch.onHumanMessage(src);
		return json(res, 200, { ok: true });
	}

	if (p === '/api/typing' && m === 'POST') {
		const b = await body(req);
		const name = cleanName(b.name);
		if (name) broadcast('typing', { channel: String(b.channel || 'general'), name, on: !!b.on });
		return json(res, 200, { ok: true });
	}

	return json(res, 404, { error: 'No such endpoint' });
}

function serveStatic(req, res, url) {
	let rel = decodeURIComponent(url.pathname);
	if (rel === '/') rel = '/index.html';
	const file = path.normalize(path.join(PUBLIC, rel));
	if (!file.startsWith(PUBLIC + path.sep)) { res.writeHead(403); return res.end(); }
	fs.readFile(file, (err, data) => {
		if (err) { res.writeHead(404); return res.end('Not found'); }
		res.writeHead(200, { 'Content-Type': MIME[path.extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-cache' });
		res.end(data);
	});
}

const server = http.createServer(async (req, res) => {
	const url = new URL(req.url, 'http://x');
	if (url.pathname.startsWith('/api/')) {
		if (!authorized(req, url)) return json(res, 401, { error: 'Bad or missing token' });
		try { await api(req, res, url); } catch (e) { json(res, 400, { error: e.message }); }
		return;
	}
	serveStatic(req, res, url);
});

server.on('error', e => {
	if (e.code === 'EADDRINUSE') console.error(`\nPort ${cfg.port} is already in use. Is the chat already running? Change "port" in config.json.\n`);
	else console.error(e);
	process.exit(1);
});

server.listen(cfg.port, cfg.host, () => {
	const shown = cfg.host === '0.0.0.0' ? 'localhost' : cfg.host;
	const link = `http://${shown}:${cfg.port}/${cfg.token ? '?token=' + cfg.token : ''}`;
	console.log('\n  Codex-Claude Chat is running');
	console.log(`  Open:     ${link}`);
	console.log(`  Workdir:  ${cfg.workdir}${cfg.mock ? '   (MOCK agents)' : ''}`);
	for (const a of publicAgents()) console.log(`  ${a.name.padEnd(8)}  ${a.installed ? 'ok  ' + (a.version || '') : 'NOT FOUND - ' + (a.error || '').split('\n')[0]}`);
	console.log('  Stop:     Ctrl+C\n');
	if (process.argv.includes('--open')) openBrowser(link);
});

function openBrowser(link) {
	const { spawn } = require('child_process');
	const [cmd, args] = process.platform === 'win32' ? ['cmd', ['/c', 'start', '', link]]
		: process.platform === 'darwin' ? ['open', [link]] : ['xdg-open', [link]];
	try { spawn(cmd, args, { stdio: 'ignore', detached: true, windowsHide: true }).unref(); } catch { /* no browser */ }
}

function shutdown() {
	for (const r of Object.values(runners)) r.cancel();
	try { store.flush(); } catch { /* ignore */ }
	process.exit(0);
}
process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
