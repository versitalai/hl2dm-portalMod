#!/usr/bin/env node
'use strict';
// Stand-in for the claude/codex CLIs: reads the prompt on stdin, prints a reply.
// Used by `start --mock` (try the UI without either CLI) and by test/smoke.js.
const id = process.argv[2] || 'agent';
const name = id[0].toUpperCase() + id.slice(1);
let prompt = '';
process.stdin.on('data', d => { prompt += d; });
process.stdin.on('end', () => {
	const delay = Number(process.env.MOCK_DELAY_MS || 700);
	setTimeout(() => {
		const msg = (prompt.split(/## (?:Original )?[Mm]essage from [^\n]*\n/)[1] || '').split('\n## ')[0].trim();
		if (/You are the lead/.test(prompt)) {
			if (/## AI sync log so far\n\(nothing yet\)/.test(prompt)) {
				process.stdout.write(`I'll loop in the other AI.\n[[to:${id === 'claude' ? 'codex' : 'claude'}]]\nCan you sanity-check this request: "${msg}"?\n[[/to]]`);
			} else {
				process.stdout.write(`**${name} (lead):** after syncing, here's our combined answer to "${msg}". The other AI agreed. ✅`);
			}
			return;
		}
		if (/is leading and has sent you a message/.test(prompt)) {
			process.stdout.write(`_${name} here:_ looks fine to me, one suggestion: add a test.`);
			return;
		}
		process.stdout.write(`Hi! This is mock **${name}**. You said: "${msg}"`);
	}, delay);
});
