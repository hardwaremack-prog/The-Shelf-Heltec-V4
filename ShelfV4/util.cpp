// The Shelf for Heltec V4 - helpers: names, JSON, request bodies, file responses
#include "shelf.h"
#include <esp_heap_caps.h>
#include <mbedtls/md5.h>
#include <sys/time.h>
#include "ff.h"
#include <algorithm>

const char* const CATEGORIES[6] = {"Pictures", "Music", "Videos", "Games", "Programs", "Books"};
bool isCategory(const String& c) { for (auto x : CATEGORIES) if (c == x) return true; return false; }

SpiRamAllocator psAlloc;
void* SpiRamAllocator::allocate(size_t n) {
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : malloc(n);
}
void SpiRamAllocator::deallocate(void* p) { free(p); }
void* SpiRamAllocator::reallocate(void* p, size_t n) {
  void* q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return q ? q : realloc(p, n);
}

// ------------------------------------------------------------ names & strings
String sanitizeFilename(String name) {
  int s = std::max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
  if (s >= 0) name = name.substring(s + 1);
  String o; o.reserve(name.length());
  for (size_t i = 0; i < name.length(); i++) {
    char c = name[i];
    if (strchr("/\\?%*:|\"<>", c) || (uint8_t)c < 0x20) o += '-'; else o += c;
  }
  o.trim();
  if (o == "." || o == "..") o = "";
  while (o.startsWith(".")) o.remove(0, 1);      // no hidden files from uploads
  return o.length() ? o : String("file");
}

String extOf(const String& n) {
  int i = n.lastIndexOf('.');
  if (i < 0 || i == (int)n.length() - 1) return "";
  String e = n.substring(i + 1); e.toLowerCase(); return e;
}

static bool inList(const String& e, const char* const* list) { for (; *list; list++) if (e == *list) return true; return false; }
static const char* const PIC[] = {"jpg","jpeg","png","gif","webp","bmp","svg","heic",nullptr};
static const char* const MUS[] = {"mp3","wav","ogg","flac","m4a","aac","mid","midi","kar","rmi",nullptr};
static const char* const VID[] = {"mp4","webm","mov","avi","mkv","m4v","ogv","3gp",nullptr};
String fileKind(const String& name) {
  String e = extOf(name);
  if (inList(e, PIC)) return "image";
  if (inList(e, MUS)) return "audio";
  if (inList(e, VID)) return "video";
  if (e == "pdf") return "pdf";
  return "other";
}

String humanSize(uint64_t bytes) {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double n = bytes; int i = 0;
  while (n >= 1024 && i < 4) { n /= 1024; i++; }
  char b[24];
  snprintf(b, sizeof b, (n >= 10 || i == 0) ? "%.0f %s" : "%.1f %s", n, units[i]);
  return b;
}

String jsonEsc(const String& s) {
  String o; o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += c;
    }
  }
  return o;
}

String uuid4() {
  uint8_t b[16];
  for (int i = 0; i < 16; i += 4) { uint32_t r = esp_random(); memcpy(b + i, &r, 4); }
  b[6] = (b[6] & 0x0f) | 0x40; b[8] = (b[8] & 0x3f) | 0x80;
  char s[37];
  snprintf(s, sizeof s, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15]);
  return s;
}

String md5hex(const String& s) {
  uint8_t d[16];
  mbedtls_md5((const unsigned char*)s.c_str(), s.length(), d);
  char h[33]; for (int i = 0; i < 16; i++) sprintf(h + i * 2, "%02x", d[i]);
  return h;
}

String fnvHex(const String& s) {
  uint64_t h = 1469598103934665603ULL;
  for (size_t i = 0; i < s.length(); i++) { h ^= (uint8_t)s[i]; h *= 1099511628211ULL; }
  char b[17]; snprintf(b, sizeof b, "%016llx", (unsigned long long)h); return b;
}

