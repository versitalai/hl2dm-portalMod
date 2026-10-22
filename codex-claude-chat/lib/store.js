'use strict';
// JSON-file persistence. Everything lives in memory; writes are debounced and
// atomic (write tmp file, then rename) so a crash mid-write can't corrupt history.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const MAX_PER_CHANNEL = 2000;

class Store {
	constructor(dataDir) {
		this.file = path.join(dataDir, 'chat.json');
		fs.mkdirSync(dataDir, { recursive: true });
		this.data = { channels: [{ id: 'general', name: 'general' }], messages: { general: [] } };
		if (fs.existsSync(this.file)) {
			try {
				this.data = JSON.parse(fs.readFileSync(this.file, 'utf8'));
			} catch (e) {
				const bad = this.file + '.corrupt-' + Date.now();
				fs.renameSync(this.file, bad);
				console.error(`[store] chat.json unreadable, moved to ${bad}`);
			}
		}
		// Pending agent replies can't survive a restart: mark them interrupted.
		for (const list of Object.values(this.data.messages)) {
			for (const m of list) {
				if (m.pending) { m.pending = false; m.status = 'error'; m.error = 'Server restarted before reply finished.'; }
			}
		}
		this.timer = null;
	}

	static id() { return crypto.randomBytes(6).toString('hex'); }

	save() {
		clearTimeout(this.timer);
		this.timer = setTimeout(() => this.flush(), 250);
	}

	flush() {
		clearTimeout(this.timer);
		const tmp = this.file + '.tmp';
		fs.writeFileSync(tmp, JSON.stringify(this.data));
		fs.renameSync(tmp, this.file);
	}

	channels() { return this.data.channels; }

	addChannel(name) {
		const id = name.toLowerCase().replace(/[^a-z0-9-]+/g, '-').replace(/^-|-$/g, '').slice(0, 32);
		if (!id) throw new Error('Invalid channel name');
		if (!this.data.messages[id]) {
			this.data.channels.push({ id, name: id });
			this.data.messages[id] = [];
			this.save();
		}
		return this.data.channels.find(c => c.id === id);
	}

	deleteChannel(id) {
		if (id === 'general') throw new Error('Cannot delete #general');
		this.data.channels = this.data.channels.filter(c => c.id !== id);
		delete this.data.messages[id];
		this.save();
	}

	list(channel) { return this.data.messages[channel] || []; }

	get(id) {
		for (const list of Object.values(this.data.messages)) {
			const m = list.find(x => x.id === id);
			if (m) return m;
		}
		return null;
	}

	add(msg) {
		const list = this.data.messages[msg.channel];
		if (!list) throw new Error('Unknown channel');
		const m = { id: Store.id(), ts: Date.now(), reactions: {}, ...msg };
		list.push(m);
		if (list.length > MAX_PER_CHANNEL) list.splice(0, list.length - MAX_PER_CHANNEL);
		this.save();
		return m;
	}

	update(id, patch) {
		const m = this.get(id);
		if (!m) return null;
		Object.assign(m, patch);
		this.save();
		return m;
	}

	remove(id) {
		for (const list of Object.values(this.data.messages)) {
			const i = list.findIndex(x => x.id === id);
			if (i >= 0) { const [m] = list.splice(i, 1); this.save(); return m; }
		}
		return null;
	}

	clear(channel) {
		if (this.data.messages[channel]) { this.data.messages[channel] = []; this.save(); }
	}
}

module.exports = { Store };
