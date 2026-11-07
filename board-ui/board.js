// The Shelf for Heltec V4: small touches on top of The Shelf's own page.
(function () {
  async function boardInfo() {
    try { return await fetch('/api/board/info').then(r => r.json()); } catch { return null; }
  }
  function fmt(b) { return b >= 1e9 ? (b / 1e9).toFixed(1) + ' GB' : b >= 1e6 ? (b / 1e6).toFixed(1) + ' MB' : Math.round(b / 1e3) + ' KB'; }
  async function decorate() {
    const info = await boardInfo();
    const p = document.querySelector('.brand-text p');
    if (p) p.textContent = 'running on a Heltec V4';
    const net = document.getElementById('networkUrl');
    if (net && info) net.textContent = info.host + '.local';
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
