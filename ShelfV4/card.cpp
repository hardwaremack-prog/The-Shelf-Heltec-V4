// The Shelf for Heltec V4 - microSD card backup and restore over Wi-Fi.
//
//   GET  /api/board/card/info            files and bytes that a backup would hold
//   GET  /api/board/card/list            every file on the card: [[path, size], ...]
//   GET  /api/board/card/backup          the whole card as one .tar (streamed; the settings
//                                        page gzips it on the way to disk)
//   POST /api/board/card/file?path=/x/y  restore one file: the raw body becomes that file
//
// The tar is plain POSIX ustar, with GNU long-name entries for paths over 100 characters,
// so 7-Zip, Windows tar, macOS and Linux can all open it. The board's own temp folder is
// left out.
#include "shelf.h"

struct TarItem { String path; uint64_t size; uint64_t mtimeMs; };

// Walks the whole card one file at a time, holding only the folders it's inside (a few open
// handles), never a list of every file: a board without PSRAM has no room for that.
struct CardWalk {
  std::vector<File> stack;
  bool started = false;
  bool next(TarItem& it) {
    if (!started) { started = true; File root = store->open("/"); if (root && root.isDirectory()) stack.push_back(root); }
    while (!stack.empty()) {
      File e = stack.back().openNextFile();
      if (!e) { stack.back().close(); stack.pop_back(); continue; }
      String p = e.path();
      if (!p.startsWith("/")) p = "/" + p;
      if (p == TMP_ROOT || p.startsWith(TMP_ROOT "/")) { e.close(); continue; }
      if (e.isDirectory()) { if (stack.size() < 12) stack.push_back(e); else e.close(); continue; }
      it.path = p; it.size = e.size(); it.mtimeMs = (uint64_t)e.getLastWrite() * 1000ULL;
      e.close();
      return true;
    }
    return false;
  }
  ~CardWalk() { for (auto& f : stack) f.close(); }
};

static uint64_t pad512(uint64_t n) { return (n + 511) & ~(uint64_t)511; }
static size_t nameLen(const TarItem& it) { return it.path.length() - 1; }      // without the leading '/'
static uint64_t entryBytes(const TarItem& it) {
  uint64_t n = 512 + pad512(it.size);
  if (nameLen(it) > 100) n += 512 + pad512(nameLen(it) + 1);                  // GNU long name entry
  return n;
}

static void octal(char* dst, size_t width, uint64_t v) {                       // width includes the NUL
  dst[width - 1] = 0;
  for (int i = (int)width - 2; i >= 0; i--) { dst[i] = '0' + (v & 7); v >>= 3; }
}
static void header(uint8_t* h, const char* name, size_t nlen, uint64_t size, uint64_t mtime, char type) {
  memset(h, 0, 512);
  memcpy(h, name, std::min(nlen, (size_t)100));
  octal((char*)h + 100, 8, 0644); octal((char*)h + 108, 8, 0); octal((char*)h + 116, 8, 0);
  octal((char*)h + 124, 12, size); octal((char*)h + 136, 12, mtime);
  h[156] = type;
  memcpy(h + 257, "ustar", 6); memcpy(h + 263, "00", 2);
  memcpy(h + 265, "shelf", 5); memcpy(h + 297, "shelf", 5);
  memset(h + 148, ' ', 8);
  uint32_t sum = 0; for (int i = 0; i < 512; i++) sum += h[i];
  octal((char*)h + 148, 7, sum); h[155] = ' ';
}

struct TarStream {
  CardWalk walk; TarItem cur;
  uint8_t head[1536]; size_t headLen = 0, headPos = 0;   // long-name entry + header for the current file
  File f; uint64_t dataLeft = 0, padLeft = 0;
  bool inFile = false, done = false;
  size_t trailerLeft = 1024;
};

static size_t fillTar(TarStream& t, uint8_t* out, size_t maxLen) {
  size_t n = 0;
  while (n < maxLen) {
    if (t.headPos < t.headLen) {                          // header bytes
      size_t k = std::min(maxLen - n, t.headLen - t.headPos);
      memcpy(out + n, t.head + t.headPos, k); t.headPos += k; n += k; continue;
    }
    if (t.inFile && t.dataLeft) {                         // file bytes (zeros if the card won't give them)
      size_t want = (size_t)std::min<uint64_t>(maxLen - n, t.dataLeft);
      int got = t.f ? t.f.read(out + n, want) : -1;
      if (got <= 0) { memset(out + n, 0, want); got = want; }
      n += got; t.dataLeft -= got; continue;
    }
    if (t.inFile && t.padLeft) {
      size_t k = (size_t)std::min<uint64_t>(maxLen - n, t.padLeft);
      memset(out + n, 0, k); t.padLeft -= k; n += k; continue;
    }
    if (t.inFile) { if (t.f) t.f.close(); t.inFile = false; }
    if (!t.done && t.walk.next(t.cur)) {                  // start the next file
      const TarItem& it = t.cur;
      const char* name = it.path.c_str() + 1; size_t nl = nameLen(it);
      t.headLen = 0; t.headPos = 0;
      if (nl > 100) {
        header(t.head, "././@LongLink", 13, nl + 1, 0, 'L');
        size_t body = (size_t)pad512(nl + 1);
        memset(t.head + 512, 0, body); memcpy(t.head + 512, name, nl);
        t.headLen = 512 + body;
      }
      header(t.head + t.headLen, name, nl, it.size, it.mtimeMs / 1000, '0');
      t.headLen += 512;
      t.f = store->open(it.path, FILE_READ);
      t.dataLeft = it.size; t.padLeft = pad512(it.size) - it.size; t.inFile = true;
      continue;
    }
    t.done = true;
    if (t.trailerLeft) {                                  // two empty blocks end the archive
      size_t k = std::min(maxLen - n, t.trailerLeft);
      memset(out + n, 0, k); t.trailerLeft -= k; n += k; continue;
    }
    break;
  }
  return n;
}

