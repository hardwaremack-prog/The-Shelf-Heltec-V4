// The Shelf for Heltec V4 - update the firmware over Wi-Fi.
//
//   GET  /update                     the update page. It's built into the firmware, not the web
//                                    app, so it still works if a web-app update gets cut off.
//   GET  /api/board/update/info      version, which firmware this is, and where things live
//   POST /api/board/update/app       raw body = the new app (ShelfV4.ino.bin): written to the
//                                    spare app slot, which becomes the one that starts next
//   POST /api/board/update/www       raw body = the new web app image (the "www" partition)
//   POST /api/board/update/restart
//
// The page downloads both pieces from the installer site (or slices them out of an installer
// .bin you pick) and sends them here. Boards installed before 1.9.0 have one app slot, so the
// first update to 1.9.0 has to go over USB from the installer page.
#include "shelf.h"
#include <Update.h>
#include <LittleFS.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#if defined(SHELF_V3)
  #define FW_ID "shelf-v3"
#elif CONFIG_SPIRAM_MODE_OCT
  #define FW_ID "shelf-v4-r8"
#else
  #define FW_ID "shelf-v4"
#endif

volatile int otaPercent = -1;        // shown on the board's screen while an update is written
volatile char otaWhat = 0;           // 'a' app, 'w' web app

static const uint32_t OTA_MAGIC = 0x0E7A0E7A;
struct OtaReq { uint32_t magic; bool ok; char err[120]; };
static AsyncWebServerRequest* otaOwner = nullptr;

bool isOtaUpload(const String& url) { return url == "/api/board/update/app" || url == "/api/board/update/www"; }

static const esp_partition_t* wwwPart() { return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "www"); }

static void fail(OtaReq* q, const String& why) {
  if (!q->err[0]) strlcpy(q->err, why.c_str(), sizeof q->err);
  if (Update.isRunning()) Update.abort();
  otaOwner = nullptr; otaPercent = -1;
}

void otaBody(AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total) {
  OtaReq* q = (OtaReq*)r->_tempObject;
  if (index == 0) {
    if (q) return;
    q = (OtaReq*)calloc(1, sizeof(OtaReq));
    if (!q) return;
    q->magic = OTA_MAGIC; r->_tempObject = q;
    if (!canWrite(r)) { fail(q, "The Shelf is in read-only mode. Unlock write mode first."); return; }
    if (r->client()) r->client()->setRxTimeout(30);
    if (Update.isRunning()) Update.abort();                      // an earlier update that was cut off
    bool www = r->url().endsWith("/www");
    if (www) {
      const esp_partition_t* p = wwwPart();
      if (!p) { fail(q, "This board has no web app area. Install from the installer page over USB."); return; }
      if (total > p->size) { fail(q, "That web app image is too big for this board."); return; }
      LittleFS.end();                                            // it's about to be rewritten
      if (!Update.begin(total, U_SPIFFS)) { fail(q, String("Couldn't start: ") + Update.errorString()); return; }
    } else {
      const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
      if (!next || next == esp_ota_get_running_partition()) {
        fail(q, "This board has one firmware slot. Install 1.9.0 or later once over USB from the installer page; after that it updates over Wi-Fi.");
        return;
      }
      if (total > next->size) { fail(q, "That firmware is too big for this board. Is it the right board's file?"); return; }
      if (!Update.begin(total, U_FLASH)) { fail(q, String("Couldn't start: ") + Update.errorString()); return; }
    }
    otaOwner = r; otaWhat = www ? 'w' : 'a'; otaPercent = 0;
    screenWake();
  }
  if (!q || q->err[0] || otaOwner != r) return;
  if (Update.write(data, len) != len) { fail(q, String("Writing failed: ") + Update.errorString()); return; }
  otaPercent = total ? (int)((index + len) * 100 / total) : 100;
  if (index + len >= total) {
    if (!Update.end(true)) { fail(q, String("The new ") + (otaWhat == 'w' ? "web app" : "firmware") + " didn't check out: " + Update.errorString()); return; }
    q->ok = true; otaOwner = nullptr; otaPercent = -1;
  }
}

