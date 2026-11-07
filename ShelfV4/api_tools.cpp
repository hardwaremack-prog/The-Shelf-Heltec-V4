// The Shelf for Heltec V4 - the Tools: Home board, Notes, Docs, Sheets, Slides, Draw,
// Visitors, weather, and the settings endpoints The Shelf's web app asks for.
#include "shelf.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <algorithm>

static const char* const COLLECTIONS[] = {"notes", "documents", "presentations", "sheets", nullptr};
static String collDir(const String& kind) { return String(DATA_ROOT "/") + kind; }

// The "updated" stamp is always the last thing in files the board writes.
static uint64_t updatedOf(const String& path) {
  File f = store->open(path, FILE_READ);
  if (!f) return 0;
  size_t sz = f.size(), n = std::min(sz, (size_t)96);
  char b[97]; f.seek(sz - n); n = f.read((uint8_t*)b, n); b[n] = 0; f.close();
  const char* p = strstr(b, "\"updated\":");
  const char* last = nullptr;
  while (p) { last = p; p = strstr(p + 1, "\"updated\":"); }
  return last ? strtoull(last + 10, nullptr, 10) : 0;
}

static void listByUpdated(AsyncWebServerRequest* r, const String& dir, const char* suffix) {
  std::vector<std::pair<uint64_t, String>> v;
  File d = store->open(dir);
  if (d && d.isDirectory()) {
    File c = d.openNextFile();
    while (c) {
      String n = c.name();
      if (!c.isDirectory() && n.endsWith(suffix)) { String p = c.path(); c.close(); v.push_back({updatedOf(p), p}); }
      else c.close();
      c = d.openNextFile();
    }
    d.close();
  }
  std::sort(v.begin(), v.end(), [](const std::pair<uint64_t, String>& a, const std::pair<uint64_t, String>& b) { return a.first > b.first; });
  auto* paths = new std::vector<String>();
  for (auto& e : v) paths->push_back(e.second);
  sendJsonArrayOfFiles(r, paths);
}

// ------------------------------------------------------------ Notes / Documents / Presentations / Sheets
static bool collection(AsyncWebServerRequest* r, const String& kind, const String& id, WebRequestMethod m) {
  String dir = collDir(kind);
  mkdirs(dir);
  if (!id.length()) {
    if (m == HTTP_GET) { listByUpdated(r, dir, ".json"); return true; }
    if (m == HTTP_POST) {
      if (!bodyValidObject(r)) { sendErr(r, 400, "Invalid item"); return true; }
      String nid = uuid4(), path = dir + "/" + nid + ".json";
      String tail = ",\"id\":\"" + nid + "\",\"updated\":" + String((double)nowMs(), 0) + "}";
      if (!bodyWriteWithTail(r, path, tail)) { sendErr(r, 500, "Could not save."); return true; }
      statsDirty = true;
      sendJsonFile(r, path); return true;
    }
    return false;
  }
  if (!isValidId(id)) { sendErr(r, 400, "Invalid id"); return true; }
  String path = dir + "/" + id + ".json";
  if (m == HTTP_GET) { if (!store->exists(path)) sendErr(r, 404, "Not found"); else sendJsonFile(r, path); return true; }
  if (m == HTTP_PUT) {
    if (!store->exists(path)) { sendErr(r, 404, "Not found"); return true; }
    if (!bodyValidObject(r)) { sendErr(r, 400, "Invalid item"); return true; }
    String tail = ",\"id\":\"" + id + "\",\"updated\":" + String((double)nowMs(), 0) + "}";
    if (!bodyWriteWithTail(r, path, tail)) { sendErr(r, 500, "Could not save."); return true; }
    statsDirty = true;
    sendJsonFile(r, path); return true;
  }
  if (m == HTTP_DELETE) {
    if (!store->remove(path)) { sendErr(r, 404, "Not found"); return true; }
    sendJson(r, 200, "{\"deleted\":\"" + id + "\"}"); return true;
  }
  return false;
}

