// The Shelf for Heltec V4: small touches on top of The Shelf's own page.
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

// ---- 900MHz Coms: a simple LoRa text mesh between Shelf boards ----
(function () {
  let timer = null, last = '', drafting = false;
  const LEVELS = ['Low (~11 dBm)', 'Normal (~20 dBm)', 'High (~27 dBm)'];
  const ago = t => { const s = Math.max(0, (Date.now() - t) / 1000); return s < 60 ? 'just now' : s < 3600 ? Math.floor(s / 60) + ' min ago' : s < 86400 ? Math.floor(s / 3600) + ' h ago' : new Date(t).toLocaleDateString(); };
  const time = t => new Date(t).toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' });
  const post = (u, d) => fetch(u, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(d || {}) });

  function viewing() { return typeof state !== 'undefined' && state.view === 'coms'; }

  async function refresh(force) {
    if (!viewing()) { clearInterval(timer); timer = null; return; }
    let c; try { c = await fetch('/api/coms').then(r => r.json()); } catch { return; }
    const sig = JSON.stringify([c.enabled, c.radio, c.receipts, c.log.length, c.log.length && c.log[c.log.length - 1].id, c.neighbors.length, c.error, c.heard, c.relayed, c.synced, c.receivedBy]);
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
          <div class="coms-neigh">${neigh.length ? 'In range: ' + neigh.map(n => `<b style="color:var(--text)">${escapeHtml(n.name || n.node)}</b> (${n.rssi} dBm, ${ago(n.at)})`).join(', ') : 'No other boards heard yet. They show up here as soon as one transmits.'}</div>
          <details style="margin-top:10px"><summary style="cursor:pointer;color:var(--text-muted);font-size:13px">Settings</summary>
            <div class="coms-settings">
              <label>Your name on the mesh<input id="comsName" maxlength="16" value="${escapeHtml(c.name)}"></label>
              <label>Transmit power<select id="comsLevel">${LEVELS.map((l, i) => `<option value="${i}" ${i === c.level ? 'selected' : ''}>${l}</option>`).join('')}</select></label>
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
            <b>How it works:</b> every Heltec V4 running The Shelf has a 900 MHz LoRa radio. With Coms on, boards in range hear each other directly, and each board re-sends what it hears, so messages hop from board to board as far as the mesh reaches. A board that joins late is caught up on the last 12 hours by its neighbors. No internet or Wi-Fi needed between boards.<br><br>
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
    const rc = content.querySelector('#comsReceipts');
    if (rc) rc.addEventListener('click', async () => { await post('/api/coms/settings', { receipts: !c.receipts }); last = ''; refresh(true); });
    const save = content.querySelector('#comsSave');
    if (save) save.addEventListener('click', async () => { await post('/api/coms/settings', { name: content.querySelector('#comsName').value, level: Number(content.querySelector('#comsLevel').value) }); last = ''; refresh(true); });
    const clear = content.querySelector('#comsClear');
    if (clear) clear.addEventListener('click', async () => { if (confirm('Clear the message log on this board?')) { await post('/api/coms/clear'); last = ''; refresh(true); } });
  }

  window.loadComsView = function () {
    setToolHeader('900MHz Coms', 'A simple radio mesh between Shelf boards', null, null);
    last = '';
    refresh(true);
    clearInterval(timer);
    timer = setInterval(() => { if (document.activeElement && document.activeElement.id === 'comsName') return; refresh(false); }, 2000);
  };
})();
