// The Shelf for Heltec V4 - Messages: finds every other Shelf on the network and chats
// with it, speaking the same protocol as The Shelf's server.js:
//   - "shelf-presence" UDP broadcasts on port 41234 every 3 seconds
//   - POST /api/messaging/hello  ("are you a Shelf?"), answered with "shelf-hello"
//   - POST /api/messaging/receive to deliver a message
#include "shelf.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <lwip/sockets.h>
#include <algorithm>

static const uint16_t DISCOVERY_PORT = 41234;
static const uint16_t MY_PORT = 80;            // also listening on 8080, where Shelves look
static const uint32_t PEER_TIMEOUT_MS = 10000;
static const char* MSG_DIR = DATA_ROOT "/messaging";
static const char* CONV_DIR = DATA_ROOT "/messaging/c";
static const char* KNOWN_FILE = DATA_ROOT "/messaging/known-peers.json";

struct Identity { String id, name, status; } me;
static String instanceTag;
static Preferences mprefs;

struct Peer {
  String id, name, status, ip, via;
  uint16_t port = 8080;
  uint32_t seenMs = 0;        // millis() when last heard - drives "online"
  uint64_t lastSeenAt = 0;    // wall clock, remembered across restarts
  bool live = false;          // heard from since boot
};
static std::vector<Peer> peers;
static SemaphoreHandle_t peersLock;
static bool knownDirty = false;
static uint32_t knownSavedAt = 0;

struct ScanState { uint64_t at = 0; int found = 0; bool scanning = false; int addresses = 0; String range; } lastScan;
static volatile bool scanRequested = false;

// The Shelf sends its port as a number, or as text when it was started with PORT=...
static uint16_t portOf(JsonVariantConst v, uint16_t dflt) {
  if (v.is<int>()) return v.as<int>() > 0 ? v.as<int>() : dflt;
  if (v.is<const char*>()) { int p = atoi(v.as<const char*>()); return p > 0 && p < 65536 ? p : dflt; }
  return dflt;
}

struct PLock { PLock() { xSemaphoreTake(peersLock, portMAX_DELAY); } ~PLock() { xSemaphoreGive(peersLock); } };

static bool online(const Peer& p) { return p.live && millis() - p.seenMs < PEER_TIMEOUT_MS; }

// ------------------------------------------------------------ identity
static void saveIdentity() { mprefs.putString("id", me.id); mprefs.putString("name", me.name); mprefs.putString("status", me.status); }

static String presenceJson() {
  return "{\"type\":\"shelf-presence\",\"id\":\"" + me.id + "\",\"name\":\"" + jsonEsc(me.name) + "\",\"status\":\"" + me.status +
         "\",\"httpPort\":" + MY_PORT + ",\"instance\":\"" + instanceTag + "\"}";
}

static String identityJson() {
  return "{\"id\":\"" + me.id + "\",\"name\":\"" + jsonEsc(me.name) + "\",\"status\":\"" + me.status + "\"}";
}

// ------------------------------------------------------------ peers
static void loadKnown() {
  JsonDocument d(&psAlloc);
  if (!loadJsonFile(KNOWN_FILE, d)) return;
  PLock l;
  for (JsonPair kv : d.as<JsonObject>()) {
    Peer p; p.id = kv.key().c_str();
    if (!isValidId(p.id) || p.id == me.id) continue;
    p.name = kv.value()["name"] | "Unknown"; p.ip = kv.value()["ip"] | "";
    p.port = kv.value()["httpPort"] | 8080; p.lastSeenAt = kv.value()["lastSeenAt"] | 0ULL;
    p.status = "online";
    peers.push_back(p);
  }
}

static void saveKnownIfDue(bool force = false) {
  if (!knownDirty || (!force && millis() - knownSavedAt < 60000)) return;
  JsonDocument d(&psAlloc);
  {
    PLock l;
    for (auto& p : peers) {
      JsonObject o = d[p.id].to<JsonObject>();
      o["name"] = p.name; if (p.ip.length()) o["ip"] = p.ip; o["httpPort"] = p.port; o["lastSeenAt"] = p.lastSeenAt;
    }
    knownDirty = false;
  }
  mkdirs(MSG_DIR);
  saveJsonFile(KNOWN_FILE, d);
  knownSavedAt = millis();
}

