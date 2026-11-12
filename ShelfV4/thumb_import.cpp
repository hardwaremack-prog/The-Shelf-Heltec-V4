// The Shelf on a board - copy thumbnails and album art from The Shelf on a computer.
//
// Thumbnails for games, programs, books and videos, and album art fetched online, live on the
// computer that runs The Shelf. Files copied onto the board's card arrive without them. Here the
// board asks that Shelf for each shelf's list and downloads the pictures for the files it has
// itself, straight over the network (no browser in between, so nothing blocks it).
//
//   POST /api/thumbs/import  {from: "http://10.0.0.80:8080"}   start copying
//   GET  /api/thumbs/import                                     how it's going
#include "shelf.h"
#include <HTTPClient.h>

static volatile bool running = false;
static String from;                                    // set only while nothing is running
// read by the web task while the copying task writes them: plain buffers, never reallocated
static char status[64] = "", errorMsg[128] = "";
static void setStatus(const String& t) { snprintf(status, sizeof status, "%s", t.c_str()); }
static void setError(const String& t) { snprintf(errorMsg, sizeof errorMsg, "%s", t.c_str()); }
static volatile int copied = 0, skipped = 0, failed = 0;

static const char* const CATS[] = {"Games", "Programs", "Books", "Videos", "Music", nullptr};

static bool download(const String& url, const String& dest) {
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(url)) return false;
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  int len = http.getSize();
  if (len == 0 || len > 3 * 1024 * 1024) { http.end(); return false; }
  String tmp = dest + ".part";
  bool ok = false;
  {
    FsGuard g;
    File f = store->open(tmp, FILE_WRITE);
    if (f) { int n = http.writeToStream(&f); f.close(); ok = n > 0 && (len < 0 || n == len); }
    if (ok) ok = moveFile(tmp, dest); else store->remove(tmp);
  }
  http.end();
  return ok;
}

static void importShelf(const char* cat) {
  setStatus(String("Looking at ") + cat + "…");
  bool music = !strcmp(cat, "Music"), video = !strcmp(cat, "Videos");
  const char* kind = music ? "album-art" : video ? "video-thumbs" : "file-thumbs";
  const char* key = music ? "artUrl" : "thumbUrl";
  String dir = String(DATA_ROOT "/") + kind;
  mkdirs(dir);
  std::vector<uint64_t> have = dirHashes(dir);

  // what to fetch: (file name, picture url) for files this board has and has no picture for
  std::vector<std::pair<String, String>> todo;
  {
    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(from + "/api/files?category=" + cat)) { setError("Couldn't reach " + from); return; }
    int code = http.GET();
    if (code != 200) { http.end(); setError(String("The Shelf at ") + from + " didn't answer (" + code + ")."); return; }
    Stream& s = http.getStream();
    JsonDocument filter; filter["name"] = true; filter[key] = true;
    if (!s.find('[')) { http.end(); return; }
    for (;;) {
      JsonDocument e;
      if (deserializeJson(e, s, DeserializationOption::Filter(filter))) break;
      String name = e["name"] | "", url = e[key] | "";
      if (name.length() && url.startsWith("/") && name.indexOf('/') < 0 && !name.startsWith(".")) {
        String tp = String(dir) + "/" + fnvHex(String(cat) + "/" + name) + ".jpg";
        if (hasHash(have, tp.substring(tp.lastIndexOf('/') + 1))) skipped++;
        else if (store->exists(String("/") + cat + "/" + name)) todo.push_back({name, url});
      }
      int c;                                              // on to the next one, or the end
      do { c = s.read(); if (c < 0) { uint32_t t = millis(); while (!s.available() && millis() - t < 3000) delay(5); c = s.read(); } } while (c >= 0 && c != ',' && c != ']');
      if (c != ',') break;
    }
    http.end();
  }
  int i = 0;
  for (auto& t : todo) {
    setStatus(String(cat) + ": " + (++i) + " of " + todo.size());
    String dest = String(dir) + "/" + fnvHex(String(cat) + "/" + t.first) + ".jpg";
    if (download(from + t.second, dest)) copied++; else failed++;
    delay(5);
  }
}

static void importTask(void*) {
  for (int i = 0; CATS[i] && !errorMsg[0]; i++) importShelf(CATS[i]);
  setStatus(errorMsg[0] ? "" : "Done");
  running = false;
  vTaskDelete(nullptr);
}

static String stateJson() {
  return String("{\"running\":") + (running ? "true" : "false") + ",\"status\":\"" + jsonEsc(status) + "\",\"error\":\"" + jsonEsc(errorMsg) +
         "\",\"copied\":" + copied + ",\"skipped\":" + skipped + ",\"failed\":" + failed + ",\"from\":\"" + jsonEsc(from) + "\"}";
}

bool handleThumbImportApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m) {
  if (url != "/api/thumbs/import") return false;
  if (m == HTTP_GET) { sendJson(r, 200, stateJson()); return true; }
  if (m != HTTP_POST) { sendErr(r, 405, "Use GET or POST"); return true; }
  if (running) { sendJson(r, 200, stateJson()); return true; }
  if (!staOK) { sendErr(r, 400, "The board needs to be on your home Wi-Fi to reach the computer."); return true; }
  JsonDocument b; parseBody(r, b);
  String f = b["from"] | "";
  f.trim();
  if (!f.startsWith("http://") && !f.startsWith("https://")) f = "http://" + f;
  while (f.endsWith("/")) f.remove(f.length() - 1);
  if (f.length() < 10) { sendErr(r, 400, "Type the address of The Shelf on your computer, like 10.0.0.80:8080."); return true; }
  from = f; copied = skipped = failed = 0; errorMsg[0] = 0; setStatus("Starting…"); running = true;
  if (xTaskCreatePinnedToCore(importTask, "thumbimp", 8192, nullptr, 1, nullptr, 1) != pdPASS) {
    running = false; sendErr(r, 500, "Not enough memory to start."); return true;
  }
  sendJson(r, 200, stateJson());
  return true;
}
