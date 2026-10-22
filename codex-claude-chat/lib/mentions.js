'use strict';
// Mention parsing + routing rules.
//   @claude          -> only Claude answers
//   @codex           -> only Codex answers
//   @both / @ai / @claude + @codex in one message -> collaboration (lead answers, consults the other)
//   no AI mention    -> no AI answers (plain human chat)
//   @everyone / @here pings humans only; @<name> pings one human
const BOTH_ALIASES = new Set(['both', 'ai', 'ais', 'agents', 'all']);
const EVERYONE_ALIASES = new Set(['everyone', 'here']);

function parseMentions(text) {
	const out = new Set();
	// Skip mentions inside code (`@x` or ``` blocks) so pasted code doesn't ping anyone.
	const stripped = String(text || '').replace(/```[\s\S]*?```/g, ' ').replace(/`[^`\n]*`/g, ' ');
	const re = /(^|[^\w@./])@([A-Za-z][\w-]{0,31})/g;
	let m;
	while ((m = re.exec(stripped))) out.add(m[2].toLowerCase());
	return [...out];
}

// Returns { mode: 'none' | 'single' | 'collab', agents: [ids] }
function route(mentions, agentIds) {
	const hit = agentIds.filter(id => mentions.includes(id));
	const both = mentions.some(m => BOTH_ALIASES.has(m));
	if (both || hit.length >= 2) return { mode: 'collab', agents: agentIds };
	if (hit.length === 1) return { mode: 'single', agents: hit };
	return { mode: 'none', agents: [] };
}

module.exports = { parseMentions, route, BOTH_ALIASES, EVERYONE_ALIASES };