// ------------------------------------------------------------ Draw
static String drawingJson(const String& id, const String& title, uint64_t updated) {
  return "{\"id\":\"" + id + "\",\"title\":\"" + jsonEsc(title) + "\",\"updated\":" + String((double)updated, 0) +
         ",\"url\":\"/data-files/drawings/" + id + ".png?v=" + String((uint32_t)(updated / 1000)) + "\"}";
}

static bool drawings(AsyncWebServerRequest* r, const String& id, WebRequestMethod m) {
  String dir = DATA_ROOT "/drawings";
  mkdirs(dir);
  if (!id.length() && m == HTTP_GET) {
    // meta files are small: build the list in memory
    std::vector<std::pair<uint64_t, String>> v;
    File d = store->open(dir);
    if (d) { File c = d.openNextFile(); while (c) { String n = c.name(); String p = c.path(); c.close();
      if (n.endsWith(".json")) { JsonDocument j(&psAlloc); if (loadJsonFile(p, j)) v.push_back({j["updated"] | 0ULL, drawingJson(j["id"] | "", j["title"] | "Untitled", j["updated"] | 0ULL)}); }
      c = d.openNextFile(); } d.close(); }
    std::sort(v.begin(), v.end(), [](const std::pair<uint64_t, String>& a, const std::pair<uint64_t, String>& b) { return a.first > b.first; });
    String s = "["; for (size_t i = 0; i < v.size(); i++) { if (i) s += ","; s += v[i].second; } s += "]";
    sendJson(r, 200, s); return true;
  }
  JsonDocument filter(&psAlloc); filter["title"] = true;
  if (!id.length() && m == HTTP_POST) {
    JsonDocument b(&psAlloc); parseBody(r, b, &filter);
    String nid = uuid4();
    if (!bodyDataUrlToFile(r, "dataUrl", dir + "/" + nid + ".png", 20u * 1024 * 1024)) { sendErr(r, 400, "Expected a PNG data URL"); return true; }
    String title = b["title"] | "Untitled"; if (!title.length()) title = "Untitled";
    uint64_t t = nowMs();
    JsonDocument meta(&psAlloc); meta["id"] = nid; meta["title"] = title; meta["updated"] = t;
    saveJsonFile(dir + "/" + nid + ".json", meta);
    sendJson(r, 200, drawingJson(nid, title, t)); return true;
  }
  if (!isValidId(id)) { sendErr(r, 400, "Invalid id"); return true; }
  String metaPath = dir + "/" + id + ".json";
  if (m == HTTP_PUT) {
    if (!store->exists(metaPath)) { sendErr(r, 404, "Not found"); return true; }
    JsonDocument b(&psAlloc); parseBody(r, b, &filter);
    String tmp = dir + "/" + id + ".png.part";
    if (bodyDataUrlToFile(r, "dataUrl", tmp, 20u * 1024 * 1024)) moveFile(tmp, dir + "/" + id + ".png");
    String title = b["title"] | "Untitled"; if (!title.length()) title = "Untitled";
    uint64_t t = nowMs();
    JsonDocument meta(&psAlloc); meta["id"] = id; meta["title"] = title; meta["updated"] = t;
    saveJsonFile(metaPath, meta);
    sendJson(r, 200, drawingJson(id, title, t)); return true;
  }
  if (m == HTTP_DELETE) {
    store->remove(metaPath); store->remove(dir + "/" + id + ".png");
    sendJson(r, 200, "{\"deleted\":\"" + id + "\"}"); return true;
  }
  return false;
}

// ------------------------------------------------------------ Home board (portal)
static const char* PORTAL_FILE = DATA_ROOT "/portal.json";
static void loadPortal(JsonDocument& p) {
  if (!loadJsonFile(PORTAL_FILE, p) || !p.is<JsonObject>()) p.to<JsonObject>();
  if (!p["links"].is<JsonArray>()) p["links"].to<JsonArray>();
  if (!p["posts"].is<JsonArray>()) p["posts"].to<JsonArray>();
}