static bool safePath(const String& p) {
  return p.startsWith("/") && p.length() > 1 && p.length() < 240 && p.indexOf("..") < 0 && p.indexOf("//") < 0 &&
         !p.endsWith("/") && !(p == TMP_ROOT || p.startsWith(TMP_ROOT "/"));
}

bool isCardFileUpload(const String& url) { return url == "/api/board/card/file"; }

bool handleCardApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m) {
  if (!url.startsWith("/api/board/card/")) return false;
  if (!onSD) { sendErr(r, 400, "No microSD card is fitted, so there's nothing to back up or restore."); return true; }

  if (url == "/api/board/card/info") {
    CardWalk w; TarItem it; uint32_t files = 0; uint64_t bytes = 0, tarBytes = 1024;
    while (w.next(it)) { files++; bytes += it.size; tarBytes += entryBytes(it); }
    sendJson(r, 200, String("{\"files\":") + files + ",\"bytes\":" + String((double)bytes, 0) + ",\"tarBytes\":" + String((double)tarBytes, 0) + "}");
    return true;
  }

  if (url == "/api/board/card/list") {                    // [[path, size], ...], sent as it's walked
    struct ListWalk { CardWalk w; String pend = "["; size_t pos = 0; bool first = true, end = false; };
    auto L = std::make_shared<ListWalk>();
    AsyncWebServerResponse* res = r->beginChunkedResponse("application/json",
      [L](uint8_t* out, size_t maxLen, size_t) -> size_t {
        size_t n = 0;
        while (n < maxLen) {
          if (L->pos < L->pend.length()) {
            size_t k = std::min(maxLen - n, L->pend.length() - L->pos);
            memcpy(out + n, L->pend.c_str() + L->pos, k); L->pos += k; n += k; continue;
          }
          if (L->end) break;
          L->pend = ""; L->pos = 0;
          TarItem it;
          if (L->w.next(it)) { L->pend = String(L->first ? "" : ",") + "[\"" + jsonEsc(it.path) + "\"," + String((double)it.size, 0) + "]"; L->first = false; }
          else { L->pend = "]"; L->end = true; }
        }
        return n;
      });
    res->addHeader("Cache-Control", "no-store");
    r->send(res);
    return true;
  }

  if (url == "/api/board/card/backup" && m == HTTP_GET) {
    uint64_t total = 1024;
    { CardWalk w; TarItem it; while (w.next(it)) total += entryBytes(it); }   // first pass: just the size
    auto t = std::make_shared<TarStream>();
    auto fill = [t](uint8_t* out, size_t maxLen, size_t) -> size_t { size_t n = fillTar(*t, out, maxLen); return n; };
    // Over 4 GB the size doesn't fit the web server's length field: send it in chunks instead
    // (the settings page then takes the size from /info for its progress bar).
    AsyncWebServerResponse* res = total < 0xFFFFFFF0ULL ? r->beginResponse("application/x-tar", (size_t)total, fill)
                                                        : r->beginChunkedResponse("application/x-tar", fill);
    char fn[64]; time_t now = time(nullptr); struct tm tmv; gmtime_r(&now, &tmv);
    if (now > 1700000000) strftime(fn, sizeof fn, "attachment; filename=\"shelf-card-%Y-%m-%d.tar\"", &tmv);
    else strlcpy(fn, "attachment; filename=\"shelf-card.tar\"", sizeof fn);
    res->addHeader("Content-Disposition", fn);
    res->addHeader("Cache-Control", "no-store");
    res->addHeader("X-Tar-Bytes", String((double)total, 0));
    r->send(res);
    return true;
  }

  if (url == "/api/board/card/file" && m == HTTP_POST) {
    String path = r->hasParam("path") ? r->getParam("path")->value() : "";
    if (!safePath(path)) { sendErr(r, 400, "Bad path"); return true; }
    Body* b = bodyOf(r);
    size_t len = b ? b->len : 0;
    mkdirs(path.substring(0, path.lastIndexOf('/')));
    bool ok;
    if (b && b->inFile) {                                  // big file: it was saved in the board's temp folder
      if (r->_tempFile) r->_tempFile.close();
      store->remove(path);
      ok = store->rename(b->path, path);
    } else {
      store->remove(path);
      File f = store->open(path, FILE_WRITE);
      uint32_t synced = 0;
      ok = f && (len == 0 || writeBlockRetry(f, path.c_str(), (const uint8_t*)b->buf, len, synced));
      if (f) f.close();
    }
    File chk = store->open(path, FILE_READ);
    size_t got = chk ? chk.size() : 0; if (chk) chk.close();
    if (!ok || got != len) { store->remove(path); sendErr(r, 500, "The card didn't save " + path); return true; }
    invalidateCounts();
    if (path.startsWith("/Music/")) markMusicDirty();
    sendJson(r, 200, String("{\"ok\":true,\"size\":") + got + "}");
    return true;
  }
  sendErr(r, 404, "Not found");
  return true;
}
