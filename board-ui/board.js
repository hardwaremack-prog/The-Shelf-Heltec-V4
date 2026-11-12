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

// ---- Pictures, cover art and thumbnails: a few at a time ----
// The shelf pages ask for every picture on screen at once. A board sends them slowly, and one
// without PSRAM (a plain ESP32) runs out of memory sending several big photos together. Here the
// pictures wait in a queue and load as they come near the screen: one at a time on a board without
// PSRAM, four at a time otherwise.
(function () {
  let limit = 2, active = 0;
  const q = [];
  fetch('/api/board/info').then(r => r.json()).then(i => { limit = i.psram ? 4 : 1; pump(); }).catch(() => {});
  function pump() {
    while (active < limit && q.length) {
      const img = q.shift();
      if (!img.isConnected || img.getAttribute('src')) continue;
      active++;
      let finished = false;
      const done = () => { if (finished) return; finished = true; active--; clearTimeout(t); setTimeout(pump, 0); };
      const t = setTimeout(done, 20000);                       // a stuck picture doesn't hold up the rest
      img.addEventListener('load', done, { once: true });
      img.addEventListener('error', done, { once: true });
      img.src = img.dataset.qsrc;
    }
  }
  const io = 'IntersectionObserver' in window ? new IntersectionObserver(es => {
    for (const e of es) if (e.isIntersecting) { io.unobserve(e.target); q.push(e.target); }
    pump();
  }, { rootMargin: '400px' }) : null;
  const watch = img => { if (img.getAttribute('src')) return; if (io) io.observe(img); else { q.push(img); pump(); } };
  new MutationObserver(ms => {
    for (const m of ms) for (const n of m.addedNodes) {
      if (n.nodeType !== 1) continue;
      if (n.matches && n.matches('img[data-qsrc]')) watch(n);
      if (n.querySelectorAll) n.querySelectorAll('img[data-qsrc]').forEach(watch);
    }
  }).observe(document.documentElement, { childList: true, subtree: true });
})();

// ---- Cover art and thumbnails: try again when the board was too busy ----
// A shelf asks for a picture per song or game, all at once. A board without PSRAM (a plain
// ESP32) can't always serve them all together, so a picture that fails or gets cut off is
// asked for again a moment later, up to 4 times, spread out so they don't all come back at once.
(function () {
  const isArt = img => /\/data-files\/(album-art|video-thumbs|file-thumbs)\//.test(img.getAttribute('src') || '');
  document.addEventListener('error', e => {
    const img = e.target;
    if (!(img instanceof HTMLImageElement) || !isArt(img)) return;
    const n = +(img.dataset.artRetry || 0);
    if (n >= 4) return;
    img.dataset.artRetry = n + 1;
    setTimeout(() => {
      try { const u = new URL(img.getAttribute('src'), location.href); u.searchParams.set('r', n + 1); img.src = u.pathname + u.search; } catch (x) {}
    }, 700 + Math.random() * 1500 * (n + 1));
  }, true);
})();

