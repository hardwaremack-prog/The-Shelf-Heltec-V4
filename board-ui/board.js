// The Shelf for Heltec V4 (and V3): small touches on top of The Shelf's own page.
(function () {
  async function boardInfo() {
    try { return await fetch('/api/board/info').then(r => r.json()); } catch { return null; }
  }
  function fmt(b) { return b >= 1e9 ? (b / 1e9).toFixed(1) + ' GB' : b >= 1e6 ? (b / 1e6).toFixed(1) + ' MB' : Math.round(b / 1e3) + ' KB'; }
  async function decorate() {
    const info = await boardInfo();
    // On its own Wi-Fi the board has no internet clock: lend it this device's time.
    if (info && !info.timeSet) fetch('/api/board/time', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ ms: Date.now() }) }).catch(() => {});
    const p = document.querySelector('.brand-text p');
    if (p) p.textContent = 'running on a Heltec V4';
    const net = document.getElementById('networkUrl');
    if (net && info) net.textContent = (info.hotspot || (info.ap && !info.ssid)) ? info.ip : info.host + '.local';
    const scroll = document.querySelector('.sidebar-scroll');
    if (scroll && !document.querySelector('.board-badge')) {
      const a = document.createElement('a');
      a.className = 'board-badge'; a.href = '/board/';
      a.innerHTML = '<span class="chip">V4</span><span><b>Heltec V4</b><small>' +
        (info ? fmt(info.total - info.used) + ' free on ' + (info.storage === 'microSD card' ? 'microSD' : 'the board') : 'Board settings') +
        ' · Wi-Fi &amp; settings</small></span>';
      scroll.appendChild(a);
    }
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', decorate); else decorate();
})();

