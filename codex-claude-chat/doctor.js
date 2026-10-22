#!/usr/bin/env node
'use strict';
// Setup + diagnostics. `node doctor.js` checks everything; `--first-run` (used by the
// launchers) only runs once and offers to fix what's missing.
const fs = require('fs');
const path = require('path');
const os = require('os');
const readline = require('readline');
const { spawnSync } = require('child_process');
const { loadConfig } = require('./lib/config');
const { probe } = require('./lib/agents');

const IS_WIN = process.platform === 'win32';
const MARKER = path.join(__dirname, 'data', '.setup-done');
const USER_CFG = path.join(__dirname, 'config.json');
const PACKAGES = { claude: '@anthropic-ai/claude-code', codex: '@openai/codex' };
const LOGIN = { claude: 'claude   (then type /login, or follow the prompt)', codex: 'codex login' };

const firstRun = process.argv.includes('--first-run');
if (firstRun && fs.existsSync(MARKER)) process.exit(0);

const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
const ask = q => new Promise(r => rl.question(q, a => r(a.trim())));
const ok = s => console.log(`  [ok]   ${s}`);
const bad = s => console.log(`  [!!]   ${s}`);

function sh(cmd, args) {
	return spawnSync(cmd, args, { shell: IS_WIN, stdio: 'inherit', windowsHide: true });
}

(async () => {
	console.log('\nCodex-Claude Chat — setup check\n');
	const major = Number(process.versions.node.split('.')[0]);
	major >= 18 ? ok(`Node.js ${process.versions.node}`) : bad(`Node.js ${process.versions.node} is too old: install Node 18+ from https://nodejs.org`);

	const cfg = loadConfig({ noMock: true }); // always check the real CLIs
	let missing = [];
	for (const a of Object.values(cfg.agents)) {
		const r = probe({ ...a, mock: false }, { mock: false });
		if (r.ok) ok(`${a.name} CLI: ${r.version}`);
		else { bad(`${a.name} CLI not found ("${a.command}")`); missing.push(a.id); }
	}

	if (missing.length) {
		const pk = missing.filter(id => PACKAGES[id]).map(id => PACKAGES[id]);
		console.log(`\n  Missing CLIs can be installed with:\n    npm install -g ${pk.join(' ')}\n`);
		if (pk.length && /^y/i.test(await ask('  Install them now? [y/N] '))) {
			sh('npm', ['install', '-g', ...pk]);
			missing = missing.filter(id => !probe({ ...cfg.agents[id], mock: false }, { mock: false }).ok);
			if (!missing.length) ok('Installed.');
			else bad(`Still missing: ${missing.join(', ')}. Open a NEW terminal (PATH refresh) and run doctor again.`);
		}
	}
	console.log('\n  Each CLI must be signed in once (opens a browser):');
	for (const id of Object.keys(cfg.agents)) if (LOGIN[id]) console.log(`    ${LOGIN[id]}`);

	// Workdir: the folder the AIs read and edit.
	const user = JSON.parse(fs.readFileSync(USER_CFG, 'utf8'));
	console.log(`\n  AI working folder: ${user.workdir ? user.workdir : '(not set: the folder you start the chat from)'}`);
	if (firstRun || /^y/i.test(await ask('  Change it? [y/N] '))) {
		const def = path.join(os.homedir(), IS_WIN ? 'Documents' : '', 'ai-workspace');
		const dir = (await ask(`  Folder for the AIs to work in [${def}]: `)) || def;
		fs.mkdirSync(dir, { recursive: true });
		user.workdir = path.resolve(dir);
		fs.writeFileSync(USER_CFG, JSON.stringify(user, null, 2) + '\n');
		ok(`workdir = ${user.workdir}`);
	}

	if (IS_WIN && firstRun && /^y/i.test(await ask('\n  Create a Desktop shortcut? [Y/n] ') || 'y')) {
		const target = path.join(__dirname, 'start.bat');
		const ps = `$s=(New-Object -ComObject WScript.Shell).CreateShortcut([Environment]::GetFolderPath('Desktop')+'\\Codex-Claude Chat.lnk');`
			+ `$s.TargetPath='${target.replace(/'/g, "''")}';$s.WorkingDirectory='${__dirname.replace(/'/g, "''")}';$s.WindowStyle=7;$s.Save()`;
		const r = spawnSync('powershell', ['-NoProfile', '-Command', ps], { stdio: 'inherit' });
		r.status === 0 ? ok('Shortcut "Codex-Claude Chat" added to Desktop.') : bad('Could not create shortcut.');
	}

	fs.mkdirSync(path.dirname(MARKER), { recursive: true });
	fs.writeFileSync(MARKER, new Date().toISOString());
	console.log('\n  Done. Start the chat with start.bat (Windows) or ./start.sh (Linux).');
	console.log('  Tip: no CLIs yet? Try the UI with mock AIs: start.bat --mock\n');
	rl.close();
})();