bool isValidId(const String& id) {
  if (id.length() < 1 || id.length() > 100) return false;
  for (size_t i = 0; i < id.length(); i++) { char c = id[i]; if (!isalnum((unsigned char)c) && c != '-') return false; }
  return true;
}

uint64_t nowMs() {
  struct timeval tv; gettimeofday(&tv, nullptr);
  return (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
}

bool mkdirs(const String& path) {
  if (path.length() <= 1) return true;
  if (store->exists(path)) return true;
  int i = path.lastIndexOf('/');
  if (i > 0 && !mkdirs(path.substring(0, i))) return false;
  return store->mkdir(path);
}

bool removeAll(const String& path) {
  File f = store->open(path);
  if (!f) return false;
  if (!f.isDirectory()) { f.close(); return store->remove(path); }
  std::vector<std::pair<String, bool>> kids;
  File c = f.openNextFile();
  while (c) { kids.push_back({String(c.path()), c.isDirectory()}); c.close(); c = f.openNextFile(); }
  f.close();
  for (auto& k : kids) { if (k.second) removeAll(k.first); else store->remove(k.first); }
  return store->rmdir(path);
}

bool copyFile(const String& from, const String& to) {
  File a = store->open(from, FILE_READ); if (!a) return false;
  File b = store->open(to, FILE_WRITE); if (!b) { a.close(); return false; }
  static uint8_t buf[4096];
  bool ok = true;
  while (a.available()) { int n = a.read(buf, sizeof buf); if (n <= 0) break; if (b.write(buf, n) != (size_t)n) { ok = false; break; } }
  a.close(); b.close();
  if (!ok) store->remove(to);
  return ok;
}

bool moveFile(const String& from, const String& to) {
  if (!store->exists(from)) return false;
  if (store->exists(to)) store->remove(to);
  if (store->rename(from, to)) return true;
  return copyFile(from, to) && store->remove(from);
}

// "name.ext" -> "name (1).ext" if taken, like The Shelf does.
String uniqueName(const String& dir, const String& name) {
  if (!store->exists(dir + "/" + name)) return name;
  int dot = name.lastIndexOf('.');
  String base = dot > 0 ? name.substring(0, dot) : name, ext = dot > 0 ? name.substring(dot) : "";
  for (int i = 1; i < 10000; i++) {
    String n = base + " (" + i + ")" + ext;
    if (!store->exists(dir + "/" + n)) return n;
  }
  return name;
}

const char* mimeFor(const String& p) {
  String e = extOf(p);
  struct M { const char* e; const char* m; };
  static const M map[] = {
    {"html","text/html; charset=utf-8"},{"htm","text/html; charset=utf-8"},{"js","text/javascript"},{"mjs","text/javascript"},
    {"css","text/css"},{"json","application/json"},{"txt","text/plain; charset=utf-8"},{"md","text/plain; charset=utf-8"},
    {"csv","text/csv"},{"xml","application/xml"},{"svg","image/svg+xml"},{"png","image/png"},{"jpg","image/jpeg"},
    {"jpeg","image/jpeg"},{"gif","image/gif"},{"webp","image/webp"},{"bmp","image/bmp"},{"ico","image/x-icon"},
    {"heic","image/heic"},{"avif","image/avif"},{"mp3","audio/mpeg"},{"mid","audio/midi"},{"midi","audio/midi"},{"kar","audio/midi"},{"rmi","audio/midi"},{"m4a","audio/mp4"},{"aac","audio/aac"},
    {"wav","audio/wav"},{"ogg","audio/ogg"},{"flac","audio/flac"},{"mp4","video/mp4"},{"m4v","video/mp4"},
    {"webm","video/webm"},{"mov","video/quicktime"},{"ogv","video/ogg"},{"mkv","video/x-matroska"},{"avi","video/x-msvideo"},
    {"3gp","video/3gpp"},{"pdf","application/pdf"},{"zip","application/zip"},{"wasm","application/wasm"},
    {"woff","font/woff"},{"woff2","font/woff2"},{"ttf","font/ttf"},{"otf","font/otf"},{"data","application/octet-stream"},
  };
  for (auto& m : map) if (e == m.e) return m.m;
  return "application/octet-stream";
}

// ------------------------------------------------------------ JSON files
bool loadJsonFile(const String& path, JsonDocument& doc) {
  File f = store->open(path, FILE_READ);
  if (!f) return false;
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  return !e;
}

bool saveJsonFile(const String& path, JsonDocument& doc) {
  String tmp = path + ".tmp";
  File f = store->open(tmp, FILE_WRITE);
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  store->remove(path);
  return store->rename(tmp, path);
}

// ------------------------------------------------------------ bodies
Body* bodyOf(AsyncWebServerRequest* r) {
  Body* b = (Body*)r->_tempObject;
  return (b && b->magic == BODY_MAGIC) ? b : nullptr;
}

// A reader over a body held in RAM or in its spool file.
struct BodyReader {
  // Spooled bodies are read 4 KB at a time: byte-by-byte reads from the card ran at ~33 KB/s.
  Body* b; size_t pos = 0; File f;
  uint8_t* rb = nullptr; size_t rbLen = 0, rbPos = 0;
  explicit BodyReader(Body* body) : b(body) {
    if (b && b->inFile) { f = store->open(b->path, FILE_READ); rb = (uint8_t*)malloc(4096); }
  }
  ~BodyReader() { if (f) f.close(); free(rb); }
  bool ok() const { return b && (!b->inFile || (f && rb)); }
  int get() {
    if (!b) return -1;
    if (!b->inFile) return pos < b->len ? (uint8_t)b->buf[pos++] : -1;
    if (rbPos >= rbLen) { int k = f.read(rb, 4096); if (k <= 0) return -1; rbLen = k; rbPos = 0; }
    pos++; return rb[rbPos++];
  }
  size_t readMany(char* out, size_t n) {          // for ArduinoJson's readBytes
    size_t i = 0;
    while (i < n) {
      if (rbPos >= rbLen) { int k = f.read(rb, 4096); if (k <= 0) break; rbLen = k; rbPos = 0; }
      size_t m = std::min(n - i, rbLen - rbPos);
      memcpy(out + i, rb + rbPos, m); rbPos += m; i += m; pos += m;
    }
    return i;
  }
};

// Stream adapter so ArduinoJson can read a body.
struct BodyStream : public Stream {
  BodyReader rd; int peeked = -2;
  explicit BodyStream(Body* b) : rd(b) {}
  int available() override { return peeked >= 0 || (rd.b && rd.pos < rd.b->len) ? 1 : 0; }
  int read() override { if (peeked != -2) { int c = peeked; peeked = -2; return c; } return rd.get(); }
  int peek() override { if (peeked == -2) peeked = rd.get(); return peeked; }
  size_t write(uint8_t) override { return 0; }
  size_t readBytes(char* buf, size_t n) override {
    size_t i = 0;
    if (n && peeked != -2) { if (peeked < 0) return 0; buf[i++] = (char)peeked; peeked = -2; }
    if (rd.b && !rd.b->inFile) {
      size_t left = rd.b->len - rd.pos, k = std::min(n - i, left);
      memcpy(buf + i, rd.b->buf + rd.pos, k); rd.pos += k; return i + k;
    }
    if (rd.f) i += rd.readMany(buf + i, n - i);
    return i;
  }
};

bool parseBody(AsyncWebServerRequest* r, JsonDocument& doc, JsonDocument* filter) {
  Body* b = bodyOf(r);
  if (!b || b->tooBig || b->len == 0) return false;
  BodyStream s(b);
  if (!s.rd.ok()) return false;
  DeserializationError e = filter
      ? deserializeJson(doc, s, DeserializationOption::Filter(*filter), DeserializationOption::NestingLimit(40))
      : deserializeJson(doc, s, DeserializationOption::NestingLimit(40));
  return !e;
}

size_t bodyLen(AsyncWebServerRequest* r) { Body* b = bodyOf(r); return b ? b->len : 0; }

bool bodyContains(AsyncWebServerRequest* r, const char* needle) {
  Body* b = bodyOf(r);
  if (!b || b->tooBig) return false;
  BodyReader rd(b);
  if (!rd.ok()) return false;
  size_t m = 0, n = strlen(needle); int c;
  while ((c = rd.get()) >= 0) {
    if (c == needle[m]) { if (++m == n) return true; }
    else m = (c == needle[0]) ? 1 : 0;
  }
  return false;
}

bool bodyValidObject(AsyncWebServerRequest* r) {
  JsonDocument filter(&psAlloc); filter.to<JsonObject>();   // keep nothing, just check the syntax
  JsonDocument doc(&psAlloc);
  if (!parseBody(r, doc, &filter)) return false;
  return doc.is<JsonObject>();
}

static int b64val(int c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+' || c == '-') return 62;
  if (c == '/' || c == '_') return 63;
  return -1;
}

bool bodyDataUrlToFile(AsyncWebServerRequest* r, const char* key, const String& outPath, size_t maxBytes, String* mimeOut) {
  Body* b = bodyOf(r);
  if (!b || b->tooBig) return false;
  BodyReader rd(b);
  if (!rd.ok()) return false;
  String pat = String("\"") + key + "\"";
  size_t m = 0; int c;
  // find "key"
  while ((c = rd.get()) >= 0) {
    if (c == pat[m]) { if (++m == pat.length()) break; }
    else m = (c == pat[0]) ? 1 : 0;
  }
  if (c < 0) return false;
  // skip : and whitespace, expect "data:
  while ((c = rd.get()) >= 0 && (c == ' ' || c == ':' || c == '\n' || c == '\r' || c == '\t')) {}
  if (c != '"') return false;
  String head;
  while ((c = rd.get()) >= 0 && c != ',' && c != '"' && head.length() < 80) head += (char)c;
  if (c != ',' || !head.startsWith("data:") || !head.endsWith(";base64")) return false;
  if (mimeOut) *mimeOut = head.substring(5, head.length() - 7);
  File out = store->open(outPath, FILE_WRITE);
  if (!out) return false;
  uint8_t obuf[768]; size_t on = 0, total = 0;
  uint32_t acc = 0; int bits = 0; bool ok = true;
  while ((c = rd.get()) >= 0 && c != '"') {
    if (c == '\\') continue;                 // "\/" escapes from some encoders
    int v = b64val(c);
    if (v < 0) continue;                     // '=' padding, whitespace
    acc = (acc << 6) | v; bits += 6;
    if (bits >= 8) {
      bits -= 8; obuf[on++] = (acc >> bits) & 0xff;
      if (on == sizeof obuf) { if (out.write(obuf, on) != on) { ok = false; break; } total += on; on = 0; }
      if (total > maxBytes) { ok = false; break; }
    }
  }
  if (ok && on) { ok = out.write(obuf, on) == on; total += on; }
  out.close();
  if (c != '"' || !ok || total == 0 || total > maxBytes) { store->remove(outPath); return false; }
  return true;
}

// Copies a JSON object (from a reader) to outPath, swapping its closing brace
// for `tail` (which should start with "," and end with "}").
static bool copyObjectWithTail(BodyReader& rd, size_t len, const String& outPath, const String& tail) {
  // find the last '}' and whether the object is empty
  File out = store->open(outPath, FILE_WRITE);
  if (!out) return false;
  // Pass 1 is avoided: we stream everything, holding back trailing whitespace and the final '}'.
  uint8_t buf[1024]; size_t n = 0; bool seenContent = false, ok = true;
  String held;                // trailing "}" + whitespace held back
  int c; bool started = false;
  while ((c = rd.get()) >= 0) {
    if (!started) { if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue; if (c != '{') { ok = false; break; } started = true; buf[n++] = '{'; continue; }
    if (c == '}' || c == ' ' || c == '\n' || c == '\r' || c == '\t') { held += (char)c; if (held.length() > 4096) { ok = false; break; } continue; }
    // flush what was held - it wasn't the end after all
    for (size_t i = 0; i < held.length(); i++) { buf[n++] = held[i]; if (n == sizeof buf) { out.write(buf, n); n = 0; } }
    held = "";
    seenContent = true;
    buf[n++] = (uint8_t)c;
    if (n == sizeof buf) { if (out.write(buf, n) != n) { ok = false; break; } n = 0; }
  }
  // held must end with the object's closing brace
  int lastBrace = held.lastIndexOf('}');
  if (ok && started && lastBrace >= 0) {
    for (int i = 0; i < lastBrace; i++) { buf[n++] = held[i]; if (n == sizeof buf) { out.write(buf, n); n = 0; } }
    if (n) out.write(buf, n);
    out.print(seenContent ? tail : tail.substring(1));   // "{" is already written; drop the leading ","
  } else ok = false;
  out.close();
  if (!ok) store->remove(outPath);
  return ok;
}

bool bodyWriteWithTail(AsyncWebServerRequest* r, const String& outPath, const String& tail) {
  Body* b = bodyOf(r);
  if (!b || b->tooBig || !b->len) return false;
  BodyReader rd(b);
  if (!rd.ok()) return false;
  String tmp = outPath + ".tmp";
  if (!copyObjectWithTail(rd, b->len, tmp, tail)) return false;
  store->remove(outPath);
  return store->rename(tmp, outPath);
}

bool fileWriteWithTail(const String& inPath, const String& outPath, const String& tail) {
  Body fake; memset(&fake, 0, sizeof fake); fake.magic = BODY_MAGIC; fake.inFile = true;
  strlcpy(fake.path, inPath.c_str(), sizeof fake.path);
  File t = store->open(inPath, FILE_READ); if (!t) return false; fake.len = t.size(); t.close();
  BodyReader rd(&fake);
  String tmp = outPath + ".tmp";
  if (!copyObjectWithTail(rd, fake.len, tmp, tail)) return false;
  store->remove(outPath);
  return store->rename(tmp, outPath);
}

// ------------------------------------------------------------ responses
void sendBody(AsyncWebServerRequest* r, int code, const char* type, String&& body) {
  auto buf = std::make_shared<String>(std::move(body));
  size_t len = buf->length();
  AsyncWebServerResponse* res = r->beginResponse(type, len, [buf](uint8_t* out, size_t maxLen, size_t index) -> size_t {
    if (index >= buf->length()) return 0;
    size_t n = std::min(maxLen, (size_t)buf->length() - index);
    memcpy(out, buf->c_str() + index, n);
    return n;
  });
  res->setCode(code);
  res->addHeader("Cache-Control", "no-store");
  r->send(res);
}
void sendJson(AsyncWebServerRequest* r, int code, const String& body) {
  if (body.length() > 4096) { String copy = body; sendBody(r, code, "application/json", std::move(copy)); return; }
  AsyncWebServerResponse* res = r->beginResponse(code, "application/json", body);
  res->addHeader("Cache-Control", "no-store");
  r->send(res);
}
void sendErr(AsyncWebServerRequest* r, int code, const String& msg) {
  sendJson(r, code, String("{\"error\":\"") + jsonEsc(msg) + "\"}");
}
void sendDoc(AsyncWebServerRequest* r, JsonDocument& doc, int code) {
  String s; serializeJson(doc, s); sendJson(r, code, s);
}

void sendJsonFile(AsyncWebServerRequest* r, const String& path) {
  if (!store->exists(path)) return sendErr(r, 404, "Not found");
  AsyncWebServerResponse* res = r->beginResponse(*store, path, "application/json");
  res->addHeader("Cache-Control", "no-store");
  r->send(res);
}

void sendFileRanged(AsyncWebServerRequest* r, const String& p, bool download) {
  File f = store->open(p, FILE_READ);
  if (!f || f.isDirectory()) { if (f) f.close(); r->send(404, "text/plain", "Not found"); return; }
  size_t size = f.size();
  size_t start = 0, end = size ? size - 1 : 0;
  bool ranged = false;
  if (r->hasHeader("Range") && size > 0) {
    String rg = r->header("Range");
    if (rg.startsWith("bytes=")) {
      rg = rg.substring(6);
      int dash = rg.indexOf('-');
      String a = rg.substring(0, dash), b = rg.substring(dash + 1);
      int comma = b.indexOf(','); if (comma >= 0) b = b.substring(0, comma);
      if (a.length() == 0) { size_t suf = strtoul(b.c_str(), nullptr, 10); start = suf >= size ? 0 : size - suf; }
      else { start = strtoul(a.c_str(), nullptr, 10); if (b.length()) end = strtoul(b.c_str(), nullptr, 10); }
      if (end >= size) end = size - 1;
      if (start > end) {
        f.close();
        AsyncWebServerResponse* res = r->beginResponse(416, "text/plain", "range");
        res->addHeader("Content-Range", String("bytes */") + size);
        return r->send(res);
      }
      ranged = true;
    }
  }
  size_t len = size ? end - start + 1 : 0;
  File fh = f; size_t base = start;
  AsyncWebServerResponse* res = r->beginResponse(mimeFor(p), len,
    [fh, base, len](uint8_t* buf, size_t maxLen, size_t index) mutable -> size_t {
      if (index >= len) return 0;
      size_t want = std::min(maxLen, len - index);
      if (fh.position() != base + index) fh.seek(base + index);
      int got = fh.read(buf, want);
      return got > 0 ? (size_t)got : 0;
    });
  res->addHeader("Accept-Ranges", "bytes");
  res->addHeader("Cache-Control", "no-cache");
  if (ranged) { res->setCode(206); res->addHeader("Content-Range", String("bytes ") + start + "-" + end + "/" + size); }
  if (download) {
    String n = p.substring(p.lastIndexOf('/') + 1); n.replace("\"", "'");
    res->addHeader("Content-Disposition", String("attachment; filename=\"") + n + "\"");
  }
  r->send(res);
}

struct ArrayStreamState {
  std::vector<String>* paths; std::vector<String>* tails;
  size_t idx = 0, emitted = 0, left = 0; File cur; int stage = 0;   // 0 "[", 1 next file, 2 body, 5 done
  String pending;
  bool hasTail() const { return tails && idx < tails->size() && (*tails)[idx].length(); }
  ~ArrayStreamState() { if (cur) cur.close(); delete paths; delete tails; }
};

void sendJsonArrayOfFiles(AsyncWebServerRequest* r, std::vector<String>* paths, std::vector<String>* tails) {
  auto st = std::make_shared<ArrayStreamState>();
  st->paths = paths; st->tails = tails;
  AsyncWebServerResponse* res = r->beginChunkedResponse("application/json",
    [st](uint8_t* buf, size_t maxLen, size_t) -> size_t {
      size_t n = 0;
      while (n < maxLen) {
        if (st->pending.length()) {
          size_t k = std::min(maxLen - n, (size_t)st->pending.length());
          memcpy(buf + n, st->pending.c_str(), k); n += k; st->pending.remove(0, k); continue;
        }
        if (st->stage == 5) break;
        if (st->stage == 0) { st->pending = "["; st->stage = 1; continue; }
        if (st->stage == 1) {
          while (st->idx < st->paths->size()) {
            st->cur = store->open((*st->paths)[st->idx], FILE_READ);
            if (st->cur && st->cur.size() > 1) break;
            if (st->cur) st->cur.close();
            st->idx++;
          }
          if (st->idx >= st->paths->size()) { st->pending = "]"; st->stage = 5; continue; }
          st->left = st->cur.size() - (st->hasTail() ? 1 : 0);
          if (st->emitted++ > 0) st->pending = ",";
          st->stage = 2;
          continue;
        }
        // stage 2: file body
        if (st->left == 0) {
          if (st->hasTail()) st->pending = (*st->tails)[st->idx];
          st->cur.close(); st->idx++; st->stage = 1; continue;
        }
        int got = st->cur.read(buf + n, std::min(maxLen - n, st->left));
        if (got <= 0) { st->left = 0; continue; }
        n += got; st->left -= got;
      }
      return n;
    });
  res->addHeader("Cache-Control", "no-store");
  r->send(res);
}

// ------------------------------------------------------------ fast folder listing
// The card (or the board's flash, when there's no card) is the only FAT volume mounted, so find
// its FatFs drive number once by looking for our own /.shelf folder.
static char fatDrive[4] = "";
static bool findDrive() {
  for (int i = 0; i < FF_VOLUMES; i++) {
    char p[16]; snprintf(p, sizeof p, "%d:/.shelf", i);
    FF_DIR d;
    if (f_opendir(&d, p) == FR_OK) { f_closedir(&d); snprintf(fatDrive, sizeof fatDrive, "%d:", i); return true; }
  }
  return false;
}
static uint64_t fatTimeMs(WORD fdate, WORD ftime) {
  struct tm t = {};
  t.tm_year = ((fdate >> 9) & 0x7F) + 80; t.tm_mon = ((fdate >> 5) & 0x0F) - 1; t.tm_mday = fdate & 0x1F;
  t.tm_hour = (ftime >> 11) & 0x1F; t.tm_min = (ftime >> 5) & 0x3F; t.tm_sec = (ftime & 0x1F) * 2;
  t.tm_isdst = -1;
  return (uint64_t)mktime(&t) * 1000ULL;
}
bool listDir(const String& path, std::vector<DirEnt>& out, size_t max) {
  if (!fatDrive[0] && !findDrive()) return false;
  String fp = String(fatDrive) + (path.startsWith("/") ? path : "/" + path);
  FF_DIR* d = (FF_DIR*)malloc(sizeof(FF_DIR));
  FILINFO* fi = (FILINFO*)malloc(sizeof(FILINFO));
  bool ok = d && fi && f_opendir(d, fp.c_str()) == FR_OK;
  if (ok) {
    while (out.size() < max) {
      if (f_readdir(d, fi) != FR_OK || !fi->fname[0]) break;
      if (fi->fname[0] == '.' && (!fi->fname[1] || (fi->fname[1] == '.' && !fi->fname[2]))) continue;
      out.push_back({String(fi->fname), (uint64_t)fi->fsize, fatTimeMs(fi->fdate, fi->ftime), (fi->fattrib & AM_DIR) != 0});
    }
    f_closedir(d);
  }
  free(d); free(fi);
  return ok;
}
std::vector<String> dirNames(const String& path) {
  std::vector<DirEnt> v; listDir(path, v);
  std::vector<String> n; n.reserve(v.size());
  for (auto& e : v) if (!e.dir) n.push_back(e.name);
  std::sort(n.begin(), n.end());
  return n;
}
bool hasName(const std::vector<String>& n, const String& name) { return std::binary_search(n.begin(), n.end(), name); }

// ------------------------------------------------------------ careful block writes
volatile uint32_t sdRetries = 0, sdFailures = 0;
bool writeBlockRetry(File& f, const char* path, const uint8_t* buf, size_t n, uint32_t& synced) {
  if (!n) return true;
  for (int t = 0; t < 4; t++) {
    if (t) {
      if (f) f.close();
      delay(200 * t);
      f = store->open(path, "r+");
      if (!f || !f.seek(synced)) continue;
      sdRetries++;
    }
    if (f && f.write(buf, n) == n) {
      f.flush();
      if (f.position() == synced + n) { synced += n; return true; }
    }
  }
  sdFailures++;
  return false;
}