// ---- Read-only mode: a switch in the sidebar, and an unlock prompt when a change is refused ----
(function () {
  const esc = s => String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  const _fetch = window.fetch.bind(window);
  let A = null, dialogOpen = false, dismissedAt = 0;
  const post = (u, d) => _fetch(u, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(d || {}) });

  // A small password dialog. onSubmit returns an error message, or nothing when it worked.
  window.shelfAskPassword = function (title, sub, button, onSubmit) {
    const ov = document.createElement('div');
    ov.className = 'rm-overlay';
    ov.innerHTML = `<form class="rm-box rm-small"><h3>${esc(title)}</h3><p class="rm-sub">${sub}</p>
      <input type="password" class="rm-input" placeholder="Admin password" autocomplete="current-password">
      <div class="rm-err"></div>
      <div class="rm-row"><button type="button" class="coms-btn secondary rm-cancel">Cancel</button><button type="submit" class="coms-btn">${esc(button)}</button></div></form>`;
    document.body.appendChild(ov);
    const input = ov.querySelector('input'), err = ov.querySelector('.rm-err');
    const close = () => { ov.remove(); };
    ov.querySelector('.rm-cancel').onclick = close;
    ov.addEventListener('mousedown', e => { if (e.target === ov) close(); });
    ov.addEventListener('keydown', e => { if (e.key === 'Escape') close(); });
    ov.querySelector('form').onsubmit = async e => {
      e.preventDefault();
      err.textContent = 'Checking…';
      const msg = await onSubmit(input.value);
      if (msg) { err.textContent = msg; input.select(); } else close();
    };
    setTimeout(() => input.focus(), 30);
    return close;
  };

  async function load() {
    try { A = await _fetch('/api/admin/status').then(r => r.json()); } catch { return; }
    let pill = document.getElementById('roPill');
    const badge = document.querySelector('.board-badge');
    if (!A.adminSet || !A.readOnly) { if (pill) pill.remove(); return; }
    if (!pill) {
      if (!badge) return;
      pill = document.createElement('button');
      pill.id = 'roPill'; pill.className = 'ro-pill';
      badge.insertAdjacentElement('afterend', pill);
      pill.addEventListener('click', () => A.unlocked ? lock() : unlock(false));
    }
    pill.classList.toggle('open', !!A.unlocked);
    pill.innerHTML = A.unlocked
      ? '<span class="ro-dot"></span><span><b>Write mode</b><small>Unlocked in this browser · tap to lock</small></span>'
      : '<span class="ro-dot"></span><span><b>Read-only</b><small>Tap and enter the admin password to make changes</small></span>';
  }
  async function lock() { await post('/api/admin/lock'); await load(); if (typeof loadAuthStatus === 'function') loadAuthStatus(); }
  function unlock(blocked) {
    if (dialogOpen) return;
    dialogOpen = true;
    const closeOrig = window.shelfAskPassword(blocked ? 'This Shelf is read-only' : 'Unlock write mode',
      (blocked ? 'That change wasn’t saved. ' : '') + 'Enter the admin password to make changes from this browser for the next 8 hours.' + (blocked ? ' Then try it again.' : ''),
      'Unlock', async pw => {
        const r = await post('/api/admin/unlock', { password: pw });
        if (!r.ok) { const j = await r.json().catch(() => ({})); return j.error || 'Couldn’t unlock.'; }
        dialogOpen = false; await load(); if (typeof loadAuthStatus === 'function') loadAuthStatus();
        return '';
      });
    const obs = new MutationObserver(() => { if (!document.querySelector('.rm-overlay')) { dialogOpen = false; dismissedAt = Date.now(); obs.disconnect(); } });
    obs.observe(document.body, { childList: true });
    void closeOrig;
  }
  function refused() { if (Date.now() - dismissedAt > 2500) unlock(true); }
  // Any refused change (fetch or upload) brings up the unlock prompt.
  // The Shelf's upload list only says "failed": show why, from the board's answer.
  function explainUpload(why) {
    setTimeout(() => {
      const st = [...document.querySelectorAll('#uploadList .status.err')].find(x => !x.dataset.why);
      if (!st) return;
      st.dataset.why = '1';
      st.title = why;
      const m = document.createElement('div');
      m.className = 'upload-why'; m.textContent = why;
      st.parentElement.appendChild(m);
    }, 60);
  }
  window.fetch = async function (...args) {
    const isUpload = String(args[0]).includes('/api/upload');
    let r;
    try { r = await _fetch(...args); }
    catch (e) { if (isUpload) explainUpload('The connection to the board dropped partway through. Try again, closer to the board or one file at a time.'); throw e; }
    if (r.status === 403 && r.headers.get('X-Shelf-ReadOnly')) { refused(); load(); }
    else if (isUpload && !r.ok) r.clone().json().then(j => explainUpload(j.error || ('The board answered ' + r.status))).catch(() => explainUpload('The board answered ' + r.status));
    return r;
  };
  const send = XMLHttpRequest.prototype.send;
  XMLHttpRequest.prototype.send = function (...a) {
    this.addEventListener('load', () => { if (this.status === 403 && this.getResponseHeader('X-Shelf-ReadOnly')) { refused(); load(); } });
    return send.apply(this, a);
  };
  window.shelfReadOnlyRefresh = load;
  const start = () => { load(); setInterval(load, 20000); };
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', () => setTimeout(start, 300)); else setTimeout(start, 300);
})();

