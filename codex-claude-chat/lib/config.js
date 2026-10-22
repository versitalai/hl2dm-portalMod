'use strict';
// Loads config.default.json, then deep-merges config.json (user overrides) on top.
// config.json holds ONLY your overrides, so updated defaults (e.g. new CLI flags) still
// reach you after an update instead of being frozen in an old copy.
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const DEFAULT_PATH = path.join(ROOT, 'config.default.json');
const USER_PATH = path.join(ROOT, 'config.json');

function isObj(v) { return v && typeof v === 'object' && !Array.isArray(v); }

function merge(base, over) {
	const out = { ...base };
	for (const [k, v] of Object.entries(over || {})) {
		out[k] = isObj(v) && isObj(base[k]) ? merge(base[k], v) : v;
	}
	return out;
}

function loadConfig(opts = {}) {
	const defaults = JSON.parse(fs.readFileSync(DEFAULT_PATH, 'utf8'));
	if (!fs.existsSync(USER_PATH)) {
		fs.writeFileSync(USER_PATH, JSON.stringify({
			_comment: 'Your overrides. Copy any key from config.default.json here to change it.',
			workdir: '',
		}, null, 2) + '\n');
	}
	let user = {};
	try {
		user = JSON.parse(fs.readFileSync(USER_PATH, 'utf8'));
	} catch (e) {
		console.error(`[config] config.json is not valid JSON (${e.message}); using defaults.`);
	}
	const cfg = merge(defaults, user);

	// Environment overrides: handy for one-off runs without editing files.
	if (process.env.PORT) cfg.port = Number(process.env.PORT);
	if (process.env.HOST) cfg.host = process.env.HOST;
	if (process.env.MOCK === '1' || process.argv.includes('--mock')) cfg.mock = true;
	if (process.env.WORKDIR) cfg.workdir = process.env.WORKDIR;

	cfg.workdir = path.resolve(cfg.workdir || process.cwd());
	cfg.root = ROOT;
	cfg.dataDir = process.env.DATA_DIR ? path.resolve(process.env.DATA_DIR) : path.join(ROOT, 'data');

	// Mock agents: a tiny local script stands in for a CLI (UI demo + tests).
	// Global "mock": true mocks every agent; agents.<id>.mock mocks just that one.
	const mock = path.join(ROOT, 'mock', 'mock-agent.js');
	const only = (process.env.MOCK_AGENTS || '').split(',').filter(Boolean);
	for (const [id, a] of Object.entries(cfg.agents)) {
		if (!opts.noMock && (cfg.mock || a.mock || only.includes(id))) {
			a.mock = true;
			a.command = process.execPath;
			a.args = [mock, id];
		}
	}
	for (const [id, a] of Object.entries(cfg.agents)) a.id = id;
	return cfg;
}

module.exports = { loadConfig };