// Records a Shelf we heard from (broadcast, hello, scan, message).
static bool notePeer(JsonVariantConst info, const String& ip, const char* via) {
  String id = info["id"] | "";
  if (!isValidId(id) || id == me.id) return false;
  if (String(info["instance"] | "") == instanceTag) return false;        // our own echo
  String name = info["name"] | "Unknown"; name = name.substring(0, 60);
  String status = info["status"] | "online";
  if (status != "online" && status != "away" && status != "busy") status = "online";
  uint16_t port = portOf(info["httpPort"], 8080);
  PLock l;
  Peer* p = nullptr;
  for (auto& x : peers) if (x.id == id) { p = &x; break; }
  if (!p) {
    // one Shelf per address: an older identity at the same address is that computer before it changed
    peers.erase(std::remove_if(peers.begin(), peers.end(), [&](const Peer& o) {
      return o.ip == ip && o.port == port && !store->exists(String(CONV_DIR) + "/" + o.id); }), peers.end());
    if (peers.size() >= 64) {   // keep the list bounded: drop the oldest one
      auto it = std::min_element(peers.begin(), peers.end(), [](const Peer& a, const Peer& b) { return a.lastSeenAt < b.lastSeenAt; });
      peers.erase(it);
    }
    peers.push_back(Peer()); p = &peers.back(); p->id = id; knownDirty = true;
  }
  if (p->name != name || p->ip != ip || p->port != port) knownDirty = true;
  p->name = name; p->status = status; p->ip = ip; p->port = port; p->via = via;
  p->seenMs = millis(); p->live = true; p->lastSeenAt = nowMs();
  return true;
}

static String peerJson(const Peer& p) {
  return "{\"id\":\"" + p.id + "\",\"name\":\"" + jsonEsc(p.name) + "\",\"status\":\"" + p.status + "\",\"ip\":\"" + p.ip +
         "\",\"httpPort\":" + p.port + ",\"lastSeen\":" + String((double)p.lastSeenAt, 0) + ",\"via\":\"" + p.via + "\"}";
}

// ------------------------------------------------------------ talking to other Shelves
// Asks one address "are you a Shelf?". Fills `out` with its answer.
static bool probeShelf(const String& ip, uint16_t port, uint32_t timeoutMs, JsonDocument& out, int* httpCode = nullptr) {
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(timeoutMs);
  http.setTimeout(timeoutMs);
  if (!http.begin(client, ip, port, "/api/messaging/hello")) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(presenceJson());
  if (httpCode) *httpCode = code;
  bool ok = false;
  if (code == 200) {
    DeserializationError e = deserializeJson(out, http.getString());
    ok = !e && String(out["type"] | "") == "shelf-hello";
    if (ok) out["httpPort"] = portOf(out["httpPort"], port);
  } else if (code == 404) {
    // an older Shelf without "hello" still answers the identity request
    http.end();
    if (http.begin(client, ip, port, "/api/messaging/identity") && http.GET() == 200) {
      ok = !deserializeJson(out, http.getString()) && out["id"].is<const char*>() && String(out["status"] | "") != "offline";
      if (ok) out["httpPort"] = port;
    }
  }
  http.end();
  return ok;
}

// Quick TCP check of many addresses at once, so a whole /24 scans in a few seconds.
static void findOpenPorts(const std::vector<uint32_t>& ips, uint16_t port, std::vector<uint32_t>& open) {
  const int BATCH = 12;
  for (size_t i = 0; i < ips.size(); i += BATCH) {
    int socks[BATCH]; size_t n = std::min((size_t)BATCH, ips.size() - i);
    fd_set wfds; FD_ZERO(&wfds); int maxfd = -1;
    for (size_t k = 0; k < n; k++) {
      socks[k] = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (socks[k] < 0) continue;
      int fl = lwip_fcntl(socks[k], F_GETFL, 0); lwip_fcntl(socks[k], F_SETFL, fl | O_NONBLOCK);
      struct sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = ips[i + k];
      lwip_connect(socks[k], (struct sockaddr*)&a, sizeof a);
      FD_SET(socks[k], &wfds); maxfd = std::max(maxfd, socks[k]);
    }
    uint32_t t0 = millis();
    while (maxfd >= 0 && millis() - t0 < 450) {
      fd_set w = wfds; struct timeval tv = {0, 50000};
      if (lwip_select(maxfd + 1, nullptr, &w, nullptr, &tv) > 0) {
        for (size_t k = 0; k < n; k++) {
          if (socks[k] >= 0 && FD_ISSET(socks[k], &w)) {
            int err = 0; socklen_t len = sizeof err;
            lwip_getsockopt(socks[k], SOL_SOCKET, SO_ERROR, &err, &len);
            if (!err) open.push_back(ips[i + k]);
            FD_CLR(socks[k], &wfds); lwip_close(socks[k]); socks[k] = -1;
          }
        }
      }
    }
    for (size_t k = 0; k < n; k++) if (socks[k] >= 0) lwip_close(socks[k]);
    delay(1);
  }
}

