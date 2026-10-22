'use strict';
/* global MD */
(function () {
	const $ = s => document.querySelector(s);
	const EMOJI = ['👍', '❤️', '😂', '🎉', '👀', '🤔', '✅', '❌'];
	const TOKEN = (() => {
		const t = new URLSearchParams(location.search).get('token');
		try { if (t) localStorage.setItem('ccchat.token', t); return t || localStorage.getItem('ccchat.token') || ''; } catch { return t || ''; }
	})();

	const pref = {
		get(k, d) { try { const v = localStorage.getItem('ccchat.' + k); return v === null ? d : JSON.parse(v); } catch { return d; } },
		set(k, v) { try { localStorage.setItem('ccchat.' + k, JSON.stringify(v)); } catch { /* private mode */ } },
	};

	const S = {
		me: pref.get('name', ''),
		channels: [], messages: {}, agents: [], online: [],
		current: pref.get('channel', 'general'),
		unread: {}, replyTo: null, editing: null,
		typing: new Map(), syncOpen: new Set(), search: '',
	};

	// ---------- api ----------
	async function api(method, url, data) {
		const r = await fetch(url, {
			method, headers: { 'Content-Type': 'application/json', 'x-token': TOKEN },
			body: data === undefined ? undefined : JSON.stringify(data),
		});
		const j = await r.json().catch(() => ({}));
		if (!r.ok) throw new Error(j.error || r.statusText);
		return j;
	}
	function toast(t, ms = 2600) {
		const el = $('#toast'); el.textContent = t; el.hidden = false;
		clearTimeout(toast.t); toast.t = setTimeout(() => { el.hidden = true; }, ms);
	}

	// ---------- helpers ----------
	const agentById = id => S.agents.find(a => a.id === id);
	const agentByName = n => S.agents.find(a => a.name.toLowerCase() === String(n).toLowerCase());
	const BOTH = ['both', 'ai', 'ais', 'agents', 'all'];
	function mentionClass(name) {
		const n = name.toLowerCase();
		if (BOTH.includes(n)) return 'both';
		const a = agentById(n);
		return a ? a.id : '';
	}
	function colorFor(name) {
		const a = agentByName(name);
		if (a) return a.color;
		let h = 0; for (const c of name) h = (h * 31 + c.charCodeAt(0)) % 360;
		return `hsl(${h} 55% 48%)`;
	}
	function initials(n) { return n.split(/\s+/).map(x => x[0]).join('').slice(0, 2).toUpperCase(); }
	function fmtTime(ts) { return new Date(ts).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }); }
	function fmtDay(ts) {
		const d = new Date(ts); const t = new Date();
		const y = new Date(); y.setDate(t.getDate() - 1);
		if (d.toDateString() === t.toDateString()) return 'Today';
		if (d.toDateString() === y.toDateString()) return 'Yesterday';
		return d.toLocaleDateString([], { weekday: 'long', month: 'short', day: 'numeric', year: 'numeric' });
	}
	const list = ch => S.messages[ch] || (S.messages[ch] = []);
	function mentionsMe(m) {
		const me = S.me.toLowerCase();
		return (m.mentions || []).some(x => x === me || x === 'everyone' || x === 'here');
	}
	function md(text) { return MD.render(text, mentionClass); }

	// ---------- rendering ----------
	function renderSidebar() {
		const ch = $('#channels'); ch.innerHTML = '';
		for (const c of S.channels) {
			const li = document.createElement('li');
			li.className = c.id === S.current ? 'active' : '';
			li.innerHTML = `<span># ${MD.esc(c.name)}</span>`;
			if (S.unread[c.id]) li.innerHTML += `<span class="badge">${S.unread[c.id]}</span>`;
			else if (c.id !== 'general') li.innerHTML += `<button class="icon del" title="Delete channel">✕</button>`;
			li.onclick = e => {
				if (e.target.classList.contains('del')) {
					if (confirm(`Delete #${c.name} and all its messages?`)) api('DELETE', `/api/channels/${c.id}`).catch(err => toast(err.message));
					return;
				}
				switchChannel(c.id);
			};
			ch.appendChild(li);
		}
		const ag = $('#agents'); ag.innerHTML = '';
		for (const a of S.agents) {
			const li = document.createElement('li');
			const cls = !a.installed ? 'off' : a.status === 'busy' ? 'busy' : '';
			const sub = !a.installed ? 'not installed' : a.status === 'busy' ? 'working…' : 'ready';
			li.title = a.installed ? (a.version || '') : (a.error || 'CLI not found on PATH');
			li.innerHTML = `<span class="dot ${cls}"></span><span style="color:${a.color};font-weight:600">${MD.esc(a.name)}</span><span class="agent-sub">${sub}</span>`;
			ag.appendChild(li);
		}
		const pp = $('#people'); pp.innerHTML = '';
		for (const n of S.online) {
			const li = document.createElement('li');
			li.innerHTML = `<span class="dot"></span>${MD.esc(n)}${n === S.me ? ' <span class="agent-sub">you</span>' : ''}`;
			pp.appendChild(li);
		}
		const total = Object.values(S.unread).reduce((a, b) => a + b, 0);
		document.title = (total ? `(${total}) ` : '') + 'Codex-Claude Chat';
		const missing = S.agents.filter(a => !a.installed);
		const banner = $('#banner');
		banner.hidden = !missing.length;
		if (missing.length) banner.innerHTML = `⚠ ${missing.map(a => `<b>${MD.esc(a.name)}</b>`).join(' and ')} CLI not found. Install it, then click ↻ in the sidebar. See README → Setup.`;
	}

	function syncHtml(m) {
		const n = (m.sync || []).length;
		if (!n && !(m.collab && m.pending)) return '';
		const open = S.syncOpen.has(m.id) || (pref.get('syncOpen', false) && !S.syncOpen.has('!' + m.id));
		const names = [...new Set([m.author, ...(m.sync || []).map(s => s.toName)])].join(' ↔ ');
		const live = m.pending ? ' <span class="live">· syncing<span class="dots"></span></span>' : '';
		const items = (m.sync || []).map(s => `
			<div class="sync-item">
				<div class="sync-head"><i><b style="color:${colorFor(s.fromName)}">${MD.esc(s.fromName)}</b> → <b style="color:${colorFor(s.toName)}">${MD.esc(s.toName)}</b> · ${fmtTime(s.ts)}</i></div>
				<div class="sync-body"><i>${md(s.text)}</i></div>
			</div>`).join('');
		return `<details class="sync" data-id="${m.id}"${open ? ' open' : ''}><summary><i>⇄ AI sync: ${MD.esc(names)} · ${n} message${n === 1 ? '' : 's'}</i>${live}</summary>${items}</details>`;
	}

	function messageEl(m, prev) {
		const el = document.createElement('div');
		el.className = 'msg';
		el.dataset.id = m.id;
		const cont = prev && prev.author === m.author && m.ts - prev.ts < 5 * 60e3 && !m.replyTo && !prev.pending
			&& new Date(prev.ts).toDateString() === new Date(m.ts).toDateString();
		if (cont) el.classList.add('cont');
		if (mentionsMe(m) && m.author !== S.me) el.classList.add('mention');
		const agent = m.authorType === 'agent';
		let html = '';
		if (m.replyTo) {
			const ref = list(m.channel).find(x => x.id === m.replyTo);
			html += `<div class="reply-ref" data-jump="${m.replyTo}">↪ ${ref ? `<b>${MD.esc(ref.author)}</b> ${MD.esc(ref.text.replace(/\s+/g, ' ').slice(0, 90))}` : '<i>original message deleted</i>'}</div>`;
		}
		html += `<div class="avatar" style="background:${colorFor(m.author)}">${agent ? '✦' : MD.esc(initials(m.author))}</div>`;
		html += `<div class="meta"><span class="name" style="color:${colorFor(m.author)}">${MD.esc(m.author)}</span>${agent ? '<span class="tag">AI</span>' : ''}<span class="time" title="${new Date(m.ts).toLocaleString()}">${fmtTime(m.ts)}</span></div>`;
		html += '<div class="body">';
		html += syncHtml(m);
		if (m.pending) {
			const what = m.collab ? `${MD.esc(m.author)} is leading and syncing with the other AI` : `${MD.esc(m.author)} is thinking`;
			html += `<div class="thinking">${what}<span class="dots"></span> <button class="btn-sm" data-act="cancel">Stop</button></div>`;
		} else {
			html += md(m.text);
			if (m.editedAt) html += ' <span class="edited">(edited)</span>';
		}
		if (m.status === 'error') {
			html += `<div class="error-box"><span>⚠ ${MD.esc(m.error || 'Failed')}</span><button class="btn-sm" data-act="retry">Retry</button></div>`;
		}
		const rx = Object.entries(m.reactions || {});
		if (rx.length) {
			html += '<div class="reactions">' + rx.map(([e, who]) =>
				`<button class="reaction${who.includes(S.me) ? ' mine' : ''}" data-emoji="${MD.esc(e)}" title="${MD.esc(who.join(', '))}">${e} ${who.length}</button>`).join('') + '</div>';
		}
		html += '</div>';
		const mine = m.authorType === 'human' && m.author === S.me;
		html += `<div class="actions">
			<button data-act="react" title="React">😀</button>
			<button data-act="reply" title="Reply">↩</button>
			<button data-act="copy" title="Copy text">⧉</button>
			${mine ? '<button data-act="edit" title="Edit">✎</button>' : ''}
			${mine || agent ? '<button data-act="delete" title="Delete">🗑</button>' : ''}
		</div>`;
		el.innerHTML = html;
		if (S.search && !(m.text + ' ' + m.author).toLowerCase().includes(S.search)) el.classList.add('hidden-by-search');
		return el;
	}

	function renderMessages() {
		const box = $('#messages');
		const msgs = list(S.current);
		box.innerHTML = '';
		if (!msgs.length) {
			box.innerHTML = `<div class="empty"><h3>Welcome to #${MD.esc(S.current)}</h3>
				<p>Ping <span class="at claude">@Claude</span> or <span class="at codex">@Codex</span> to get one AI.<br>
				Ping <span class="at both">@Both</span> and the lead AI answers after syncing with the other.<br>
				Messages without an AI ping are just chat.</p></div>`;
			return;
		}
		let prev = null; let lastDay = '';
		const frag = document.createDocumentFragment();
		for (const m of msgs) {
			const day = new Date(m.ts).toDateString();
			if (day !== lastDay) {
				const d = document.createElement('div'); d.className = 'day'; d.textContent = fmtDay(m.ts);
				frag.appendChild(d); lastDay = day; prev = null;
			}
			frag.appendChild(messageEl(m, prev));
			prev = m;
		}
		box.appendChild(frag);
		box.scrollTop = box.scrollHeight;
	}

	function nearBottom() { const b = $('#messages'); return b.scrollHeight - b.scrollTop - b.clientHeight < 120; }

	function upsertMessage(m) {
		const arr = list(m.channel);
		const i = arr.findIndex(x => x.id === m.id);
		if (i >= 0) arr[i] = m; else arr.push(m);
		if (m.channel !== S.current) return;
		const box = $('#messages');
		const stick = nearBottom();
		const old = box.querySelector(`.msg[data-id="${m.id}"]`);
		if (old) {
			const idx = arr.findIndex(x => x.id === m.id);
			old.replaceWith(messageEl(m, arr[idx - 1]));
		} else renderMessages();
		if (stick) box.scrollTop = box.scrollHeight;
	}

	function renderTyping() {
		const now = Date.now();
		const names = [];
		for (const [k, exp] of S.typing) {
			const [ch, n] = k.split('|');
			if (exp < now) S.typing.delete(k); else if (ch === S.current && n !== S.me) names.push(n);
		}
		const busy = S.agents.filter(a => a.status === 'busy').map(a => a.name);
		const parts = [];
		if (names.length) parts.push(`${names.join(', ')} ${names.length > 1 ? 'are' : 'is'} typing…`);
		if (busy.length) parts.push(`${busy.join(' & ')} working…`);
		$('#typing').textContent = parts.join('  ·  ');
	}
	setInterval(renderTyping, 1500);

	// ---------- notifications ----------
	let audio;
	function beep() {
		if (!pref.get('sound', true)) return;
		try {
			audio = audio || new (window.AudioContext || window.webkitAudioContext)();
			const o = audio.createOscillator(); const g = audio.createGain();
			o.frequency.value = 880; o.type = 'sine';
			g.gain.setValueAtTime(0.0001, audio.currentTime);
			g.gain.exponentialRampToValueAtTime(0.15, audio.currentTime + 0.02);
			g.gain.exponentialRampToValueAtTime(0.0001, audio.currentTime + 0.25);
			o.connect(g).connect(audio.destination); o.start(); o.stop(audio.currentTime + 0.3);
		} catch { /* no audio */ }
	}
	function notify(m, why) {
		beep();
		if (pref.get('notify', false) && 'Notification' in window && Notification.permission === 'granted' && document.hidden) {
			const n = new Notification(`${m.author} ${why}`, { body: m.text.slice(0, 140), icon: 'icon.svg', tag: m.id });
			n.onclick = () => { window.focus(); switchChannel(m.channel); jump(m.id); };
		}
	}
	function considerNotify(m, wasPending) {
		if (m.author === S.me || m.pending) return;
		const mine = m.replyTo && list(m.channel).some(x => x.id === m.replyTo && x.author === S.me);
		const finishedForMe = wasPending && mine && m.authorType === 'agent';
		if (mentionsMe(m)) notify(m, 'mentioned you');
		else if (finishedForMe) notify(m, 'replied to you');
		else return;
		if (m.channel !== S.current || document.hidden) bumpUnread(m.channel);
	}
	function bumpUnread(ch) {
		if (ch === S.current && !document.hidden) return;
		S.unread[ch] = (S.unread[ch] || 0) + 1;
		renderSidebar();
	}
	document.addEventListener('visibilitychange', () => {
		if (!document.hidden && S.unread[S.current]) { delete S.unread[S.current]; renderSidebar(); }
	});

	// ---------- events (SSE) ----------
	function connect() {
		const es = new EventSource(`/api/events?name=${encodeURIComponent(S.me)}&token=${encodeURIComponent(TOKEN)}`);
		es.addEventListener('open', () => { if (connect.lost) { connect.lost = false; load(); } });
		es.addEventListener('error', () => { connect.lost = true; });
		es.addEventListener('message:new', e => {
			const m = JSON.parse(e.data);
			upsertMessage(m);
			if (m.authorType === 'human' && m.author !== S.me) {
				if (mentionsMe(m)) considerNotify(m, false);
				else if (m.channel !== S.current || document.hidden) bumpUnread(m.channel);
			}
		});
		es.addEventListener('message:update', e => {
			const m = JSON.parse(e.data);
			const old = list(m.channel).find(x => x.id === m.id);
			const wasPending = old && old.pending;
			upsertMessage(m);
			if (wasPending && !m.pending) considerNotify(m, true);
		});
		es.addEventListener('message:delete', e => {
			const { id, channel } = JSON.parse(e.data);
			S.messages[channel] = list(channel).filter(x => x.id !== id);
			if (channel === S.current) renderMessages();
		});
		es.addEventListener('channel:cleared', e => {
			const { channel } = JSON.parse(e.data);
			S.messages[channel] = [];
			if (channel === S.current) renderMessages();
		});
		es.addEventListener('channels', e => {
			S.channels = JSON.parse(e.data);
			if (!S.channels.some(c => c.id === S.current)) switchChannel('general');
			renderSidebar();
		});
		es.addEventListener('workdir', e => { S.workdir = JSON.parse(e.data); showWorkdir(); });
		es.addEventListener('presence', e => { S.online = JSON.parse(e.data); renderSidebar(); });
		es.addEventListener('agents', e => { S.agents = JSON.parse(e.data); renderSidebar(); });
		es.addEventListener('agents:status', e => {
			const st = JSON.parse(e.data);
			for (const a of S.agents) if (st[a.id]) a.status = st[a.id];
			renderSidebar(); renderTyping();
		});
		es.addEventListener('typing', e => {
			const t = JSON.parse(e.data);
			const k = `${t.channel}|${t.name}`;
			if (t.on) S.typing.set(k, Date.now() + 4000); else S.typing.delete(k);
			renderTyping();
		});
	}

	async function load() {
		const st = await api('GET', '/api/state');
		S.channels = st.channels; S.messages = st.messages; S.agents = st.agents; S.online = st.online;
		if (!S.channels.some(c => c.id === S.current)) S.current = 'general';
		S.workdir = st.workdir; S.mock = st.mock; showWorkdir();
		renderSidebar(); switchChannel(S.current);
	}

	function showWorkdir() { $('#workdir').textContent = `AI workdir: ${S.workdir}${S.mock ? ' (mock agents)' : ''}`; }
	$('#workdirBtn').onclick = async () => {
		const d = prompt('Folder the AIs should work in (full path):', S.workdir); if (!d) return;
		try { await api('POST', '/api/workdir', { workdir: d }); toast('AI workdir updated'); } catch (e) { toast(e.message); }
	};

	function switchChannel(id) {
		S.current = id; pref.set('channel', id);
		delete S.unread[id];
		$('#chanTitle').textContent = '# ' + id;
		$('#input').placeholder = `Message #${id}`;
		$('#sidebar').classList.remove('open');
		setContext(null);
		renderSidebar(); renderMessages();
	}

	function jump(id) {
		const el = document.querySelector(`.msg[data-id="${id}"]`);
		if (!el) return toast('Message not loaded');
		el.scrollIntoView({ block: 'center', behavior: 'smooth' });
		el.classList.remove('flash'); void el.offsetWidth; el.classList.add('flash');
	}

	// ---------- composer ----------
	const input = $('#input');
	function autosize() { input.style.height = 'auto'; input.style.height = Math.min(input.scrollHeight, innerHeight * 0.4) + 'px'; }

	function setContext(ctx) {
		S.replyTo = ctx && ctx.type === 'reply' ? ctx.msg.id : null;
		S.editing = ctx && ctx.type === 'edit' ? ctx.msg.id : null;
		const box = $('#context');
		box.hidden = !ctx;
		if (!ctx) return;
		const snippet = MD.esc(ctx.msg.text.replace(/\s+/g, ' ').slice(0, 80));
		$('#contextText').innerHTML = ctx.type === 'reply' ? `Replying to <b>${MD.esc(ctx.msg.author)}</b>: ${snippet}` : 'Editing message (Esc to cancel)';
		if (ctx.type === 'edit') { input.value = ctx.msg.text; autosize(); }
		input.focus();
	}
	$('#contextClose').onclick = () => { if (S.editing) input.value = ''; setContext(null); };

	const HELP = [
		'**Pings:** `@Claude` → only Claude answers · `@Codex` → only Codex answers · `@Both` (or `@AI`, `@All`, or both names) → lead AI answers after syncing with the other.',
		'`@name` pings a person · `@everyone` / `@here` pings all people.',
		'**Commands:** `/clear` clear channel · `/nick NewName` rename · `/channel name` new channel · `/help` this help.',
		'**Keys:** Enter send · Shift+Enter newline · ↑ in empty box edits your last message · Esc cancels reply/edit.',
	].join('\n\n');

	async function submit() {
		const text = input.value;
		if (!text.trim()) return;
		const cmd = text.trim().match(/^\/(\w+)\s*(.*)$/s);
		if (cmd && !S.editing) {
			const [, c, arg] = cmd;
			if (c === 'help') { showLocal(HELP); input.value = ''; return; }
			if (c === 'clear') { if (confirm(`Clear all messages in #${S.current}?`)) await api('POST', `/api/channels/${S.current}/clear`); input.value = ''; return; }
			if (c === 'nick') { if (arg.trim()) setName(arg.trim()); input.value = ''; return; }
			if (c === 'channel') { try { const ch = await api('POST', '/api/channels', { name: arg }); switchChannel(ch.id); } catch (e) { toast(e.message); } input.value = ''; return; }
		}
		try {
			if (S.editing) await api('PATCH', `/api/messages/${S.editing}`, { author: S.me, text });
			else await api('POST', '/api/messages', { channel: S.current, author: S.me, text, replyTo: S.replyTo });
			input.value = ''; autosize(); setContext(null);
			sendTyping(false);
		} catch (e) { toast(e.message); }
	}

	function showLocal(text) {
		const box = $('#messages');
		const el = document.createElement('div');
		el.className = 'msg';
		el.innerHTML = `<div class="avatar" style="background:#666">?</div><div class="meta"><span class="name">Help</span><span class="tag">only you</span></div><div class="body">${md(text)}</div>`;
		box.appendChild(el); box.scrollTop = box.scrollHeight;
	}

	let typingSent = 0;
	function sendTyping(on) {
		const now = Date.now();
		if (on && now - typingSent < 2500) return;
		typingSent = on ? now : 0;
		api('POST', '/api/typing', { name: S.me, channel: S.current, on }).catch(() => {});
	}

	// ---- @mention autocomplete ----
	let menuItems = []; let menuSel = 0;
	function mentionCandidates() {
		const people = new Set(S.online);
		for (const m of list(S.current)) if (m.authorType === 'human') people.add(m.author);
		people.delete(S.me);
		return [
			...S.agents.map(a => ({ name: a.name, sub: 'only ' + a.name + ' answers', color: a.color })),
			{ name: 'Both', sub: 'AIs collaborate', color: 'var(--both)' },
			...[...people].map(n => ({ name: n.replace(/\s+/g, ''), sub: S.online.includes(n) ? 'online' : 'person', color: colorFor(n) })),
			{ name: 'everyone', sub: 'all people' },
		];
	}
	function updateMenu() {
		const upto = input.value.slice(0, input.selectionStart);
		const m = upto.match(/(?:^|\s)@([\w-]*)$/);
		const menu = $('#mentionMenu');
		if (!m) { menu.hidden = true; menuItems = []; return; }
		const q = m[1].toLowerCase();
		menuItems = mentionCandidates().filter(c => c.name.toLowerCase().startsWith(q)).slice(0, 8);
		if (!menuItems.length) { menu.hidden = true; return; }
		menuSel = Math.min(menuSel, menuItems.length - 1);
		menu.innerHTML = menuItems.map((c, i) => `<li data-i="${i}" class="${i === menuSel ? 'sel' : ''}"><b style="color:${c.color || 'inherit'}">@${MD.esc(c.name)}</b><small>${MD.esc(c.sub)}</small></li>`).join('');
		menu.hidden = false;
	}
	function pickMention(i) {
		const c = menuItems[i]; if (!c) return;
		const pos = input.selectionStart;
		const before = input.value.slice(0, pos).replace(/@([\w-]*)$/, '@' + c.name + ' ');
		input.value = before + input.value.slice(pos);
		input.selectionStart = input.selectionEnd = before.length;
		$('#mentionMenu').hidden = true; menuItems = [];
		input.focus();
	}
	$('#mentionMenu').addEventListener('mousedown', e => {
		const li = e.target.closest('li'); if (li) { e.preventDefault(); pickMention(+li.dataset.i); }
	});

	input.addEventListener('input', () => { autosize(); updateMenu(); if (input.value.trim()) sendTyping(true); });
	input.addEventListener('click', updateMenu);
	input.addEventListener('keydown', e => {
		if (menuItems.length && !$('#mentionMenu').hidden) {
			if (e.key === 'ArrowDown') { menuSel = (menuSel + 1) % menuItems.length; updateMenu(); return e.preventDefault(); }
			if (e.key === 'ArrowUp') { menuSel = (menuSel - 1 + menuItems.length) % menuItems.length; updateMenu(); return e.preventDefault(); }
			if (e.key === 'Enter' || e.key === 'Tab') { pickMention(menuSel); return e.preventDefault(); }
			if (e.key === 'Escape') { $('#mentionMenu').hidden = true; menuItems = []; return e.preventDefault(); }
		}
		if (e.key === 'Enter' && !e.shiftKey && !e.isComposing) { e.preventDefault(); submit(); }
		else if (e.key === 'Escape') { if (S.editing) input.value = ''; setContext(null); autosize(); }
		else if (e.key === 'ArrowUp' && !input.value) {
			const mine = list(S.current).filter(m => m.author === S.me && m.authorType === 'human').pop();
			if (mine) { e.preventDefault(); setContext({ type: 'edit', msg: mine }); }
		}
	});
	input.addEventListener('blur', () => setTimeout(() => { $('#mentionMenu').hidden = true; }, 150));
	$('#composer').addEventListener('submit', e => { e.preventDefault(); submit(); });
	document.querySelectorAll('[data-ping]').forEach(b => b.onclick = () => {
		const tag = '@' + b.dataset.ping + ' ';
		const v = input.value.replace(/^@(Claude|Codex|Both)\s+/i, '');
		input.value = tag + v; input.focus(); input.selectionStart = input.selectionEnd = input.value.length; autosize();
	});

	// ---------- message actions ----------
	$('#messages').addEventListener('click', async e => {
		const copyBtn = e.target.closest('pre .copy');
		if (copyBtn) { copy(copyBtn.parentElement.querySelector('code').textContent); return; }
		const jumpEl = e.target.closest('[data-jump]');
		if (jumpEl) { jump(jumpEl.dataset.jump); return; }
		const msgEl = e.target.closest('.msg'); if (!msgEl) return;
		const m = list(S.current).find(x => x.id === msgEl.dataset.id); if (!m) return;
		const rx = e.target.closest('.reaction');
		if (rx) { api('POST', `/api/messages/${m.id}/react`, { author: S.me, emoji: rx.dataset.emoji }).catch(err => toast(err.message)); return; }
		const act = e.target.closest('[data-act]'); if (!act) return;
		switch (act.dataset.act) {
			case 'reply': setContext({ type: 'reply', msg: m }); break;
			case 'edit': setContext({ type: 'edit', msg: m }); break;
			case 'copy': copy(m.text); break;
			case 'delete':
				if (confirm('Delete this message?')) api('DELETE', `/api/messages/${m.id}`, { author: S.me }).catch(err => toast(err.message));
				break;
			case 'cancel': api('POST', `/api/messages/${m.id}/cancel`, {}).catch(err => toast(err.message)); break;
			case 'retry': api('POST', `/api/messages/${m.id}/retry`, {}).catch(err => toast(err.message)); break;
			case 'react': openPicker(act, m); break;
		}
	});
	$('#messages').addEventListener('toggle', e => {
		const d = e.target; if (!d.classList || !d.classList.contains('sync')) return;
		const id = d.dataset.id;
		if (d.open) { S.syncOpen.add(id); S.syncOpen.delete('!' + id); } else { S.syncOpen.delete(id); S.syncOpen.add('!' + id); }
	}, true);

	function copy(t) {
		(navigator.clipboard ? navigator.clipboard.writeText(t) : Promise.reject()).then(() => toast('Copied'), () => {
			const ta = document.createElement('textarea'); ta.value = t; document.body.appendChild(ta); ta.select();
			try { document.execCommand('copy'); toast('Copied'); } catch { toast('Copy failed'); }
			ta.remove();
		});
	}

	function openPicker(btn, m) {
		const p = $('#emojiPicker');
		p.innerHTML = EMOJI.map(x => `<button>${x}</button>`).join('');
		const r = btn.getBoundingClientRect();
		p.hidden = false;
		p.style.top = Math.max(8, r.top - 46) + 'px';
		p.style.left = Math.max(8, Math.min(innerWidth - p.offsetWidth - 8, r.left - 120)) + 'px';
		p.onclick = e => {
			const b = e.target.closest('button'); if (!b) return;
			api('POST', `/api/messages/${m.id}/react`, { author: S.me, emoji: b.textContent }).catch(err => toast(err.message));
			p.hidden = true;
		};
	}
	document.addEventListener('mousedown', e => { if (!e.target.closest('#emojiPicker') && !e.target.closest('[data-act="react"]')) $('#emojiPicker').hidden = true; });

	// ---------- header + sidebar controls ----------
	$('#search').addEventListener('input', e => { S.search = e.target.value.trim().toLowerCase(); renderMessages(); });
	$('#exportBtn').onclick = () => {
		const lines = [`# #${S.current} — exported ${new Date().toLocaleString()}`, ''];
		for (const m of list(S.current)) {
			lines.push(`**${m.author}** _${new Date(m.ts).toLocaleString()}_`, '');
			if (m.sync && m.sync.length) {
				lines.push('<details><summary><i>AI sync</i></summary>', '');
				for (const s of m.sync) lines.push(`*${s.fromName} → ${s.toName}:*`, '', s.text, '');
				lines.push('</details>', '');
			}
			lines.push(m.text || (m.error ? `⚠ ${m.error}` : ''), '', '---', '');
		}
		const a = document.createElement('a');
		a.href = URL.createObjectURL(new Blob([lines.join('\n')], { type: 'text/markdown' }));
		a.download = `chat-${S.current}-${new Date().toISOString().slice(0, 10)}.md`;
		a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 1000);
	};
	$('#clearBtn').onclick = () => { if (confirm(`Clear all messages in #${S.current}?`)) api('POST', `/api/channels/${S.current}/clear`).catch(e => toast(e.message)); };
	$('#addChannel').onclick = async () => {
		const n = prompt('New channel name'); if (!n) return;
		try { const ch = await api('POST', '/api/channels', { name: n }); switchChannel(ch.id); } catch (e) { toast(e.message); }
	};
	$('#recheck').onclick = () => api('POST', '/api/health').then(a => { S.agents = a; renderSidebar(); toast('Re-checked AI CLIs'); }).catch(e => toast(e.message));
	$('#menuBtn').onclick = () => $('#sidebar').classList.toggle('open');

	function applyTheme() { document.documentElement.dataset.theme = pref.get('theme', 'dark'); }
	$('#themeBtn').onclick = () => { pref.set('theme', pref.get('theme', 'dark') === 'dark' ? 'light' : 'dark'); applyTheme(); };
	applyTheme();

	const optSound = $('#optSound'); optSound.checked = pref.get('sound', true);
	optSound.onchange = () => { pref.set('sound', optSound.checked); if (optSound.checked) beep(); };
	const optSync = $('#optSyncOpen'); optSync.checked = pref.get('syncOpen', false);
	optSync.onchange = () => { pref.set('syncOpen', optSync.checked); renderMessages(); };
	const optNotify = $('#optNotify'); optNotify.checked = pref.get('notify', false);
	optNotify.onchange = async () => {
		if (optNotify.checked && 'Notification' in window && Notification.permission !== 'granted') {
			const p = await Notification.requestPermission();
			if (p !== 'granted') { optNotify.checked = false; toast('Notifications blocked by the browser'); }
		}
		pref.set('notify', optNotify.checked);
	};

	// ---------- login ----------
	let es;
	function setName(n) {
		n = n.replace(/[^\w .-]/g, '').trim().slice(0, 32);
		if (!n) return;
		if (S.agents.some(a => a.name.toLowerCase() === n.toLowerCase()) || /^(claude|codex|both|ai|all|everyone|here)$/i.test(n)) {
			$('#loginErr').textContent = 'That name is reserved.'; toast('That name is reserved'); return false;
		}
		S.me = n; pref.set('name', n);
		$('#meName').textContent = n;
		if (es) location.reload();
		return true;
	}
	$('#renameBtn').onclick = () => { const n = prompt('New display name', S.me); if (n) setName(n); };

	async function start() {
		$('#login').hidden = true; $('#app').hidden = false;
		$('#meName').textContent = S.me;
		try { await load(); } catch (e) { toast('Cannot reach server: ' + e.message, 6000); }
		es = true; connect();
		input.focus();
	}
	$('#loginForm').addEventListener('submit', e => {
		e.preventDefault();
		if (setName($('#loginName').value)) start();
	});
	if (S.me) start(); else { $('#login').hidden = false; $('#loginName').focus(); }
})();