// ---- Bluetooth (plain ESP32 boards only): pair a headset and play the Music shelf on it ----
// The board itself plays the songs from its card to the headset, so nothing has to stay open
// on the phone. Only MP3 and WAV songs can go to the headset.
(function () {
  let timer = null, songs = null, filter = '', lastSig = '';
  const post = (u, d) => fetch(u, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(d || {}) }).then(r => r.json()).catch(() => ({}));
  const viewing = () => typeof state !== 'undefined' && state.view === 'bluetooth';
  const playable = n => /\.(mp3|wav)$/i.test(n);
  const pathOf = f => decodeURIComponent(String(f.url || '').replace(/^\/shared/, '')) || ('/Music/' + f.name);
  const mmss = s => { s = Math.max(0, Math.round(s || 0)); return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0'); };
  const bars = rssi => rssi > -60 ? '▂▄▆█' : rssi > -72 ? '▂▄▆' : rssi > -84 ? '▂▄' : '▂';
  const songName = p => (p || '').split('/').pop().replace(/\.[^.]+$/, '');

  async function loadSongs() {
    try { songs = (await fetch('/api/files?category=Music').then(r => r.json())).sort((a, b) => a.name.localeCompare(b.name)); } catch { songs = []; }
    drawSongs();
  }
  function shown() { const q = filter.trim().toLowerCase(); return (songs || []).filter(f => playable(f.name) && (!q || f.name.toLowerCase().includes(q))); }
  function drawSongs() {
    const box = document.getElementById('btSongs'); if (!box) return;
    if (!songs) { box.innerHTML = '<div class="bt-empty">Loading songs…</div>'; return; }
    const list = shown(), other = (songs || []).filter(f => !playable(f.name)).length;
    box.innerHTML = (list.length ? list.map((f, i) => `<button class="bt-song" data-i="${i}"><span class="bt-play">▶</span><span>${escapeHtml(f.name.replace(/\.[^.]+$/, ''))}</span></button>`).join('')
      : `<div class="bt-empty">${songs.length ? 'No songs match.' : 'No songs on the Music shelf yet.'}</div>`) +
      (other ? `<div class="bt-note">${other} song${other === 1 ? '' : 's'} in other formats (M4A, FLAC, MIDI…) can only play in the browser.</div>` : '');
    box.querySelectorAll('.bt-song').forEach(b => b.onclick = () => play(+b.dataset.i, false));
  }
  async function play(index, shuffle) {
    const list = shown(); if (!list.length) return;
    await post('/api/bt/play', { paths: list.map(pathOf), index, shuffle });
    refresh(true);
  }

  function render(b) {
    const content = el('#content');
    if (!content.querySelector('.bt')) {
      content.innerHTML = `<div class="bt coms">
        <div class="coms-card" id="btHead"></div>
        <div class="coms-card" id="btNow"></div>
        <div class="coms-card"><div class="bt-row"><b>Music</b><span class="bt-grow"></span>
          <button class="tool-action-btn tool-action-btn-secondary" id="btAll">Play all</button><button class="tool-action-btn tool-action-btn-secondary" id="btShuf">Shuffle all</button></div>
          <input type="search" id="btFilter" class="bt-search" placeholder="Find a song" value="${escapeHtml(filter)}">
          <div id="btSongs" class="bt-songs"></div></div></div>`;
      content.querySelector('#btFilter').oninput = e => { filter = e.target.value; drawSongs(); };
      content.querySelector('#btAll').onclick = () => play(0, false);
      content.querySelector('#btShuf').onclick = () => play(0, true);
      drawSongs();
    }
    // headset
    const head = content.querySelector('#btHead');
    let h = `<div class="coms-top"><label class="toggle-switch"><input type="checkbox" id="btPower" ${b.on ? 'checked' : ''}><span class="toggle-slider"></span></label>
      <span class="power-label">${!b.on ? 'Bluetooth is off' : b.connected ? 'Connected to ' + escapeHtml(b.headset.name || 'your headset') : b.held ? 'Not connected' : b.target ? 'Connecting…' : b.saved.mac ? 'Looking for ' + escapeHtml(b.saved.name || 'your headset') + '…' : 'Looking for headsets…'}</span>
      <span class="sub">${b.on ? 'The board plays your music straight to a Bluetooth headset or speaker.' : 'Turn it on to play music from this board on a Bluetooth headset or speaker. The board restarts to switch it on or off.'}</span></div>`;
    if (b.on && b.started) {
      if (b.connected) h += `<div class="bt-row"><button class="tool-action-btn tool-action-btn-secondary" id="btDisc">Disconnect</button><button class="tool-action-btn tool-action-btn-secondary" id="btForget">Forget this headset</button></div>`;
      else {
        h += `<p class="bt-help">Put the headset in pairing mode (usually: hold its power button until the light flashes). It shows up here in a few seconds; tap it to connect.${b.saved.mac ? ' It reconnects to <b>' + escapeHtml(b.saved.name || b.saved.mac) + '</b> on its own when that one is on.' : ''}</p>`;
        h += (b.found.length ? `<div class="bt-found">${b.found.map(d => `<button class="bt-dev" data-mac="${d.mac}"><span>${escapeHtml(d.name)}</span><span class="bt-sig">${bars(d.rssi)}</span><span class="bt-act">${b.target === d.mac ? 'Connecting…' : 'Connect'}</span></button>`).join('')}</div>`
          : '<div class="bt-empty">Nothing found yet…</div>');
        h += `<div class="bt-row"><button class="tool-action-btn tool-action-btn-secondary" id="btScan">Search again</button>${b.saved.mac ? '<button class="tool-action-btn tool-action-btn-secondary" id="btForget">Forget ' + escapeHtml(b.saved.name || 'saved headset') + '</button>' : ''}</div>`;
      }
    }
    if (b.error) h += `<div class="bt-note">${escapeHtml(b.error)}</div>`;
    head.innerHTML = h;
    head.querySelector('#btPower').onchange = async e => {
      head.querySelector('.power-label').textContent = 'Restarting the board…';
      await post('/api/bt/power', { on: e.target.checked });
      setTimeout(() => location.reload(), 9000);
    };
    const on = (id, f) => { const x = head.querySelector(id); if (x) x.onclick = f; };
    on('#btDisc', async () => { await post('/api/bt/disconnect'); refresh(true); });
    on('#btForget', async () => { await post('/api/bt/forget'); refresh(true); });
    on('#btScan', async () => { await post('/api/bt/scan'); refresh(true); });
    head.querySelectorAll('.bt-dev').forEach(x => x.onclick = async () => { await post('/api/bt/connect', { mac: x.dataset.mac }); refresh(true); });
    // now playing
    const p = b.player, now = content.querySelector('#btNow');
    const track = p.track ? songName(p.track) : '';
    now.innerHTML = `<div class="bt-row"><b>${track ? escapeHtml(track) : 'Nothing playing'}</b><span class="bt-grow"></span><span class="bt-time">${track ? mmss(p.pos) + (p.dur ? ' / ' + mmss(p.dur) : '') : ''}</span></div>
      ${track ? `<div class="bt-sub">${p.index + 1} of ${p.count}${p.shuffle ? ' · shuffled' : ''}${b.connected ? '' : ' · waiting for the headset'}</div>` : `<div class="bt-sub">${b.connected ? 'Pick a song below.' : 'Connect a headset, then pick a song below.'}</div>`}
      <div class="bt-ctl"><button id="btPrev" title="Previous">⏮</button><button id="btPP" class="bt-big" title="Play/pause">${p.playing ? '❚❚' : '▶'}</button><button id="btNext" title="Next">⏭</button><button id="btStop" title="Stop">■</button>
      <span class="bt-vol">🔊 <input type="range" id="btVol" min="0" max="100" value="${p.volume}"></span></div>`;
    const ctl = c => async () => { await post('/api/bt/control', { cmd: c }); refresh(true); };
    now.querySelector('#btPrev').onclick = ctl('prev');
    now.querySelector('#btNext').onclick = ctl('next');
    now.querySelector('#btStop').onclick = ctl('stop');
    now.querySelector('#btPP').onclick = ctl(p.playing ? 'pause' : 'resume');
    let vt = 0; now.querySelector('#btVol').oninput = e => { clearTimeout(vt); const v = +e.target.value; vt = setTimeout(() => post('/api/bt/control', { volume: v }), 150); };
  }

  async function refresh(force) {
    if (!viewing()) { clearInterval(timer); timer = null; return; }
    let b; try { b = await fetch('/api/bt').then(r => r.json()); } catch { return; }
    const sig = JSON.stringify([b, Math.round(b.player.pos)]);
    if (!force && sig === lastSig) return;
    lastSig = sig;
    if (document.activeElement && document.activeElement.id === 'btVol') return;   // don't move the slider under a finger
    render(b);
  }

  window.loadBtView = function () {
    setToolHeader('Bluetooth', 'Play music from this board on a Bluetooth headset', null, null);
    el('#content').innerHTML = '';
    lastSig = ''; songs = null;
    refresh(true); loadSongs();
    clearInterval(timer);
    timer = setInterval(() => refresh(false), 1500);
  };
})();