static void scanNetwork() {
  if (!staOK || me.status == "offline") return;
  lastScan.scanning = true;
  IPAddress self = WiFi.localIP(), mask = WiFi.subnetMask();
  uint32_t me32 = (uint32_t)self;           // network byte order
  uint32_t m32 = (uint32_t)mask;
  if (ntohl(m32) < 0xFFFFFF00u) m32 = htonl(0xFFFFFF00u);   // big networks: the 254 around us
  uint32_t base = ntohl(me32 & m32), count = ~ntohl(m32);
  std::vector<uint32_t> ips;
  for (uint32_t i = 1; i < count && ips.size() < 1024; i++) { uint32_t ip = htonl(base + i); if (ip != me32) ips.push_back(ip); }
  lastScan.range = IPAddress(htonl(base + 1)).toString() + "\xE2\x80\x93" + IPAddress(htonl(base + count - 1)).toString();
  std::vector<uint32_t> open;
  findOpenPorts(ips, 8080, open);
  int found = 0;
  for (uint32_t ip : open) {
    JsonDocument d(&psAlloc);
    String s = IPAddress(ip).toString();
    if (probeShelf(s, 8080, 1500, d) && notePeer(d, s, "scan")) found++;
  }
  lastScan.at = nowMs(); lastScan.found = found; lastScan.scanning = false; lastScan.addresses = (int)ips.size();
}

// Direct check-in with every Shelf seen in the last week, so status stays right
// even where broadcasts don't get through.
static void checkInWithKnown() {
  std::vector<std::pair<String, uint16_t>> targets;
  {
    PLock l;
    uint64_t weekAgo = nowMs() - 7ULL * 24 * 3600 * 1000;
    for (auto& p : peers) if (p.ip.length() && (p.lastSeenAt > weekAgo || nowMs() < 1700000000000ULL)) targets.push_back({p.ip, p.port});
  }
  if (targets.size() > 20) targets.resize(20);
  for (auto& t : targets) {
    JsonDocument d(&psAlloc);
    if (probeShelf(t.first, t.second, 1200, d)) notePeer(d, t.first, "direct");
  }
}

static WiFiUDP udp;
static bool udpUp = false;

static void broadcastPresence() {
  if (me.status == "offline") return;
  String p = presenceJson();
  IPAddress targets[2] = {IPAddress(255, 255, 255, 255), WiFi.broadcastIP()};
  for (auto& t : targets) { udp.beginPacket(t, DISCOVERY_PORT); udp.write((const uint8_t*)p.c_str(), p.length()); udp.endPacket(); }
}

