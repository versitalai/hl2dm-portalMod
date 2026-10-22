#!/usr/bin/env node
'use strict';
// End-to-end test with mock agents: boots the server, drives the API, checks routing,
// the @both sync loop, chat features, and markdown escaping. `npm test`.
const assert = require('assert');
const http = require('http');
const fs = require('fs');
const os = require('os');
const path = require('path');
const vm = require('vm');
const { spawn } = require('child_process');
const { parseMentions, route } = require('../lib/mentions');

const ROOT = path.join(__dirname, '..');
const PORT = 47000 + Math.floor(Math.random() * 1000);
const DATA = fs.mkdtempSync(path.join(os.tmpdir(), 'ccchat-test-'));
const BASE = `http://127.0.0.1:${PORT}`;
let passed = 0;
const t = (name, fn) => fn().then(() => { passed++; console.log(`  ok  ${name}`); });

// ---- unit: mentions ----
const ids = ['claude', 'codex'];
assert.deepStrictEqual(route(parseMentions('hey @Claude fix this'), ids), { mode: 'single', agents: ['claude'] });
assert.deepStrictEqual(route(parseMentions('@codex go'), ids), { mode: 'single', agents: ['codex'] });
assert.strictEqual(route(parseMentions('@both plan it'), ids).mode, 'collab');
assert.strictEqual(route(parseMentions('@claude and @codex'), ids).mode, 'collab');
assert.strictEqual(route(parseMentions('no ping here, mail me a@claude.com'), ids).mode, 'none');
assert.strictEqual(route(parseMentions('`@claude` in code'), ids).mode, 'none');
assert.strictEqual(route(parseMentions('@everyone lunch'), ids).mode, 'none');
console.log('  ok  mention routing');

// ---- unit: markdown is XSS-safe ----
const sandbox = { window: {} };
vm.runInNewContext(fs.readFileSync(path.join(ROOT, 'public/markdown.js'), 'utf8'), sandbox);
const html = sandbox.window.MD.render('<img src=x onerror=alert(1)> **b** `<i>` [x](javascript:alert(1))\n```\n<script>\n```');
assert(!/<img|<script|href="javascript/i.test(html), html);
assert(html.includes('<b>b</b>') && html.includes('&lt;i&gt;'));
console.log('  ok  markdown escaping');

// ---- e2e ----
function req(method, url, data) {
	return new Promise((resolve, reject) => {
		const payload = data ? JSON.stringify(data) : '';
		const r = http.request(BASE + url, { method, headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(payload) } }, res => {
			let b = ''; res.on('data', d => { b += d; });
			res.on('end', () => { const j = JSON.parse(b || '{}'); res.statusCode < 400 ? resolve(j) : reject(Object.assign(new Error(j.error), { status: res.statusCode })); });
		});
		r.on('error', reject);
		r.end(payload);
	});
}
const sleep = ms => new Promise(r => setTimeout(r, ms));
async function waitFor(fn, ms = 8000) {
	const end = Date.now() + ms;
	for (;;) { const v = await fn(); if (v) return v; if (Date.now() > end) throw new Error('timeout'); await sleep(50); }
}
const msgs = async () => (await req('GET', '/api/state')).messages.general;

const server = spawn(process.execPath, [path.join(ROOT, 'server.js')], {
	env: { ...process.env, PORT: String(PORT), MOCK: '1', DATA_DIR: DATA, MOCK_DELAY_MS: '40' },
	stdio: ['ignore', 'pipe', 'inherit'],
});
const events = [];