static const char UPDATE_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Update The Shelf</title>
<style>
:root{--ink:#241B2F;--surface:#322542;--text:#F0E9E3;--muted:#B7ABBA;--accent:#F2B441;--accent-ink:#2A1D0E;--divider:#4A3B5C;--bad:#E2725B;--good:#7FD49B;color-scheme:dark}
*{box-sizing:border-box}body{margin:0;background:var(--ink);color:var(--text);font:16px/1.5 -apple-system,BlinkMacSystemFont,'Segoe UI',system-ui,sans-serif}
.wrap{max-width:620px;margin:0 auto;padding:28px 16px 60px}h1{font-family:Georgia,serif;font-size:30px;margin:0 0 4px}
a{color:var(--accent)}.muted{color:var(--muted);font-size:14px}
.card{background:var(--surface);border:1px solid var(--divider);border-radius:16px;padding:18px;margin-top:16px}
.row{display:flex;justify-content:space-between;gap:12px;padding:4px 0}.row span:first-child{color:var(--muted)}
button{font:inherit;font-weight:700;border:0;border-radius:999px;padding:11px 20px;cursor:pointer;background:var(--accent);color:var(--accent-ink);margin:10px 8px 0 0}
button.sec{background:transparent;color:var(--text);border:1px solid var(--divider)}button:disabled{opacity:.45;cursor:default}
.bar{height:8px;border-radius:999px;background:var(--divider);overflow:hidden;margin:12px 0 6px}.bar div{height:100%;width:0;background:var(--accent);transition:width .2s}
#msg.bad{color:var(--bad)}#msg.good{color:var(--good)}
</style></head><body><div class="wrap">
<p class="muted"><a href="/board/">&larr; Settings</a> &middot; <a href="/">The Shelf</a></p>
<h1>Update The Shelf</h1>
<p class="muted">Puts new firmware on this board over Wi-Fi. Your files, settings, Wi-Fi and admin password stay as they are.</p>
<div class="card">
  <div class="row"><span>This board</span><span id="board">&hellip;</span></div>
  <div class="row"><span>Installed</span><span id="fw">&hellip;</span></div>
  <div class="row"><span>Newest</span><span id="latest">&mdash;</span></div>
  <button id="check">Check for update</button><button id="install" hidden></button>
  <p class="muted" style="margin:12px 0 0">Checking needs the internet. On the board's own Wi-Fi, use a file instead.</p>
</div>
<div class="card">
  <b>Update from a file</b>
  <p class="muted" style="margin:6px 0 0">Pick this board's <code>.bin</code> from the installer page (the same file the USB installer uses).</p>
  <button class="sec" id="pick">Choose file&hellip;</button><input type="file" id="file" accept=".bin,.gz" hidden>
</div>
<div class="card" id="prog" hidden><div id="msg"></div><div class="bar"><div id="fill"></div></div><div class="muted" id="sub"></div></div>
</div>
<script>
const BASE='https://hardwaremack-prog.github.io/The-Shelf-Heltec-V4900mhzMesh/';
const $=id=>document.getElementById(id);let info=null,latest=null,busy=false;
const newer=(a,b)=>{a=a.split('.').map(Number);b=b.split('.').map(Number);for(let i=0;i<3;i++){if((a[i]||0)!==(b[i]||0))return (a[i]||0)>(b[i]||0)}return false};
function say(t,cls,frac,sub){$('prog').hidden=false;$('msg').textContent=t;$('msg').className=cls||'';if(frac!=null)$('fill').style.width=Math.round(frac*100)+'%';$('sub').textContent=sub||''}
function lock(on){busy=on;for(const b of document.querySelectorAll('button'))b.disabled=on}
async function loadInfo(){info=await fetch('/api/board/update/info',{cache:'no-store'}).then(r=>r.json());$('board').textContent=info.board+(info.id.endsWith('r8')?' (R8)':'');$('fw').textContent='v'+info.fw;
  if(!info.slots)say('This board was installed with an older layout, so this first update has to go over USB. Use the installer page once with this board plugged in; after that, updates work here.','bad')}
async function gunzip(buf){const b=new Uint8Array(buf);if(b[0]!==0x1f||b[1]!==0x8b)return b;return new Uint8Array(await new Response(new Blob([b]).stream().pipeThrough(new DecompressionStream('gzip'))).arrayBuffer())}
async function download(url,label,lo,hi){const r=await fetch(url,{cache:'no-store'});if(!r.ok)throw new Error("Couldn't download the "+label+' ('+r.status+')');
  const total=+r.headers.get('Content-Length')||0,rd=r.body.getReader(),parts=[];let got=0;
  for(;;){const {done,value}=await rd.read();if(done)break;parts.push(value);got+=value.length;if(total)say('Downloading the '+label+'…','',lo+(hi-lo)*got/total)}
  return gunzip(await new Blob(parts).arrayBuffer())}
function send(part,data,lo,hi,label){return new Promise((ok,no)=>{const x=new XMLHttpRequest();x.open('POST','/api/board/update/'+part);
  x.upload.onprogress=e=>{if(e.lengthComputable)say('Sending the '+label+' to the board…','',lo+(hi-lo)*e.loaded/e.total,Math.round(e.loaded/1024)+' of '+Math.round(e.total/1024)+' KB')};
  x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(e){}x.status===200&&j.ok?ok():no(new Error(j.error||('The board said '+x.status)))};
  x.onerror=()=>no(new Error('The connection to the board dropped. Try again; the board is still running its old firmware.'));
  x.setRequestHeader('Content-Type','application/octet-stream');x.send(new Blob([data]))})}