void portalMediaDone(AsyncWebServerRequest* r) {
  UploadCtx* u = (UploadCtx*)r->_tempObject;
  if (!u || u->magic != UPLOAD_MAGIC || !u->count || u->err) return sendErr(r, 400, "Only images and videos can be pasted onto the board.");
  FsGuard g;
  JsonDocument p(&psAlloc); loadPortal(p);
  JsonObject post = p["posts"].as<JsonArray>().add<JsonObject>();
  post["id"] = uuid4();
  post["type"] = u->mediaType;
  post["mediaUrl"] = String("/data-files/portal-media/") + u->saved;
  post["color"] = r->hasParam("color", true) ? r->getParam("color", true)->value() : String("amber");
  post["created"] = nowMs();
  saveJsonFile(PORTAL_FILE, p);
  String s; serializeJson(post, s); sendJson(r, 200, s);
}

static bool portal(AsyncWebServerRequest* r, const String& url, WebRequestMethod m) {
  FsGuard g;
  if (url == "/api/portal" && m == HTTP_GET) { JsonDocument p(&psAlloc); loadPortal(p); sendDoc(r, p); return true; }
  if (url == "/api/portal/posts/media" && m == HTTP_POST) { portalMediaDone(r); return true; }
  JsonDocument p(&psAlloc); loadPortal(p);
  JsonDocument b(&psAlloc); parseBody(r, b);
  if (url == "/api/portal/links" && m == HTTP_POST) {
    JsonObject l = p["links"].as<JsonArray>().add<JsonObject>();
    l["id"] = uuid4();
    String u = b["url"] | "";
    String label = b["label"] | ""; if (!label.length()) label = u;
    l["label"] = label; l["url"] = u;
    saveJsonFile(PORTAL_FILE, p);
    String s; serializeJson(l, s); sendJson(r, 200, s); return true;
  }
  if (url == "/api/portal/posts" && m == HTTP_POST) {
    JsonObject post = p["posts"].as<JsonArray>().add<JsonObject>();
    post["id"] = uuid4(); post["type"] = "text";
    post["text"] = b["text"] | ""; post["color"] = b["color"] | "amber"; post["created"] = nowMs();
    saveJsonFile(PORTAL_FILE, p);
    String s; serializeJson(post, s); sendJson(r, 200, s); return true;
  }
  if (m == HTTP_DELETE && (url.startsWith("/api/portal/links/") || url.startsWith("/api/portal/posts/"))) {
    bool links = url.startsWith("/api/portal/links/");
    String id = url.substring(18);
    JsonArray a = p[links ? "links" : "posts"].as<JsonArray>();
    for (size_t i = 0; i < a.size(); i++) {
      if (String(a[i]["id"] | "") == id) {
        String media = a[i]["mediaUrl"] | "";
        if (media.startsWith("/data-files/portal-media/")) {
          String f = media.substring(25);
          if (f.indexOf('/') < 0 && f.indexOf("..") < 0) store->remove(String(DATA_ROOT "/portal-media/") + f);
        }
        a.remove(i); break;
      }
    }
    saveJsonFile(PORTAL_FILE, p);
    sendJson(r, 200, "{\"deleted\":\"" + jsonEsc(id) + "\"}"); return true;
  }
  return false;
}

// ------------------------------------------------------------ Visitors
static const char* VISITORS_FILE = DATA_ROOT "/visitors.json";
void recordVisitor(const String& ip) {
  if (!ip.length() || ip == "127.0.0.1") return;
  FsGuard g;
  JsonDocument v(&psAlloc);
  if (!loadJsonFile(VISITORS_FILE, v) || !v.is<JsonObject>()) v.to<JsonObject>();
  uint64_t now = nowMs();
  JsonObject o = v[ip].is<JsonObject>() ? v[ip].as<JsonObject>() : v[ip].to<JsonObject>();
  o["ip"] = ip;
  if (o["hostname"].isNull()) o["hostname"] = nullptr;
  if (o["firstSeenAt"].isNull()) o["firstSeenAt"] = now;
  o["lastSeenAt"] = now;
  saveJsonFile(VISITORS_FILE, v);
}

