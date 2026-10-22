'use strict';
// Tiny, safe Markdown subset. Safety rule: HTML-escape the whole input FIRST, then only
// ever add tags we generate ourselves, so AI or user text can never inject markup.
(function () {
	function esc(s) {
		return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;').replace(/'/g, '&#39;');
	}

	function inline(s, mentionClass) {
		return s
			.replace(/\*\*([^*\n]+)\*\*/g, '<b>$1</b>')
			.replace(/(^|[^\w*])\*([^*\n]+)\*(?!\w)/g, '$1<i>$2</i>')
			.replace(/(^|[^\w])_([^_\n]+)_(?!\w)/g, '$1<i>$2</i>')
			.replace(/~~([^~\n]+)~~/g, '<s>$1</s>')
			.replace(/\[([^\]\n]+)\]\((https?:\/\/[^\s)]+)\)/g, '<a href="$2" target="_blank" rel="noopener noreferrer">$1</a>')
			.replace(/(^|[\s(])(https?:\/\/[^\s<)]+)/g, '$1<a href="$2" target="_blank" rel="noopener noreferrer">$2</a>')
			.replace(/(^|[^\w@./])@([A-Za-z][\w-]{0,31})/g, (_, pre, name) => `${pre}<span class="at ${mentionClass(name)}">@${name}</span>`);
	}

	function render(src, mentionClass) {
		mentionClass = mentionClass || (() => '');
		const blocks = [];
		const codes = [];
		let s = esc(src || '');
		s = s.replace(/```([\w+-]*)[^\n]*\n?([\s\S]*?)(?:```|$)/g, (_, lang, code) => {
			blocks.push(`<pre><code data-lang="${lang}">${code.replace(/\n$/, '')}</code><button class="copy" type="button">Copy</button></pre>`);
			return `\u0000${blocks.length - 1}\u0000`;
		});
		s = s.replace(/`([^`\n]+)`/g, (_, c) => { codes.push(`<code>${c}</code>`); return `\u0001${codes.length - 1}\u0001`; });

		const out = [];
		let list = null;   // 'ul' | 'ol'
		let para = [];
		const flushPara = () => { if (para.length) { out.push(`<p>${para.join('<br>')}</p>`); para = []; } };
		const closeList = () => { if (list) { out.push(`</${list}>`); list = null; } };

		for (const line of s.split('\n')) {
			let m;
			if ((m = line.match(/^\u0000(\d+)\u0000$/))) { flushPara(); closeList(); out.push(blocks[+m[1]]); continue; }
			if ((m = line.match(/^\s*[-*+] (.*)$/))) { flushPara(); if (list !== 'ul') { closeList(); out.push('<ul>'); list = 'ul'; } out.push(`<li>${inline(m[1], mentionClass)}</li>`); continue; }
			if ((m = line.match(/^\s*\d+[.)] (.*)$/))) { flushPara(); if (list !== 'ol') { closeList(); out.push('<ol>'); list = 'ol'; } out.push(`<li>${inline(m[1], mentionClass)}</li>`); continue; }
			closeList();
			if ((m = line.match(/^#{1,6} (.*)$/))) { flushPara(); out.push(`<h4>${inline(m[1], mentionClass)}</h4>`); continue; }
			if ((m = line.match(/^&gt; ?(.*)$/))) { flushPara(); out.push(`<blockquote>${inline(m[1], mentionClass)}</blockquote>`); continue; }
			if (!line.trim()) { flushPara(); continue; }
			para.push(inline(line, mentionClass));
		}
		flushPara(); closeList();
		return out.join('')
			.replace(/\u0001(\d+)\u0001/g, (_, i) => codes[+i])
			.replace(/\u0000(\d+)\u0000/g, (_, i) => blocks[+i]);
	}

	window.MD = { render, esc };
})();