async function flash(app,www,ver){
  if(app[0]!==0xE9)throw new Error("That isn't firmware for this board.");
  if(app.length>info.appMax)throw new Error('That firmware is too big for this board. Is it for a '+info.board+'?');
  await send('app',app,.35,.75,'firmware');
  if(www)await send('www',www,.75,.98,'web app');
  say('Restarting the board…','',.99);await fetch('/api/board/update/restart',{method:'POST'}).catch(()=>{});
  const t0=Date.now();await new Promise(r=>setTimeout(r,6000));
  while(Date.now()-t0<120000){try{const i=await fetch('/api/board/update/info',{cache:'no-store'}).then(r=>r.json());
    say('Updated to v'+i.fw+'. Opening The Shelf…','good',1);setTimeout(()=>location.href='/',2500);return}catch(e){await new Promise(r=>setTimeout(r,2000))}}
  say('The board is restarting. If this page doesn’t come back, open The Shelf again in a minute.','',1)}
$('check').onclick=async()=>{if(busy)return;lock(true);try{say('Checking…','',0);
  latest=await fetch(BASE+'ota/'+info.id+'.json',{cache:'no-store'}).then(r=>{if(!r.ok)throw new Error('No update found for this board ('+r.status+')');return r.json()});
  $('latest').textContent='v'+latest.version;const up=newer(latest.version,info.fw);
  $('install').hidden=false;$('install').textContent=up?'Install v'+latest.version:'Reinstall v'+latest.version;
  say(up?'v'+latest.version+' is ready to install.':'This board is up to date.',up?'good':'',0)}
  catch(e){say(/Failed to fetch|NetworkError|Load failed/i.test(e.message)?"Couldn't reach the internet from this browser. On the board's own Wi-Fi, use Update from a file.":e.message,'bad')}
  finally{lock(false)}};
$('install').onclick=async()=>{if(busy||!latest)return;lock(true);try{
  const app=await download(BASE+'ota/'+latest.app,'firmware',0,.2),www=await download(BASE+'ota/'+latest.www,'web app',.2,.35);
  if(latest.appSize&&app.length!==latest.appSize)throw new Error('The firmware download was incomplete. Try again.');
  if(latest.wwwSize&&www.length!==latest.wwwSize)throw new Error('The web app download was incomplete. Try again.');
  await flash(app,www,latest.version)}catch(e){say(e.message,'bad');lock(false)}};
$('pick').onclick=()=>$('file').click();
$('file').onchange=async()=>{const f=$('file').files[0];$('file').value='';if(!f||busy)return;lock(true);try{
  say('Reading '+f.name+'…','',.05);const b=await gunzip(await f.arrayBuffer());let app,www=null;
  if(b[0]===0xE9&&b.length<=info.appMax){app=b}
  else{if(b.length<info.wwwOffset+4096)throw new Error("That file isn't a full installer image for this board.");
    let end=info.wwwOffset;while(end>info.appOffset&&b[end-1]===0xFF)end--;end=Math.min(info.wwwOffset,Math.ceil(end/4096)*4096);
    app=b.slice(info.appOffset,end);www=b.slice(info.wwwOffset,info.wwwOffset+info.wwwSize);
    if(www.length!==info.wwwSize)throw new Error("That file's web app part is cut short.")}
  if(!confirm('Install '+f.name+' on this board?'))throw new Error('Cancelled.');
  await flash(app,www)}catch(e){say(e.message,'bad');lock(false)}};
loadInfo().catch(()=>say("Couldn't read the board's details.",'bad'));
</script></body></html>)HTML";

bool handleOtaApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m) {
  if (url == "/update") {
    AsyncWebServerResponse* res = r->beginResponse_P(200, "text/html; charset=utf-8", (const uint8_t*)UPDATE_PAGE, strlen_P(UPDATE_PAGE));
    res->addHeader("Cache-Control", "no-cache");
    r->send(res);
    return true;
  }
  if (!url.startsWith("/api/board/update/")) return false;
  if (url == "/api/board/update/info") {
    const esp_partition_t* run = esp_ota_get_running_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    const esp_partition_t* app0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    const esp_partition_t* w = wwwPart();
    bool slots = next && next != run;
    sendJson(r, 200, String("{\"fw\":\"" FW_VERSION "\",\"id\":\"" FW_ID "\",\"board\":\"" BOARD_NAME "\",\"slots\":") + (slots ? "true" : "false") +
                     ",\"running\":\"" + (run ? run->label : "?") + "\",\"appOffset\":" + (app0 ? app0->address : 0x10000) +
                     ",\"appMax\":" + (slots ? next->size : (run ? run->size : 0)) +
                     ",\"wwwOffset\":" + (w ? w->address : 0) + ",\"wwwSize\":" + (w ? w->size : 0) + "}");
    return true;
  }
  if (m != HTTP_POST) { sendErr(r, 405, "Use POST"); return true; }
  if (url == "/api/board/update/restart") { sendJson(r, 200, "{\"ok\":true}"); rebootSoon = true; return true; }
  if (isOtaUpload(url)) {
    OtaReq* q = (OtaReq*)r->_tempObject;
    if (!q || q->magic != OTA_MAGIC) { sendErr(r, 400, "Nothing was sent."); return true; }
    if (!q->ok) { sendErr(r, 500, q->err[0] ? String(q->err) : String("The update was cut off. Try again.")); return true; }
    sendJson(r, 200, "{\"ok\":true}");
    return true;
  }
  sendErr(r, 404, "Not found");
  return true;
}