// ------------------------------------------------------------ Weather (Open-Meteo, cached 10 minutes)
static String wxKey, wxData; static uint32_t wxAt = 0;
static void weather(AsyncWebServerRequest* r) {
  double lat = r->hasParam("lat") ? r->getParam("lat")->value().toDouble() : NAN;
  double lon = r->hasParam("lon") ? r->getParam("lon")->value().toDouble() : NAN;
  if (isnan(lat) || isnan(lon) || !r->hasParam("lat") || !r->hasParam("lon")) return sendErr(r, 400, "Missing or invalid location.");
  char key[32]; snprintf(key, sizeof key, "%.2f,%.2f", lat, lon);
  if (wxKey == key && wxData.length() && millis() - wxAt < 600000) return sendJson(r, 200, wxData);
  if (!staOK) return sendErr(r, 502, "Couldn't reach the weather service -- the board isn't on your home Wi-Fi yet.");
  HTTPClient http;
  char url[160];
  snprintf(url, sizeof url, "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current_weather=true", lat, lon);
  http.setTimeout(8000);
  if (!http.begin(url)) return sendErr(r, 502, "Couldn't reach the weather service.");
  int code = http.GET();
  if (code != 200) { http.end(); return sendErr(r, 502, String("Couldn't reach the weather service -- check your internet connection. (") + code + ")"); }
  JsonDocument j(&psAlloc); JsonDocument f(&psAlloc); f["current_weather"] = true;
  DeserializationError e = deserializeJson(j, http.getStream(), DeserializationOption::Filter(f));
  http.end();
  JsonObject cw = j["current_weather"];
  if (e || cw.isNull() || !cw["temperature"].is<float>()) return sendErr(r, 502, "Unexpected response from the weather service.");
  int code2 = cw["weathercode"] | -1;
  struct W { int c; const char* l; const char* i; };
  static const W wmo[] = {{0,"Clear sky","☀️"},{1,"Mainly clear","\U0001F324️"},{2,"Partly cloudy","⛅"},{3,"Overcast","☁️"},
    {45,"Fog","\U0001F32B️"},{48,"Freezing fog","\U0001F32B️"},{51,"Light drizzle","\U0001F326️"},{53,"Drizzle","\U0001F326️"},
    {55,"Heavy drizzle","\U0001F327️"},{56,"Light freezing drizzle","\U0001F327️"},{57,"Freezing drizzle","\U0001F327️"},
    {61,"Light rain","\U0001F327️"},{63,"Rain","\U0001F327️"},{65,"Heavy rain","\U0001F327️"},{66,"Light freezing rain","\U0001F327️"},
    {67,"Freezing rain","\U0001F327️"},{71,"Light snow","\U0001F328️"},{73,"Snow","\U0001F328️"},{75,"Heavy snow","❄️"},
    {77,"Snow grains","\U0001F328️"},{80,"Light rain showers","\U0001F326️"},{81,"Rain showers","\U0001F327️"},
    {82,"Heavy rain showers","⛈️"},{85,"Light snow showers","\U0001F328️"},{86,"Heavy snow showers","❄️"},
    {95,"Thunderstorm","⛈️"},{96,"Thunderstorm with hail","⛈️"},{99,"Severe thunderstorm","⛈️"}};
  const char* label = "Unknown"; const char* icon = "\U0001F321️";
  for (auto& w : wmo) if (w.c == code2) { label = w.l; icon = w.i; }
  float t = cw["temperature"];
  JsonDocument out(&psAlloc);
  out["tempC"] = (int)lroundf(t); out["tempF"] = (int)lroundf(t * 9 / 5 + 32);
  out["condition"] = label; out["icon"] = icon; out["isDay"] = (cw["is_day"] | 0) != 0;
  wxData = ""; serializeJson(out, wxData); wxKey = key; wxAt = millis();
  sendJson(r, 200, wxData);
}