// ---- 900MHz Coms: a simple LoRa text mesh between Shelf boards ----
(function () {
  let timer = null, last = '', drafting = false;
  const LEVEL_NAMES = ['Low', 'Normal', 'High'];
  const ago = t => { const s = Math.max(0, (Date.now() - t) / 1000); return s < 60 ? 'just now' : s < 3600 ? Math.floor(s / 60) + ' min ago' : s < 86400 ? Math.floor(s / 3600) + ' h ago' : new Date(t).toLocaleDateString(); };
  const time = t => new Date(t).toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' });
  const post = (u, d) => fetch(u, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(d || {}) });

  function viewing() { return typeof state !== 'undefined' && state.view === 'coms'; }

  async function refresh(force) {
    if (!viewing()) { clearInterval(timer); timer = null; return; }
    let c; try { c = await fetch('/api/coms').then(r => r.json()); } catch { return; }
    const sig = JSON.stringify([c.enabled, c.radio, c.receipts, c.log.length, c.log.length && c.log[c.log.length - 1].id, c.neighbors.length, c.error, c.heard, c.relayed, c.synced, c.receivedBy,
      (c.devices || []).map(d => [d.node, d.name, d.hops, d.batt, d.ro, d.admin, Math.floor((Date.now() - d.at) / 60000)])]);
    if (!force && sig === last) return;
    last = sig;
    render(c);
  }

  function render(c) {
    const content = el('#content');
    const keep = content.querySelector('#comsInput');
    const draft = keep ? keep.value : '';
    const hadFocus = keep && document.activeElement === keep;
    const neigh = c.neighbors.filter(n => n.node !== c.node);
    const devs = (c.devices || []).filter(d => d.node !== c.node).sort((a, b) => b.at - a.at);
    const log = c.log.slice().sort((a, b) => a.t - b.t);
    let day = '';
    const msgs = log.map(m => {
      const d = new Date(m.t).toDateString(); let sep = '';
      if (d !== day) { day = d; sep = `<div class="coms-day">${escapeHtml(d === new Date().toDateString() ? 'Today' : new Date(m.t).toLocaleDateString([], { weekday: 'long', month: 'short', day: 'numeric' }))}</div>`; }
      const got = (c.receivedBy || {})[m.id] || [];
      const meta = m.mine ? `${time(m.t)} · ${got.length ? '\u2713 Received by ' + got.map(escapeHtml).join(', ') : 'sent'}` : `${time(m.t)} · ${m.hops ? m.hops + (m.hops === 1 ? ' hop' : ' hops') : 'direct'}${m.rssi !== undefined ? ' · ' + m.rssi + ' dBm' : ''}${m.synced ? ' · caught up' : ''}`;
      return sep + `<div class="coms-msg ${m.mine ? 'mine' : ''}"><div class="who">${escapeHtml(m.mine ? 'You' : (m.from || 'Unknown'))}</div><div class="txt">${escapeHtml(m.text)}</div><div class="meta">${meta}</div></div>`;
    }).join('');
    content.innerHTML = `
      <div class="coms">
        <div class="coms-card">
          <div class="coms-top">
            <label class="toggle-switch"><input type="checkbox" id="comsToggle" ${c.enabled ? 'checked' : ''}><span class="toggle-slider"></span></label>
            <span class="power-label">${c.enabled ? (c.radio ? '900MHz Coms is on' : 'Starting the radio…') : '900MHz Coms is off'}</span>
            <span class="sub">${c.enabled ? 'This board is on the mesh and relays messages for everyone else on it.' : 'Turn it on to join the mesh. Any Shelf board with Coms on can talk, and each one relays for the others.'}</span>
          </div>
          ${c.enabled ? `
          <div class="coms-chips">
            <span class="coms-chip">You are <b>${escapeHtml(c.name)}</b></span>
            <span class="coms-chip">${c.freq.toFixed(1)} MHz</span>
            <span class="coms-chip">Heard <b>${c.heard}</b></span>
            <span class="coms-chip">Relayed <b>${c.relayed}</b></span>
            <span class="coms-chip">Caught up <b>${c.synced}</b></span>
            <button class="coms-chip coms-receipts ${c.receipts ? 'on' : ''}" id="comsReceipts" title="When on, this board tells senders it got their message">${c.receipts ? '\u2713 Sending \u201creceived\u201d' : 'Not sending \u201creceived\u201d'}</button>
          </div>
          <div class="coms-devs-head"><span>Boards on the mesh</span><button class="coms-chip coms-who" id="comsWho" title="Ask every board on the mesh to say hello">Refresh</button></div>
          <div class="coms-devs">${devs.length ? devs.map(d => `<button class="coms-dev" data-node="${d.node}">
              <span class="cd-chip">V${d.board || '?'}</span>
              <span class="cd-main"><b>${escapeHtml(d.name || d.node)}</b><small>${d.hops ? d.hops + (d.hops === 1 ? ' hop' : ' hops') : 'direct' + (d.rssi ? ' · ' + d.rssi + ' dBm' : '')} · ${d.batt < 0 ? 'USB' : d.batt + '%'} · ${ago(d.at)}${d.ro ? ' · 🔒' : ''}</small></span>
              <span class="cd-go">${d.admin ? 'Manage ›' : '<span class="cd-none" title="Set an admin password on that board to manage it from here">No password</span>'}</span></button>`).join('')
            : '<div class="coms-neigh" style="margin:0">No boards yet. Each board says hello when it comes on and every 15 minutes. Tap Refresh to ask now.</div>'}</div>
          <div class="coms-neigh">${neigh.length ? 'In range: ' + neigh.map(n => `<b style="color:var(--text)">${escapeHtml(n.name || n.node)}</b> (${n.rssi} dBm, ${ago(n.at)})`).join(', ') : 'No other boards heard yet. They show up here as soon as one transmits.'}</div>
          <details style="margin-top:10px"><summary style="cursor:pointer;color:var(--text-muted);font-size:13px">Settings</summary>
            <div class="coms-settings">
              <label>Your name on the mesh<input id="comsName" maxlength="16" value="${escapeHtml(c.name)}"></label>
              <label>Transmit power<select id="comsLevel">${LEVEL_NAMES.map((l, i) => `<option value="${i}" ${i === c.level ? 'selected' : ''}>${l} (~${(c.levels || [11, 20, 27])[i]} dBm)</option>`).join('')}</select></label>
              <button class="coms-btn secondary" id="comsSave">Save</button>
              <button class="coms-btn secondary" id="comsClear">Clear log</button>
            </div>
          </details>` : ''}
          ${c.error ? `<div class="coms-err">${escapeHtml(c.error)}</div>` : ''}
        </div>
        ${c.enabled ? `
          <div class="coms-log" id="comsLog">${msgs || '<div class="coms-off" style="margin:auto;text-align:center">No messages in the last while.<br>Anything said on the mesh shows up here, and when this board joins, nearby boards fill in the last 12 hours.</div>'}</div>
          <form class="coms-compose" id="comsForm"><input id="comsInput" maxlength="180" placeholder="Message everyone on the mesh…" autocomplete="off"><button type="submit">Send</button></form>
          <div class="coms-count" id="comsCount"></div>
        ` : `<div class="coms-card coms-off">
            <b>How it works:</b> every Heltec V3 and V4 running The Shelf has a 900 MHz LoRa radio. With Coms on, boards in range hear each other directly, and each board re-sends what it hears, so messages hop from board to board as far as the mesh reaches. A board that joins late is caught up on the last 12 hours by its neighbors. No internet or Wi-Fi needed between boards.<br><br>
            Fit the LoRa antenna before switching it on. It uses its own channel (915.0 MHz), separate from Meshtastic.</div>`}
      </div>`;
    const logEl = content.querySelector('#comsLog');
    if (logEl) logEl.scrollTop = logEl.scrollHeight;
    content.querySelector('#comsToggle').addEventListener('change', async e => { await post('/api/coms/toggle', { on: e.target.checked }); last = ''; setTimeout(() => refresh(true), 400); });
    const input = content.querySelector('#comsInput');
    if (input) {
      input.value = draft;
      if (hadFocus) input.focus();
      const count = () => { const n = new TextEncoder().encode(input.value).length; content.querySelector('#comsCount').textContent = n ? `${n}/180` : ''; };
      input.addEventListener('input', count); count();
      content.querySelector('#comsForm').addEventListener('submit', async e => {
        e.preventDefault();
        const text = input.value.trim(); if (!text) return;
        if (new TextEncoder().encode(text).length > 180) { alert('That’s too long for one radio message (180 characters max).'); return; }
        input.value = '';
        const r = await post('/api/coms/send', { text });
        if (!r.ok) { const j = await r.json().catch(() => ({})); alert(j.error || 'Couldn’t send.'); input.value = text; }
        last = ''; refresh(true);
      });
    }
    const who = content.querySelector('#comsWho');
    if (who) who.addEventListener('click', async () => { who.textContent = 'Asking…'; await post('/api/coms/remote/who'); setTimeout(() => { who.textContent = 'Refresh'; }, 8000); });
    content.querySelectorAll('.coms-dev').forEach(b => b.addEventListener('click', () => {
      const d = devs.find(x => x.node === b.dataset.node);
      if (d) openRemote(d);
    }));
    const rc = content.querySelector('#comsReceipts');
    if (rc) rc.addEventListener('click', async () => { await post('/api/coms/settings', { receipts: !c.receipts }); last = ''; refresh(true); });
    const save = content.querySelector('#comsSave');
    if (save) save.addEventListener('click', async () => { await post('/api/coms/settings', { name: content.querySelector('#comsName').value, level: Number(content.querySelector('#comsLevel').value) }); last = ''; refresh(true); });
    const clear = content.querySelector('#comsClear');
    if (clear) clear.addEventListener('click', async () => { if (confirm('Clear the message log on this board?')) { await post('/api/coms/clear'); last = ''; refresh(true); } });
  }

  // ---- Remote admin over 900MHz: connect to a board with its admin password ----
  const SHELVES = ['Pictures', 'Music', 'MIDI', 'Videos', 'Games', 'Programs', 'Books'];
  function openRemote(dev) {
    const R = { dev, session: null, busy: null, st: null, out: '', err: '' };
    const ov = document.createElement('div');
    ov.className = 'rm-overlay';
    document.body.appendChild(ov);
    const close = () => { if (R.session) post('/api/coms/remote/disconnect', { session: R.session }); ov.remove(); };
    ov.addEventListener('mousedown', e => { if (e.target === ov) close(); });
    ov.addEventListener('keydown', e => { if (e.key === 'Escape') close(); });
    const nm = escapeHtml(dev.name || dev.node);
    const up = s => s > 86400 ? Math.floor(s / 86400) + 'd ' + Math.floor(s % 86400 / 3600) + 'h' : Math.floor(s / 3600) + 'h ' + Math.floor(s % 3600 / 60) + 'm';
    const fmtB = b => b >= 1e9 ? (b / 1e9).toFixed(1) + ' GB' : b >= 1e6 ? (b / 1e6).toFixed(1) + ' MB' : Math.round(b / 1e3) + ' KB';

    async function waitFor(req, label) {
      const t0 = Date.now();
      R.busy = label; draw();
      for (;;) {
        await new Promise(r => setTimeout(r, 1000));
        if (!document.body.contains(ov)) return null;
        let j; try { j = await fetch('/api/coms/remote/result?req=' + req).then(r => r.json()); } catch { continue; }
        if (j.state === 'waiting') { R.busy = `${label}… ${Math.round((Date.now() - t0) / 1000)} s`; drawBusy(); continue; }
        R.busy = null;
        return j;
      }
    }
    async function cmd(n, arg, label, confirmText) {
      if (R.busy) return;
      if (confirmText && !confirm(confirmText)) return;
      R.err = '';
      const r = await post('/api/coms/remote/cmd', { session: R.session, cmd: n, arg: arg == null ? '' : String(arg) });
      const j = await r.json().catch(() => ({}));
      if (!r.ok) { R.err = j.error || 'Couldn’t send that.'; if (r.status === 401) R.session = null; draw(); return; }
      const t0 = Date.now();
      const res = await waitFor(j.req, label + ' — waiting for ' + (dev.name || 'it'));
      if (!res) return;
      if (res.state === 'failed') R.err = res.error;
      else if (res.status) R.st = res.status;
      if (res.text) R.out = res.text + (n === 10 ? ` (answered in ${Math.round((Date.now() - t0) / 1000)} s)` : '');
      draw();
    }
    async function connect(pw) {
      R.err = '';
      const r = await post('/api/coms/remote/connect', { node: dev.node, password: pw });
      const j = await r.json().catch(() => ({}));
      if (!r.ok) { R.err = j.error || 'Couldn’t connect.'; draw(); return; }
      R.session = j.session;
      const res = await waitFor(j.req, 'Asking ' + (dev.name || 'it'));
      if (!res) return;
      if (res.state === 'failed') { R.err = res.error; post('/api/coms/remote/disconnect', { session: R.session }); R.session = null; }
      else R.st = res.status;
      draw();
    }
    function drawBusy() { const b = ov.querySelector('.rm-busy'); if (b) b.textContent = R.busy || ''; else draw(); }
    function draw() {
      const s = R.st;
      let body;
      if (!R.session || !s) {
        body = `<p class="rm-sub">Sends a signed request over 900 MHz${dev.hops ? `, through ${dev.hops} other ${dev.hops === 1 ? 'board' : 'boards'}` : ''}. Nothing is encrypted, so anyone listening on 915 MHz can read the commands and replies, but only someone with that board's admin password can give them.</p>
          ${R.busy ? '' : `<input type="password" class="rm-input" id="rmPw" placeholder="${nm}'s admin password" autocomplete="current-password">
          <div class="rm-row"><button type="button" class="coms-btn secondary" data-close>Cancel</button><button class="coms-btn" id="rmGo">Connect</button></div>`}`;
      } else {
        const kv = [['Board', `V${escapeHtml(s.b)} · v${escapeHtml(s.fw)}`], ['Battery', s.bat < 0 ? 'On USB power' : s.bat + '%'], ['Up for', s.up != null ? up(s.up) : '—'],
          ['Wi-Fi', escapeHtml(s.w || '—')], ['Address', escapeHtml(s.ip || '—') + (s.cl ? ` · ${s.cl} connected` : '')], ['Storage', `${s.st === 'SD' ? 'microSD' : 'Board flash'}` + (s.fm != null ? ` · ${fmtB(s.fm * 1e6)} free` : '')],
          ['Mode', s.ro ? '🔒 Read-only' : 'Anyone can make changes'], ['Screen', s.scr ? 'On' : 'Asleep'],
          ['On the mesh', `${escapeHtml(s.cm || dev.name || '')} · ${LEVEL_NAMES[s.lv]} (~${(s.b === '3' ? [11, 17, 22] : [11, 20, 27])[s.lv]} dBm)` + (s.mb != null ? ` · knows ${s.mb} ${s.mb === 1 ? 'board' : 'boards'}` : '')],
          ['Heard / relayed', (s.hd != null ? `${s.hd} / ${s.rl}` : '—') + (s.rc ? ' · sends receipts' : '')]];
        const dis = R.busy ? 'disabled' : '';
        body = `<div class="rm-kv">${kv.map(r => `<div><span>${r[0]}</span><span>${r[1]}</span></div>`).join('')}</div>
          <div class="rm-grid">
            <div class="rm-sec"><label>Mode</label><div class="rm-seg"><button ${dis} class="${s.ro ? 'on' : ''}" data-c="2" data-a="1">Read-only</button><button ${dis} class="${s.ro ? '' : 'on'}" data-c="2" data-a="0">Anyone can change</button></div></div>
            <div class="rm-sec"><label>Screen</label><div class="rm-seg"><button ${dis} class="${s.scr ? 'on' : ''}" data-c="4" data-a="1">Wake</button><button ${dis} class="${s.scr ? '' : 'on'}" data-c="4" data-a="0">Sleep</button></div></div>
            <div class="rm-sec"><label>Transmit power</label><div class="rm-seg">${LEVEL_NAMES.map((l, i) => `<button ${dis} class="${i === s.lv ? 'on' : ''}" data-c="5" data-a="${i}">${l}</button>`).join('')}</div></div>
            <div class="rm-sec"><label>"Received" receipts</label><div class="rm-seg"><button ${dis} class="${s.rc ? 'on' : ''}" data-c="6" data-a="1">On</button><button ${dis} class="${s.rc ? '' : 'on'}" data-c="6" data-a="0">Off</button></div></div>
            <div class="rm-sec"><label>Name on the mesh</label><div class="rm-inline"><input id="rmName" maxlength="16" value="${escapeHtml(s.cm || '')}"><button ${dis} class="coms-btn secondary" id="rmRename">Rename</button></div></div>
            <div class="rm-sec"><label>Look in a shelf</label><div class="rm-inline"><select id="rmShelf">${SHELVES.map(x => `<option>${x}</option>`).join('')}</select><button ${dis} class="coms-btn secondary" id="rmList">List files</button></div></div>
          </div>
          ${R.out ? `<div class="rm-out">${escapeHtml(R.out)}</div>` : ''}
          <div class="rm-row rm-wrap">
            <button ${dis} class="coms-btn secondary" data-c="1">Refresh</button>
            <button ${dis} class="coms-btn secondary" data-c="10">Ping</button>
            <button ${dis} class="coms-btn secondary" data-c="7" data-confirm="Switch ${nm} to its own Wi-Fi? It restarts, and stays on the mesh.">Its own Wi-Fi</button>
            <button ${dis} class="coms-btn secondary" data-c="8" data-confirm="Switch ${nm} back to its home Wi-Fi? It restarts, and stays on the mesh.">Home Wi-Fi</button>
            <button ${dis} class="coms-btn secondary rm-bad" data-c="3" data-confirm="Restart ${nm}?">Restart</button>
            <span style="flex:1"></span>
            <button class="coms-btn secondary" data-close>Disconnect</button>
          </div>`;
      }
      ov.innerHTML = `<div class="rm-box"><div class="rm-head"><span class="cd-chip">V${dev.board || '?'}</span><h3>${nm}</h3><button class="rm-x" data-close title="Close">&times;</button></div>
        ${body}<div class="rm-busy">${R.busy ? escapeHtml(R.busy) : ''}</div><div class="rm-err">${escapeHtml(R.err)}</div></div>`;
      ov.querySelectorAll('[data-close]').forEach(b => b.onclick = close);
      const go = ov.querySelector('#rmGo'), pw = ov.querySelector('#rmPw');
      if (go) { go.onclick = () => { if (pw.value) connect(pw.value); }; pw.onkeydown = e => { if (e.key === 'Enter' && pw.value) connect(pw.value); }; setTimeout(() => pw.focus(), 30); }
      ov.querySelectorAll('[data-c]').forEach(b => b.onclick = () => cmd(Number(b.dataset.c), b.dataset.a, b.textContent.trim(), b.dataset.confirm));
      const rn = ov.querySelector('#rmRename'); if (rn) rn.onclick = () => cmd(9, ov.querySelector('#rmName').value.trim(), 'Renaming');
      const ls = ov.querySelector('#rmList'); if (ls) ls.onclick = () => { R.out = ''; cmd(11, ov.querySelector('#rmShelf').value, 'Listing ' + ov.querySelector('#rmShelf').value); };
    }
    draw();
  }

  window.loadComsView = function () {
    setToolHeader('900MHz Coms', 'A simple radio mesh between Shelf boards', null, null);
    last = '';
    refresh(true);
    clearInterval(timer);
    timer = setInterval(() => { if (document.activeElement && document.activeElement.id === 'comsName') return; refresh(false); }, 2000);
  };
})();