(async () => {
	await waitFor(() => req('GET', '/api/state').then(() => true, () => false));
	http.get(BASE + '/api/events?name=Tester', res => {
		let buf = '';
		res.on('data', d => { buf += d; let i; while ((i = buf.indexOf('\n\n')) >= 0) { const chunk = buf.slice(0, i); buf = buf.slice(i + 2); const ev = /event: (.*)/.exec(chunk); if (ev) events.push(ev[1]); } });
	});

	await t('plain chat triggers no AI', async () => {
		await req('POST', '/api/messages', { channel: 'general', author: 'Tester', text: 'hello humans' });
		await sleep(300);
		assert.strictEqual((await msgs()).filter(m => m.authorType === 'agent').length, 0);
	});

	await t('@claude -> only Claude answers', async () => {
		const src = await req('POST', '/api/messages', { channel: 'general', author: 'Tester', text: '@Claude hi there' });
		const reply = await waitFor(async () => (await msgs()).find(m => m.replyTo === src.id && !m.pending));
		assert.strictEqual(reply.authorId, 'claude');
		assert.match(reply.text, /mock \*\*Claude\*\*.*hi there/);
		assert.strictEqual((await msgs()).filter(m => m.replyTo === src.id).length, 1);
	});

	await t('@codex -> only Codex answers', async () => {
		const src = await req('POST', '/api/messages', { channel: 'general', author: 'Tester', text: '@codex yo' });
		const reply = await waitFor(async () => (await msgs()).find(m => m.replyTo === src.id && !m.pending));
		assert.strictEqual(reply.authorId, 'codex');
		assert.strictEqual((await msgs()).filter(m => m.replyTo === src.id).length, 1);
	});

	await t('@both -> lead syncs with helper, one final message with sync log', async () => {
		const src = await req('POST', '/api/messages', { channel: 'general', author: 'Tester', text: '@both design a login page' });
		const reply = await waitFor(async () => (await msgs()).find(m => m.replyTo === src.id && !m.pending));
		assert.strictEqual(reply.authorId, 'claude');
		assert.strictEqual(reply.status, 'done');
		assert.strictEqual(reply.sync.length, 2);
		assert.deepStrictEqual(reply.sync.map(s => `${s.from}>${s.to}`), ['claude>codex', 'codex>claude']);
		assert.match(reply.text, /combined answer/);
		assert(!/\[\[to:/.test(reply.text));
		assert.strictEqual((await msgs()).filter(m => m.replyTo === src.id).length, 1);
	});

	await t('edit / react / reply / delete / permissions', async () => {
		const m = await req('POST', '/api/messages', { channel: 'general', author: 'Tester', text: 'typo' });
		const e = await req('PATCH', `/api/messages/${m.id}`, { author: 'Tester', text: 'fixed' });
		assert.strictEqual(e.text, 'fixed'); assert(e.editedAt);
		await assert.rejects(req('PATCH', `/api/messages/${m.id}`, { author: 'Mallory', text: 'x' }), { status: 403 });
		const r = await req('POST', `/api/messages/${m.id}/react`, { author: 'Tester', emoji: '👍' });
		assert.deepStrictEqual(r.reactions['👍'], ['Tester']);
		const r2 = await req('POST', `/api/messages/${m.id}/react`, { author: 'Tester', emoji: '👍' });
		assert(!r2.reactions['👍']);
		const rep = await req('POST', '/api/messages', { channel: 'general', author: 'Bob', text: 'answer', replyTo: m.id });
		assert.strictEqual(rep.replyTo, m.id);
		await assert.rejects(req('DELETE', `/api/messages/${m.id}`, { author: 'Bob' }), { status: 403 });
		await req('DELETE', `/api/messages/${m.id}`, { author: 'Tester' });
		assert(!(await msgs()).some(x => x.id === m.id));
		await assert.rejects(req('POST', '/api/messages', { channel: 'general', author: 'Claude', text: 'impersonate' }), { status: 400 });
	});

	await t('channels + persistence file', async () => {
		const ch = await req('POST', '/api/channels', { name: 'Side Project' });
		assert.strictEqual(ch.id, 'side-project');
		await req('POST', '/api/messages', { channel: 'side-project', author: 'Tester', text: 'in side' });
		await sleep(400);
		const disk = JSON.parse(fs.readFileSync(path.join(DATA, 'chat.json'), 'utf8'));
		assert(disk.messages['side-project'].some(m => m.text === 'in side'));
	});

	await t('SSE pushes live events', async () => {
		for (const e of ['presence', 'message:new', 'message:update', 'agents:status']) assert(events.includes(e), `missing ${e}`);
	});

	console.log(`\n  ${passed + 2} checks passed`);
})().catch(e => { console.error('\n  FAIL', e); process.exitCode = 1; }).finally(() => {
	server.kill();
	fs.rmSync(DATA, { recursive: true, force: true });
});