// ------------------------------------------------------------ router
bool handleToolsApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m) {
  for (int i = 0; COLLECTIONS[i]; i++) {
    String base = String("/api/") + COLLECTIONS[i];
    if (url == base) return collection(r, COLLECTIONS[i], "", m);
    if (url.startsWith(base + "/")) {
      String id = url.substring(base.length() + 1);
      if (String(COLLECTIONS[i]) == "sheets" && id == "import") { sendErr(r, 501, "Importing spreadsheet files needs The Shelf on a computer. You can still make sheets here."); return true; }
      return collection(r, COLLECTIONS[i], id, m);
    }
  }
  if (url == "/api/drawings") return drawings(r, "", m);
  if (url.startsWith("/api/drawings/")) return drawings(r, url.substring(14), m);
  if (url.startsWith("/api/portal")) return portal(r, url, m);

  if (url == "/api/visitors") {
    if (m == HTTP_DELETE) { store->remove(VISITORS_FILE); sendJson(r, 200, "{\"cleared\":true}"); return true; }
    JsonDocument v(&psAlloc); loadJsonFile(VISITORS_FILE, v);
    std::vector<JsonObject> list; for (JsonPair kv : v.as<JsonObject>()) list.push_back(kv.value().as<JsonObject>());
    std::sort(list.begin(), list.end(), [](JsonObject a, JsonObject b) { return (a["lastSeenAt"] | 0ULL) > (b["lastSeenAt"] | 0ULL); });
    JsonDocument out(&psAlloc); JsonArray a = out.to<JsonArray>(); for (auto& o : list) a.add(o);
    sendDoc(r, out); return true;
  }
  if (url == "/api/weather") { weather(r); return true; }

  // ---- settings The Shelf's page reads. On the board everyone on your Wi-Fi can use it,
  //      like Heltec Shelf Lite; there's no "host computer" to sit at, so every visitor
  //      is treated as the host (the host-only switches are hidden by board.css).
  if (url == "/api/auth/status") {
    sendJson(r, 200, "{\"isLocal\":true,\"protectionEnabled\":false,\"secureMode\":false,\"loggedIn\":false,\"username\":null,\"role\":\"write\"}");
    return true;
  }
  if (url == "/api/auth/mode" && m == HTTP_GET) { sendJson(r, 200, "{\"secureMode\":false}"); return true; }
  if (url.startsWith("/api/auth/")) { sendErr(r, 403, "Accounts aren't available on the Heltec board -- anyone on your home Wi-Fi can use it."); return true; }
  if (url == "/api/power") { if (m == HTTP_GET) sendJson(r, 200, "{\"on\":true}"); else sendErr(r, 403, "The board is always on. Unplug it to turn it off."); return true; }
  if (url == "/api/localhost-only") { if (m == HTTP_GET) sendJson(r, 200, "{\"localhostOnly\":false}"); else sendErr(r, 403, "Not available on the board."); return true; }
  if (url == "/api/open-write-access") { if (m == HTTP_GET) sendJson(r, 200, "{\"openWriteAccess\":true}"); else sendErr(r, 403, "On the board, anyone on your Wi-Fi can already make changes."); return true; }
  if (url == "/api/network-info") {
    sendJson(r, 200, "{\"ips\":[\"" + hostName + ".local\"],\"port\":80,\"ip\":\"" + currentIP() + "\"}");
    return true;
  }
  if (url.startsWith("/api/backup")) { sendErr(r, 501, "Backups aren't available on the board -- copy the microSD card to your computer instead. Everything is in normal folders, with The Shelf's own data in the hidden .shelf folder."); return true; }
  if (url.startsWith("/api/ai/")) { sendErr(r, 501, "The AI assistant needs The Shelf on a computer."); return true; }
  if (url.startsWith("/api/imagine")) {
    if (url == "/api/imagine/status") { sendJson(r, 200, "{\"hasKey\":false,\"canManageKey\":false}"); return true; }
    sendErr(r, 501, "Create with AI needs The Shelf on a computer."); return true;
  }
  if (url.startsWith("/api/bigfiles")) {
    if (url == "/api/bigfiles" && m == HTTP_GET) { sendJson(r, 200, "[]"); return true; }
    sendErr(r, 501, "Big Transfer isn't available on the board. Use the normal shelves (up to 4 GB per file)."); return true;
  }
  if (url.startsWith("/api/vnc/")) { sendErr(r, 501, "Remote desktop (VNC) needs The Shelf on a computer."); return true; }
  return false;
}