static void messagingTask(void*) {
  uint32_t lastBeat = 0, lastCheck = 0, lastScanAt = 0, bootAt = millis();
  for (;;) {
    if (!staOK) { if (udpUp) { udp.stop(); udpUp = false; } vTaskDelay(pdMS_TO_TICKS(500)); continue; }
    if (!udpUp) { udpUp = udp.begin(DISCOVERY_PORT); }
    // listen
    int sz;
    while (udpUp && (sz = udp.parsePacket()) > 0) {
      char buf[600]; int n = udp.read(buf, std::min(sz, (int)sizeof buf - 1)); if (n <= 0) break; buf[n] = 0;
      JsonDocument d(&psAlloc);
      if (!deserializeJson(d, buf) && String(d["type"] | "") == "shelf-presence") notePeer(d, udp.remoteIP().toString(), "broadcast");
    }
    uint32_t now = millis();
    if (now - lastBeat > 3000) { broadcastPresence(); lastBeat = now; }
    if (scanRequested || (now - bootAt > 4000 && (lastScanAt == 0 || now - lastScanAt > 120000))) {
      scanRequested = false; scanNetwork(); lastScanAt = millis();
    }
    if (now - lastCheck > 5000) { checkInWithKnown(); lastCheck = millis(); }
    saveKnownIfDue();
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

void messagingBegin() {
  peersLock = xSemaphoreCreateMutex();
  mprefs.begin("msg", false);
  me.id = mprefs.getString("id", "");
  if (!isValidId(me.id)) { me.id = uuid4(); }
  me.name = mprefs.getString("name", hostName);
  me.status = mprefs.getString("status", "online");
  saveIdentity();
  char t[17]; snprintf(t, sizeof t, "%08x%08x", esp_random(), esp_random()); instanceTag = t;
  mkdirs(CONV_DIR);
  loadKnown();
  xTaskCreatePinnedToCore(messagingTask, "msg", 10240, nullptr, 1, nullptr, 0);
}

void messagingLoop() {}

int onlinePeerCount() {
  if (!peersLock) return 0;
  PLock l; int n = 0; for (auto& p : peers) if (online(p)) n++; return n;
}

// ------------------------------------------------------------ conversations on the card
// /.shelf/data/messaging/c/<peerId>/00000001-i.json  (i = in, o = out), plus a "read" marker.
struct MsgFile { uint32_t seq; bool in; String path; };

static void listMessages(const String& peerId, std::vector<MsgFile>& out) {
  File d = store->open(String(CONV_DIR) + "/" + peerId);
  if (!d || !d.isDirectory()) return;
  File c = d.openNextFile();
  while (c) {
    String n = c.name();
    if (n.endsWith(".json") && n.length() >= 15) out.push_back({(uint32_t)n.substring(0, 8).toInt(), n.charAt(9) == 'i', String(c.path())});
    c.close(); c = d.openNextFile();
  }
  d.close();
  std::sort(out.begin(), out.end(), [](const MsgFile& a, const MsgFile& b) { return a.seq < b.seq; });
}

static uint32_t readMarker(const String& peerId) {
  File f = store->open(String(CONV_DIR) + "/" + peerId + "/read", FILE_READ);
  if (!f) return 0;
  uint32_t v = f.readString().toInt(); f.close(); return v;
}
static void writeMarker(const String& peerId, uint32_t v) {
  File f = store->open(String(CONV_DIR) + "/" + peerId + "/read", FILE_WRITE);
  if (f) { f.print(v); f.close(); }
}

static String nextMessagePath(const String& peerId, bool in) {
  std::vector<MsgFile> v; listMessages(peerId, v);
  uint32_t seq = v.empty() ? 1 : v.back().seq + 1;
  char n[24]; snprintf(n, sizeof n, "/%08u-%c.json", (unsigned)seq, in ? 'i' : 'o');
  mkdirs(String(CONV_DIR) + "/" + peerId);
  return String(CONV_DIR) + "/" + peerId + n;
}

static void preview(const String& path, String& text, bool& image, uint64_t& ts) {
  JsonDocument f(&psAlloc); f["text"] = true; f["timestamp"] = true;
  JsonDocument d(&psAlloc);
  File fh = store->open(path, FILE_READ);
  if (fh) { deserializeJson(d, fh, DeserializationOption::Filter(f)); fh.close(); }
  text = d["text"] | ""; ts = d["timestamp"] | 0ULL;
  fh = store->open(path, FILE_READ);
  image = false;
  if (fh) {   // look for "image":"data: without loading the picture
    const char* pat = "\"image\":\"data:"; size_t m = 0, n = strlen(pat); int c;
    while ((c = fh.read()) >= 0) { if (c == pat[m]) { if (++m == n) { image = true; break; } } else m = (c == pat[0]) ? 1 : 0; }
    fh.close();
  }
}

// ------------------------------------------------------------ HTTP routes
bool handleMessagingApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m) {
  if (!url.startsWith("/api/messaging/")) return false;
  String ip = r->client() ? r->client()->remoteIP().toString() : String("");

  if (url == "/api/messaging/identity") {
    if (m == HTTP_PUT) {
      JsonDocument b(&psAlloc); parseBody(r, b);
      String name = b["name"] | ""; name.trim();
      if (name.length()) me.name = name.substring(0, 60);
      String st = b["status"] | "";
      if (st == "online" || st == "away" || st == "busy" || st == "offline") me.status = st;
      saveIdentity();
      if (staOK && udpUp) broadcastPresence();
    }
    sendJson(r, 200, identityJson()); return true;
  }
  if (url == "/api/messaging/hello" && m == HTTP_POST) {
    if (me.status == "offline") { sendErr(r, 404, "Not found"); return true; }
    JsonDocument b(&psAlloc);
    if (parseBody(r, b) && String(b["type"] | "") == "shelf-presence") notePeer(b, ip, "hello");
    sendJson(r, 200, "{\"type\":\"shelf-hello\",\"id\":\"" + me.id + "\",\"name\":\"" + jsonEsc(me.name) + "\",\"status\":\"" + me.status +
                     "\",\"httpPort\":" + MY_PORT + ",\"instance\":\"" + instanceTag + "\"}");
    return true;
  }
  if (url == "/api/messaging/peers") {
    String s = "["; bool first = true;
    { PLock l; for (auto& p : peers) if (online(p)) { if (!first) s += ","; s += peerJson(p); first = false; } }
    sendJson(r, 200, s + "]"); return true;
  }
  if (url == "/api/messaging/known-peers") {
    std::vector<Peer> v; { PLock l; v = peers; }
    std::sort(v.begin(), v.end(), [](const Peer& a, const Peer& b) { return a.lastSeenAt > b.lastSeenAt; });
    String s = "[";
    for (size_t i = 0; i < v.size(); i++) {
      if (i) s += ",";
      s += "{\"id\":\"" + v[i].id + "\",\"name\":\"" + jsonEsc(v[i].name) + "\",\"ip\":" + (v[i].ip.length() ? "\"" + v[i].ip + "\"" : String("null")) +
           ",\"httpPort\":" + v[i].port + ",\"online\":" + (online(v[i]) ? "true" : "false") + ",\"lastSeenAt\":" + String((double)v[i].lastSeenAt, 0) + "}";
    }
    sendJson(r, 200, s + "]"); return true;
  }
  if (url == "/api/messaging/self") {
    String s = identityJson();
    s.remove(s.length() - 1);
    s += ",\"addresses\":[\"" + currentIP() + "\"],\"port\":" + String(MY_PORT) + ",\"lastScan\":{\"at\":" + String((double)lastScan.at, 0) +
         ",\"found\":" + lastScan.found + ",\"scanning\":" + (lastScan.scanning ? "true" : "false") + ",\"addresses\":" + lastScan.addresses +
         ",\"ranges\":[" + (lastScan.range.length() ? "\"" + lastScan.range + "\"" : String("")) + "]},\"identityClash\":null,\"sharedIdentities\":[]}";
    sendJson(r, 200, s); return true;
  }
  if (url == "/api/messaging/scan" && m == HTTP_POST) {
    scanRequested = true;
    int on = 0; { PLock l; for (auto& p : peers) if (online(p)) on++; }
    sendJson(r, 200, String("{\"at\":") + String((double)lastScan.at, 0) + ",\"found\":" + lastScan.found + ",\"scanning\":true,\"addresses\":" +
                     lastScan.addresses + ",\"online\":" + on + "}");
    return true;
  }
  if (url == "/api/messaging/add" && m == HTTP_POST) {
    JsonDocument b(&psAlloc); parseBody(r, b);
    String raw = b["address"] | ""; raw.trim();
    if (raw.startsWith("http://")) raw = raw.substring(7); else if (raw.startsWith("https://")) raw = raw.substring(8);
    int sl = raw.indexOf('/'); if (sl >= 0) raw = raw.substring(0, sl);
    if (!raw.length()) { sendErr(r, 400, "Type an address like 10.0.0.50, 10.0.0.50:8080, or raspberrypi.local."); return true; }
    String host = raw; uint16_t port = 8080;
    int c = raw.lastIndexOf(':');
    if (c > 0) { host = raw.substring(0, c); port = raw.substring(c + 1).toInt(); if (!port) { sendErr(r, 400, "That port number isn't valid."); return true; } }
    IPAddress addr;
    if (!addr.fromString(host)) {
      bool okDns = false;
      if (host.endsWith(".local")) { addr = MDNS.queryHost(host.substring(0, host.length() - 6), 2000); okDns = (uint32_t)addr != 0; }
      if (!okDns) okDns = WiFi.hostByName(host.c_str(), addr) == 1;
      if (!okDns) { sendJson(r, 200, "{\"ok\":false,\"message\":\"Couldn't find a computer called \\\"" + jsonEsc(host) + "\\\" on the network. Try its IP address instead.\"}"); return true; }
    }
    String sip = addr.toString(), where = sip + ":" + port;
    if (sip == currentIP() && (port == 80 || port == 8080)) { sendJson(r, 200, "{\"ok\":false,\"message\":\"That's this Shelf.\"}"); return true; }
    JsonDocument d(&psAlloc); int code = 0;
    if (!probeShelf(sip, port, 4000, d, &code)) {
      String msg = code <= 0 ? "Nothing answered at " + where + ". Check the address, and that the computer is switched on and on the same network. A firewall on that computer can also block it."
                 : code == 503 ? "The Shelf at " + where + " is switched off, or set to \\\"Local computer only\\\"."
                 : "Something answered at " + where + ", but it isn't The Shelf (or its messaging is switched off).";
      sendJson(r, 200, "{\"ok\":false,\"message\":\"" + msg + "\"}"); return true;
    }
    if (String(d["id"] | "") == me.id) { sendJson(r, 200, "{\"ok\":false,\"message\":\"That's this Shelf.\"}"); return true; }
    notePeer(d, sip, "manual");
    String pj; { PLock l; for (auto& p : peers) if (p.id == String(d["id"] | "")) pj = peerJson(p); }
    sendJson(r, 200, "{\"ok\":true,\"peer\":" + (pj.length() ? pj : String("null")) + ",\"older\":false,\"warning\":null}");
    saveKnownIfDue(true);
    return true;
  }
  if (url == "/api/messaging/conversations" && m == HTTP_GET) {
    std::vector<String> ids;
    File d = store->open(CONV_DIR);
    if (d) { File c = d.openNextFile(); while (c) { if (c.isDirectory() && isValidId(c.name())) ids.push_back(c.name()); c.close(); c = d.openNextFile(); } d.close(); }
    struct Row { uint64_t lastAt; String json; };
    std::vector<Row> rows;
    for (auto& id : ids) {
      std::vector<MsgFile> v; listMessages(id, v);
      uint32_t rd = readMarker(id);
      int unread = 0; const MsgFile* last = v.empty() ? nullptr : &v.back(); const MsgFile* lastIn = nullptr;
      for (auto& mf : v) if (mf.in) { lastIn = &mf; if (mf.seq > rd) unread++; }
      String lt, it; bool li = false, ii = false; uint64_t lts = 0, its = 0;
      if (last) preview(last->path, lt, li, lts);
      if (lastIn) preview(lastIn->path, it, ii, its);
      Peer p; bool on = false;
      { PLock l; for (auto& x : peers) if (x.id == id) { p = x; on = online(x); } }
      String lastPreview = lt.length() ? lt : (li ? String("\xF0\x9F\x96\xBC\xEF\xB8\x8F Image") : String(""));
      String inPreview = it.length() ? it : (ii ? String("Sent an image") : String(""));
      uint64_t lastAt = last ? lts : p.lastSeenAt;
      rows.push_back({lastAt, "{\"lastInAt\":" + String((double)its, 0) + ",\"lastInPreview\":\"" + jsonEsc(inPreview) + "\",\"id\":\"" + id +
        "\",\"name\":\"" + jsonEsc(p.name.length() ? p.name : String("Unknown")) + "\",\"online\":" + (on ? "true" : "false") +
        ",\"ip\":" + (p.ip.length() ? "\"" + p.ip + "\"" : String("null")) + ",\"httpPort\":" + (p.ip.length() ? String(p.port) : String("null")) +
        ",\"lastMessage\":\"" + jsonEsc(lastPreview) + "\",\"lastAt\":" + String((double)lastAt, 0) + ",\"unread\":" + unread + "}"});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.lastAt > b.lastAt; });
    String s = "["; for (size_t i = 0; i < rows.size(); i++) { if (i) s += ","; s += rows[i].json; }
    sendJson(r, 200, s + "]"); return true;
  }
  if (url.startsWith("/api/messaging/conversations/") && m == HTTP_GET) {
    String id = url.substring(29);
    if (!isValidId(id)) { sendErr(r, 400, "Invalid id"); return true; }
    std::vector<MsgFile> v; listMessages(id, v);
    auto* paths = new std::vector<String>(); auto* tails = new std::vector<String>();
    for (auto& mf : v) { paths->push_back(mf.path); tails->push_back(",\"read\":true}"); }
    if (!v.empty() && readMarker(id) < v.back().seq) writeMarker(id, v.back().seq);
    sendJsonArrayOfFiles(r, paths, tails);
    return true;
  }
  if (url == "/api/messaging/receive" && m == HTTP_POST) {
    JsonDocument f(&psAlloc); f["fromId"] = true; f["fromName"] = true; f["fromPort"] = true; f["text"] = true;
    JsonDocument b(&psAlloc);
    if (bodyLen(r) > 2200000) { sendErr(r, 400, "Invalid image"); return true; }
    if (!parseBody(r, b, &f) || !b.is<JsonObject>()) { sendErr(r, 400, "Invalid message"); return true; }
    String from = b["fromId"] | "";
    bool hasImg = bodyContains(r, "\"image\":\"");
    if (hasImg && !bodyContains(r, "\"image\":\"data:image/")) { sendErr(r, 400, "Invalid image"); return true; }
    String text = b["text"] | "";
    if (!isValidId(from) || (!text.length() && !hasImg)) { sendErr(r, 400, "Invalid message"); return true; }
    String path = nextMessagePath(from, true);
    String tail = ",\"id\":\"" + uuid4() + "\",\"direction\":\"in\",\"timestamp\":" + String((double)nowMs(), 0) + "}";
    if (!bodyWriteWithTail(r, path, tail)) { sendErr(r, 500, "Could not save the message"); return true; }
    JsonDocument info(&psAlloc);
    info["id"] = from; info["name"] = b["fromName"] | "Unknown"; info["httpPort"] = portOf(b["fromPort"], 8080);
    if (!b["fromPort"].isNull()) notePeer(info, ip, "message");
    else { PLock l; bool k = false; for (auto& p : peers) if (p.id == from) k = true; if (!k) { Peer p; p.id = from; p.name = info["name"].as<String>(); peers.push_back(p); knownDirty = true; } }
    sendJson(r, 200, "{\"ok\":true}"); return true;
  }
  if (url == "/api/messaging/send" && m == HTTP_POST) {
    JsonDocument f(&psAlloc); f["toId"] = true; f["text"] = true;
    JsonDocument b(&psAlloc);
    if (!parseBody(r, b, &f)) { sendErr(r, 400, "Missing recipient, or nothing to send"); return true; }
    String to = b["toId"] | ""; String text = b["text"] | ""; text.trim();
    bool hasImg = bodyContains(r, "\"image\":\"data:image/");
    if (!isValidId(to) || (!text.length() && !hasImg)) { sendErr(r, 400, "Missing recipient, or nothing to send"); return true; }
    if (bodyLen(r) > 2100000) { sendErr(r, 400, "That image is too large to send."); return true; }
    String pip; uint16_t pport = 8080; String pname;
    { PLock l; for (auto& p : peers) if (p.id == to) { pip = p.ip; pport = p.port; pname = p.name; } }
    if (!pip.length()) { sendErr(r, 404, "That person appears to be offline right now."); return true; }
    uint64_t ts = nowMs();
    // what goes over the wire: their body plus who it's from
    String wire = String(TMP_ROOT "/send-") + esp_random() + ".json";
    String wtail = ",\"fromId\":\"" + me.id + "\",\"fromName\":\"" + jsonEsc(me.name) + "\",\"fromPort\":" + MY_PORT + ",\"timestamp\":" + String((double)ts, 0) + "}";
    if (!bodyWriteWithTail(r, wire, wtail)) { sendErr(r, 500, "Could not send"); return true; }
    bool delivered = false;
    {
      File wf = store->open(wire, FILE_READ);
      WiFiClient client; HTTPClient http;
      http.setConnectTimeout(4000); http.setTimeout(8000);
      if (wf && http.begin(client, pip, pport, "/api/messaging/receive")) {
        http.addHeader("Content-Type", "application/json");
        int code = http.sendRequest("POST", &wf, wf.size());
        delivered = code >= 200 && code < 300;
        http.end();
      }
      if (wf) wf.close();
    }
    if (!delivered) { store->remove(wire); sendErr(r, 502, "Couldn't reach that person \xE2\x80\x94 they may have just gone offline."); return true; }
    String path = nextMessagePath(to, false);
    String tail = ",\"id\":\"" + uuid4() + "\",\"direction\":\"out\",\"timestamp\":" + String((double)ts, 0) + ",\"read\":true}";
    bool saved = fileWriteWithTail(wire, path, tail);
    store->remove(wire);
    if (!saved) { sendErr(r, 500, "Sent, but couldn't save it here."); return true; }
    sendJsonFile(r, path);
    return true;
  }
  return false;
}