// ---- iPhone/iPad: visualizer bars that start quickly, and lock-screen controls that stay ----
// The Shelf draws the bars on iOS from the song itself: it downloads the whole file a second
// time and decodes it. From the board that takes a while, and when it's too slow The Shelf
// falls back to a hidden second <audio> routed through Web Audio, which is what makes iOS lose
// the lock-screen play/pause/next controls. The hidden copy is never used here.
//
// Instead an MP3 is downloaded in pieces, and what has arrived so far is decoded from the
// start of the song each time: first ~4 seconds' worth, then three times as much, and so on,
// so the bars start within seconds and keep ahead of the music. Safari is fussy: a piece cut
// from the middle of an MP3 usually won't decode at all (most frames borrow bytes from the
// frame before), which is why bars used to stop after the first few seconds. Decoding from
// the start, cut on a whole frame, always works. The ID3 tag (cover art can make it big) is
// skipped. Other formats are fetched whole and decoded once.
(function () {
  if (typeof IS_IOS === 'undefined' || !IS_IOS || typeof preparePcmViz !== 'function') return;
  try { ensureShadowVisualizer = function () {}; } catch (e) {}           // never the hidden second player
  const NET = 384 * 1024, FIRST = 96 * 1024, GROW = 3, WHOLE_MAX = 40 * 1048576;
  const isMidiUrl = u => /\.(mid|midi|kar|rmi)$/i.test(String(u).split('?')[0]);
  const isMp3Url = u => /\.mp3$/i.test(decodeURIComponent(String(u).split('?')[0]));
  const BR = { 1: [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320],   // MPEG-1 Layer III
               2: [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160] };      // MPEG-2/2.5 Layer III
  const SR = { 3: [44100, 48000, 32000], 2: [22050, 24000, 16000], 0: [11025, 12000, 8000] };
  function frameLen(b, i) {                                                  // an MP3 frame header at b[i]: its length, or 0
    if (i + 4 > b.length || b[i] !== 0xFF || (b[i + 1] & 0xE0) !== 0xE0) return 0;
    const ver = (b[i + 1] >> 3) & 3, layer = (b[i + 1] >> 1) & 3, bri = b[i + 2] >> 4, sri = (b[i + 2] >> 2) & 3;
    if (ver === 1 || layer !== 1 || bri === 0 || bri === 15 || sri === 3) return 0;     // Layer III only
    const kbps = BR[ver === 3 ? 1 : 2][bri], sr = SR[ver][sri], pad = (b[i + 2] >> 1) & 1;
    return ver === 3 ? Math.floor(144000 * kbps / sr) + pad : Math.floor(72000 * kbps / sr) + pad;
  }
  // first frame: a header whose next header is where it says
  function firstFrame(b, from) {
    for (let i = from; i + 4 <= b.length; i++) { const n = frameLen(b, i); if (n >= 24 && frameLen(b, i + n)) return i; }
    return -1;
  }
  function mono(dec) {
    const m = new Float32Array(dec.length);
    for (let c = 0; c < dec.numberOfChannels; c++) { const ch = dec.getChannelData(c); for (let i = 0; i < m.length; i++) m[i] += ch[i] / dec.numberOfChannels; }
    return m;
  }

  preparePcmViz = async function () {
    const src = audio.src;
    if (!src || pcmViz.src === src) return;
    const st = { src, data: null, rate: PCM_VIZ_RATE, ready: false, fallback: false };
    pcmViz = st; pcmVizSmoothed.fill(0);
    if (isMidiUrl(src)) return;                                              // MIDI: bars come from the synth (midi.js)
    const Offline = window.OfflineAudioContext || window.webkitOfflineAudioContext;
    const live = () => pcmViz === st;
    const decode = async bytes => mono(await decodeToBuffer(new Offline(1, 1, PCM_VIZ_RATE), bytes));
    try {
      if (!isMp3Url(src)) {                                                  // whole file, one decode
        const r = await fetch(src);
        if (!r.ok || (+r.headers.get('Content-Length') || 0) > WHOLE_MAX) return;
        const bytes = await r.arrayBuffer();
        if (!live()) return;
        st.data = await decode(bytes);
        if (live()) st.ready = true;
        return;
      }
      let buf = new Uint8Array(1 << 20), have = 0, pos = 0, total = Infinity;
      let start = -1, walked = -1, frames = 0, next = FIRST, failed = 0;
      while (true) {
        if (!live()) return;
        if (pos < total) {
          const r = await fetch(src, { headers: { Range: 'bytes=' + pos + '-' + (pos + NET - 1) } });
          if (r.status !== 206 && r.status !== 200) throw new Error('fetch ' + r.status);
          const piece = new Uint8Array(await r.arrayBuffer());
          if (r.status === 200) { total = piece.length; pos = 0; have = 0; }
          else { const cr = r.headers.get('Content-Range'); total = cr ? (+cr.split('/')[1] || Infinity) : Infinity; }
          if (!piece.length) total = pos;
          if (have + piece.length > buf.length) { const nb = new Uint8Array(Math.max(have + piece.length, buf.length * 2)); nb.set(buf.subarray(0, have)); buf = nb; }
          buf.set(piece, have); have += piece.length; pos += piece.length;
        }
        const last = pos >= total;
        if (start < 0) {                                                     // skip the ID3 tag, find the first frame
          let skip = 0;
          if (have >= 10 && buf[0] === 0x49 && buf[1] === 0x44 && buf[2] === 0x33)
            skip = 10 + ((buf[6] & 127) << 21 | (buf[7] & 127) << 14 | (buf[8] & 127) << 7 | (buf[9] & 127)) + (buf[5] & 0x10 ? 10 : 0);
          if (have < skip + 4096 && !last) continue;
          start = firstFrame(buf.subarray(0, have), skip);
          if (start < 0) { if (last) return; start = -1; continue; }
          walked = start;
        }
        // walk whole frames as far as they've arrived
        while (true) { const n = frameLen(buf, walked); if (!n || walked + n > have) break; walked += n; frames++; }
        const ready = walked - start;
        if (ready < next && !last) continue;
        try {
          const m = await decode(buf.slice(start, walked).buffer);            // from the start of the song, every time
          if (!live()) return;
          st.data = m; st.rate = PCM_VIZ_RATE; st.ready = true;
          next = Math.max(next * GROW, ready + FIRST);
        } catch (e) { if (++failed > 2) return; next = ready + FIRST; }       // try again with a bit more
        if (last) {
          // the whole song is in: one last pass if frames ended early (a glitch or a tag mid-file)
          if (walked < have - 256 && !failed) { const more = firstFrame(buf.subarray(0, have), walked); if (more > 0) { st.data = await decode(buf.slice(start, have).buffer).catch(() => st.data); } }
          return;
        }
      }
    } catch (e) { /* no bars for this song; playback and the lock screen are unaffected */ }
  };
})();