// ---- iPhone/iPad: visualizer bars that start quickly, and lock-screen controls that stay ----
// The Shelf draws the bars on iOS from the song itself: it downloads the whole file a second
// time and decodes it. From the board that takes a while, and when it's too slow The Shelf
// falls back to a hidden second <audio> routed through Web Audio, which is what makes iOS lose
// the lock-screen play/pause/next controls. Here the song is fetched in 512 KB pieces and each
// piece decoded as it arrives, so the bars start within seconds, and the hidden copy is never used.
(function () {
  if (typeof IS_IOS === 'undefined' || !IS_IOS || typeof preparePcmViz !== 'function') return;
  try { ensureShadowVisualizer = function () {}; } catch (e) {}           // never the hidden second player
  const CHUNK = 512 * 1024;
  const isMidiUrl = u => /\.(mid|midi|kar|rmi)$/i.test(String(u).split('?')[0]);
  preparePcmViz = async function () {
    const src = audio.src;
    if (!src || pcmViz.src === src) return;
    const st = { src, data: null, rate: PCM_VIZ_RATE, ready: false, fallback: false };
    pcmViz = st; pcmVizSmoothed.fill(0);
    if (isMidiUrl(src)) return;                                              // MIDI: no file to decode
    try {
      const Offline = window.OfflineAudioContext || window.webkitOfflineAudioContext;
      const duration = await waitForDuration(20000);
      if (pcmViz !== st) return;
      let total = Infinity, pos = 0, filled = 0;
      let data = new Float32Array(Math.max(1, Math.ceil((duration || 240) * PCM_VIZ_RATE)));
      const place = (mono, at) => {
        if (at + mono.length > data.length) { const d = new Float32Array(Math.ceil((at + mono.length) * 1.25)); d.set(data); data = d; st.data = data; }
        data.set(mono, at);
      };
      while (pos < total) {
        if (pcmViz !== st) return;                                           // a different track started
        const r = await fetch(src, { headers: { Range: 'bytes=' + pos + '-' + (pos + CHUNK - 1) } });
        if (r.status !== 206 && r.status !== 200) throw new Error('fetch ' + r.status);
        const cr = r.headers.get('Content-Range');
        const bytes = await r.arrayBuffer();
        if (r.status === 200) total = bytes.byteLength; else if (cr) total = +cr.split('/')[1] || Infinity;
        const startByte = pos; pos += bytes.byteLength;
        if (!bytes.byteLength) break;
        // where this piece starts in the song: by samples decoded so far, or by bytes if a piece failed
        const expected = isFinite(total) && duration ? Math.floor(startByte / total * duration * PCM_VIZ_RATE) : filled;
        const at = Math.abs(expected - filled) > PCM_VIZ_RATE * 2 ? expected : filled;
        try {
          const dec = await decodeToBuffer(new Offline(1, 1, PCM_VIZ_RATE), bytes);
          if (pcmViz !== st) return;
          const mono = new Float32Array(dec.length);
          for (let c = 0; c < dec.numberOfChannels; c++) { const ch = dec.getChannelData(c); for (let i = 0; i < mono.length; i++) mono[i] += ch[i] / dec.numberOfChannels; }
          place(mono, at); filled = at + mono.length;
        } catch (e) { filled = at; }                                         // undecodable piece: leave it quiet
        st.data = data; st.rate = PCM_VIZ_RATE; st.ready = true;              // bars start after the first piece
      }
    } catch (e) { /* no bars for this song; playback and the lock screen are unaffected */ }
  };
})();
