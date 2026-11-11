// The Shelf for Heltec V4 - the six shelves (Pictures, Music, Videos, Games, Programs, Books)
// Mirrors the file routes in The Shelf's server.js so its own web app works unchanged.
#include "shelf.h"
#include <esp32s3/rom/miniz.h>
#include <algorithm>

static const char* PLAYS_FILE = DATA_ROOT "/play-counts.json";
static const char* META_FILE  = DATA_ROOT "/music-meta.json";

static String catDir(const String& cat) { return "/" + cat; }
static String thumbPath(const char* kind, const String& cat, const String& name) {
  return String(DATA_ROOT "/") + kind + "/" + fnvHex(cat + "/" + name) + ".jpg";
}
static bool thumbable(const String& cat) { return cat == "Games" || cat == "Programs" || cat == "Books"; }

// ------------------------------------------------------------ category counts (cached)
static int counts[6] = {-1, -1, -1, -1, -1, -1};
void invalidateCounts() { for (int& c : counts) c = -1; statsDirty = true; }
int countCategory(const String& cat) {
  for (int i = 0; i < 6; i++) if (cat == CATEGORIES[i]) {
    if (counts[i] >= 0) return counts[i];
    int n = 0;
    std::vector<DirEnt> v; listDir(catDir(cat), v);
    for (auto& e : v) if (!e.dir && !e.name.startsWith(".")) n++;
    return counts[i] = n;
  }
  return 0;
}

// ------------------------------------------------------------ play counts
static int bumpPlay(const String& cat, const String& name) {
  FsGuard g;
  JsonDocument doc(&psAlloc);
  if (!loadJsonFile(PLAYS_FILE, doc) || !doc.is<JsonObject>()) doc.to<JsonObject>();
  String key = cat + "/" + name;
  int n = (doc[key] | 0) + 1;
  doc[key] = n;
  saveJsonFile(PLAYS_FILE, doc);
  return n;
}

// Moves everything The Shelf remembers about a file (plays, thumbnails, art, game cache)
// when it's renamed, moved or deleted. to* empty = forget it.
static void migrateExtras(const String& fc, const String& fn, const String& tc, const String& tn) {
  FsGuard g;
  JsonDocument doc(&psAlloc);
  if (loadJsonFile(PLAYS_FILE, doc) && doc.is<JsonObject>()) {
    String k = fc + "/" + fn;
    if (doc[k].is<int>()) {
      int v = doc[k]; doc.remove(k);
      if (tc.length()) doc[tc + "/" + tn] = v;
      saveJsonFile(PLAYS_FILE, doc);
    }
  }
  for (const char* kind : {"video-thumbs", "file-thumbs", "album-art"}) {
    String a = thumbPath(kind, fc, fn);
    if (!store->exists(a)) continue;
    if (tc.length()) moveFile(a, thumbPath(kind, tc, tn)); else store->remove(a);
  }
  String cache = String(PLAY_ROOT "/") + md5hex(fc + "/" + fn);
  if (store->exists(cache)) removeAll(cache);
  if (fc == "Music" || tc == "Music") markMusicDirty();
  invalidateCounts();
}

// ------------------------------------------------------------ listing
struct Entry { String name; uint64_t size; uint64_t mt; };

static void listFiles(AsyncWebServerRequest* r, const String& cat) {
  std::vector<Entry> v;
  {
    std::vector<DirEnt> all; listDir(catDir(cat), all);
    for (auto& e : all) if (!e.dir && !e.name.startsWith(".")) v.push_back({e.name, e.size, e.mtimeMs});
  }
  // which songs/videos/games already have art or a thumbnail: one look at each folder
  std::vector<String> haveArt, haveThumb;
  if (cat == "Music") haveArt = dirNames(DATA_ROOT "/album-art");
  if (cat == "Videos") haveThumb = dirNames(DATA_ROOT "/video-thumbs");
  else if (thumbable(cat)) haveThumb = dirNames(DATA_ROOT "/file-thumbs");
  std::sort(v.begin(), v.end(), [](const Entry& a, const Entry& b) { return a.mt > b.mt; });

  JsonDocument plays(&psAlloc);
  loadJsonFile(PLAYS_FILE, plays);
  JsonDocument meta(&psAlloc);
  if (cat == "Music") loadJsonFile(META_FILE, meta);

  String body; body.reserve(v.size() * 420 + 16);
  StrOut so(body); StrOut* out = &so;
  out->print("[");
  bool first = true;
  for (auto& e : v) {
    String enc = e.name; // URL-encode for the url field
    String url = "/shared/" + cat + "/";
    for (size_t i = 0; i < enc.length(); i++) {
      uint8_t c = enc[i];
      if (isalnum(c) || strchr("-_.!~*'()", c)) url += (char)c;
      else { char b[4]; snprintf(b, sizeof b, "%%%02X", c); url += b; }
    }
    String key = cat + "/" + e.name;
    int pc = plays[key] | 0;
    out->printf("%s{\"name\":\"%s\",\"size\":%llu,\"sizeLabel\":\"%s\",\"modified\":%llu,\"added\":%llu,"
                "\"kind\":\"%s\",\"ext\":\"%s\",\"url\":\"%s\",\"plays\":%d",
                first ? "" : ",", jsonEsc(e.name).c_str(), (unsigned long long)e.size, humanSize(e.size).c_str(),
                (unsigned long long)e.mt, (unsigned long long)e.mt, fileKind(e.name).c_str(), jsonEsc(extOf(e.name)).c_str(),
                jsonEsc(url).c_str(), pc);
    if (cat == "Music") {
      String art = thumbPath("album-art", cat, e.name);
      if (hasName(haveArt, art.substring(art.lastIndexOf('/') + 1))) out->printf(",\"artUrl\":\"/data-files/album-art/%s?v=%llu\"", art.substring(art.lastIndexOf('/') + 1).c_str(), (unsigned long long)e.mt);
      else out->print(",\"artUrl\":null");
      const char* a = meta[e.name]["a"] | (const char*)nullptr;
      if (a && *a) out->printf(",\"artist\":\"%s\"", jsonEsc(a).c_str()); else out->print(",\"artist\":null");
    }
    if (cat == "Videos" || thumbable(cat)) {
      const char* kind = cat == "Videos" ? "video-thumbs" : "file-thumbs";
      String t = thumbPath(kind, cat, e.name);
      if (hasName(haveThumb, t.substring(t.lastIndexOf('/') + 1))) out->printf(",\"thumbUrl\":\"/data-files/%s/%s?v=%llu\"", kind, t.substring(t.lastIndexOf('/') + 1).c_str(), (unsigned long long)nowMs() / 60000);
      else out->print(",\"thumbUrl\":null");
    }
    out->print("}");
    first = false;
  }
  out->print("]");
  sendBody(r, 200, "application/json", std::move(body));
}

// ------------------------------------------------------------ uploads (multipart)

static UploadCtx* uploadOf(AsyncWebServerRequest* r) {
  UploadCtx* u = (UploadCtx*)r->_tempObject;
  return (u && u->magic == UPLOAD_MAGIC) ? u : nullptr;
}

// Writes whatever is waiting in the buffer, in one big block (far quicker on an SD card than
// the ~1.4 KB pieces the network hands us), then makes it stick so the file's size on the card
// is up to date. If the card refuses the block, reopen the file and put the block back where
// it belongs, up to 3 more times, before giving up on the upload.
static bool uploadFlush(AsyncWebServerRequest* r, UploadCtx* u) {
  if (!u->fill) return true;
  bool ok = writeBlockRetry(r->_tempFile, u->full, u->buf, u->fill, u->synced);
  if (ok) u->retries = 0;
  u->fill = 0;
  return ok;
}
static void uploadFail(AsyncWebServerRequest* r, UploadCtx* u, uint8_t err) {
  u->err = err; u->fill = 0;
  if (r->_tempFile) r->_tempFile.close();
  if (u->full[0]) store->remove(u->full);
}

void handleUploadPart(AsyncWebServerRequest* r, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
  UploadCtx* u = uploadOf(r);
  if (!u) {
    if (r->_tempObject) return;
    u = (UploadCtx*)calloc(1, sizeof(UploadCtx) + UPLOAD_BUF);
    if (!u) return;
    u->magic = UPLOAD_MAGIC;
    u->media = r->url() == "/api/portal/posts/media";
    r->_tempObject = u;
    // The web server drops a connection after 3 s without new data. A big upload can pause
    // longer than that while the card catches up, so give it 30 s.
    if (r->client()) r->client()->setRxTimeout(30);
  }
  if (index == 0) {
    if (r->_tempFile) { uploadFlush(r, u); r->_tempFile.close(); }
    if (u->err) return;
    String dir, name;
    if (u->media) {
      String e = extOf(filename);
      String kind = fileKind(filename);
      if (kind != "image" && kind != "video") { u->err = 3; return; }
      if (e == "svg") { u->err = 3; return; }
      strlcpy(u->mediaType, kind == "video" ? "video" : "image", sizeof u->mediaType);
      dir = DATA_ROOT "/portal-media";
      name = uuid4() + "." + (e.length() ? e : String(kind == "video" ? "webm" : "png"));
    } else {
      String cat = r->hasParam("category", true) ? r->getParam("category", true)->value() : "";
      if (!isCategory(cat)) { u->err = 1; return; }
      strlcpy(u->cat, cat.c_str(), sizeof u->cat);
      dir = catDir(cat);
      name = uniqueName(dir, sanitizeFilename(filename));
    }
    mkdirs(dir);
    strlcpy(u->saved, name.c_str(), sizeof u->saved);
    u->fill = 0; u->synced = 0;
    strlcpy(u->full, (dir + "/" + name).c_str(), sizeof u->full);
    r->_tempFile = store->open(u->full, FILE_WRITE);
    if (!r->_tempFile) { u->err = 4; return; }
  }
  if (u->err || !r->_tempFile) return;
  while (len) {
    size_t n = std::min(len, (size_t)UPLOAD_BUF - u->fill);
    memcpy(u->buf + u->fill, data, n); u->fill += n; data += n; len -= n;
    if (u->fill == UPLOAD_BUF && !uploadFlush(r, u)) { uploadFail(r, u, 2); return; }
  }
  if (final) {
    if (!uploadFlush(r, u)) { uploadFail(r, u, 2); return; }
    r->_tempFile.close(); u->count++;
  }
}

void filesUploadDone(AsyncWebServerRequest* r) {
  UploadCtx* u = uploadOf(r);
  if (!u || (!u->count && !u->err)) return sendErr(r, 400, "No file was received.");
  if (u->err == 1) return sendErr(r, 400, "Invalid category");
  if (u->err == 4) return sendErr(r, 500, "Couldn't create the file on the " + String(onSD ? "microSD card. Check the card's wiring, or try a shorter file name." : "board's flash."));
  if (u->err) return sendErr(r, 500, onSD ? "Writing to the microSD card failed partway. Check that the card isn't full and the wires are snug, then try again."
                                          : "Couldn't save the file -- the board's storage is full. Fit a microSD card for more room.");
  invalidateCounts();
  if (String(u->cat) == "Music") markMusicDirty();
  sendJson(r, 200, String("{\"uploaded\":[\"") + jsonEsc(u->saved) + "\"]}");
}

// ------------------------------------------------------------ zipped HTML games
// A tiny unzip (stored + deflate) using the inflate that's built into the ESP32-S3 ROM.
static uint32_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static bool inflateEntry(File& zf, File& out, size_t compSize) {
  tinfl_decompressor* d = (tinfl_decompressor*)heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_8BIT);
  uint8_t* dict = (uint8_t*)psAlloc.allocate(TINFL_LZ_DICT_SIZE);
  uint8_t* in = (uint8_t*)malloc(4096);
  bool ok = d && dict && in;
  if (ok) {
    tinfl_init(d);
    size_t inAvail = 0, inPos = 0, dictOfs = 0, left = compSize; uint32_t spins = 0;
    for (;;) {
      if (inAvail == 0 && left) { int n = zf.read(in, std::min((size_t)4096, left)); if (n <= 0) { ok = false; break; } inAvail = n; inPos = 0; left -= n; }
      size_t inBytes = inAvail, outBytes = TINFL_LZ_DICT_SIZE - dictOfs;
      tinfl_status st = tinfl_decompress(d, in + inPos, &inBytes, dict, dict + dictOfs, &outBytes,
                                         left ? TINFL_FLAG_HAS_MORE_INPUT : 0);
      inPos += inBytes; inAvail -= inBytes;
      if (outBytes && out.write(dict + dictOfs, outBytes) != outBytes) { ok = false; break; }
      dictOfs = (dictOfs + outBytes) & (TINFL_LZ_DICT_SIZE - 1);
      if (st == TINFL_STATUS_DONE) break;
      if ((++spins & 31) == 0) delay(1);
      if (st < 0) { ok = false; break; }
      if (st == TINFL_STATUS_NEEDS_MORE_INPUT && !left && !inAvail) { ok = false; break; }
    }
  }
  free(d); psAlloc.deallocate(dict); free(in);
  return ok;
}

static bool unzipTo(const String& zipPath, const String& destDir) {
  File zf = store->open(zipPath, FILE_READ);
  if (!zf) return false;
  size_t size = zf.size();
  if (size < 22) { zf.close(); return false; }
  // find the End Of Central Directory record
  size_t scan = std::min(size, (size_t)(65535 + 22));
  uint8_t* tail = (uint8_t*)psAlloc.allocate(scan);
  if (!tail) { zf.close(); return false; }
  zf.seek(size - scan); zf.read(tail, scan);
  int eocd = -1;
  for (int i = scan - 22; i >= 0; i--) if (rd32(tail + i) == 0x06054b50) { eocd = i; break; }
  if (eocd < 0) { psAlloc.deallocate(tail); zf.close(); return false; }
  uint32_t entries = rd16(tail + eocd + 10), cdSize = rd32(tail + eocd + 12), cdOfs = rd32(tail + eocd + 16);
  psAlloc.deallocate(tail);
  if (cdOfs + cdSize > size) { zf.close(); return false; }
  uint8_t* cd = (uint8_t*)psAlloc.allocate(cdSize);
  if (!cd) { zf.close(); return false; }
  zf.seek(cdOfs); zf.read(cd, cdSize);
  bool ok = true;
  size_t p = 0;
  for (uint32_t i = 0; i < entries && p + 46 <= cdSize; i++) {
    if (rd32(cd + p) != 0x02014b50) { ok = false; break; }
    uint32_t method = rd16(cd + p + 10), csize = rd32(cd + p + 20), nlen = rd16(cd + p + 28),
             xlen = rd16(cd + p + 30), clen = rd16(cd + p + 32), lho = rd32(cd + p + 42);
    String name; name.reserve(nlen);
    for (uint32_t k = 0; k < nlen && p + 46 + k < cdSize; k++) name += (char)cd[p + 46 + k];
    p += 46 + nlen + xlen + clen;
    name.replace("\\", "/");
    if (name.endsWith("/") || name.indexOf("..") >= 0 || name.startsWith("/") || name.startsWith("__MACOSX")) continue;
    String outPath = destDir + "/" + name;
    mkdirs(outPath.substring(0, outPath.lastIndexOf('/')));
    uint8_t lh[30];
    zf.seek(lho);
    if (zf.read(lh, 30) != 30 || rd32(lh) != 0x04034b50) { ok = false; break; }
    zf.seek(lho + 30 + rd16(lh + 26) + rd16(lh + 28));
    File out = store->open(outPath, FILE_WRITE);
    if (!out) { ok = false; break; }
    bool good;
    if (method == 0) {
      static uint8_t buf[4096]; size_t left = csize; good = true;
      while (left) { int n = zf.read(buf, std::min(left, sizeof buf)); if (n <= 0 || out.write(buf, n) != (size_t)n) { good = false; break; } left -= n; }
    } else if (method == 8) good = inflateEntry(zf, out, csize);
    else good = false;
    out.close();
    if (!good) { ok = false; break; }
    delay(1);
  }
  psAlloc.deallocate(cd);
  zf.close();
  return ok;
}

static String findEntryHtml(const String& dir, int depth = 0) {
  if (depth > 6) return "";
  File d = store->open(dir);
  if (!d || !d.isDirectory()) return "";
  std::vector<String> files, dirs;
  File c = d.openNextFile();
  while (c) {
    String n = c.name();
    if (c.isDirectory()) { if (!n.startsWith(".") && n != "__MACOSX") dirs.push_back(n); }
    else files.push_back(n);
    c.close(); c = d.openNextFile();
  }
  d.close();
  for (auto& f : files) { String l = f; l.toLowerCase(); if (l == "index.html") return dir + "/" + f; }
  for (auto& f : files) { String l = f; l.toLowerCase(); if (l.endsWith(".html")) return dir + "/" + f; }
  std::sort(dirs.begin(), dirs.end());
  for (auto& s : dirs) { String x = findEntryHtml(dir + "/" + s, depth + 1); if (x.length()) return x; }
  return "";
}

static String encodePathSegments(const String& rel) {
  String o;
  for (size_t i = 0; i < rel.length(); i++) {
    uint8_t c = rel[i];
    if (isalnum(c) || strchr("-_.!~*'()/", c)) o += (char)c;
    else { char b[4]; snprintf(b, sizeof b, "%%%02X", c); o += b; }
  }
  return o;
}

static void playInfo(AsyncWebServerRequest* r) {
  String cat = r->hasParam("category") ? r->getParam("category")->value() : "";
  if (!isCategory(cat)) return sendErr(r, 400, "Invalid category");
  String name = sanitizeFilename(r->hasParam("name") ? r->getParam("name")->value() : "");
  String path = catDir(cat) + "/" + name;
  if (!store->exists(path)) return sendErr(r, 404, "File not found");
  String e = extOf(name);
  if (e == "html" || e == "htm") return sendJson(r, 200, "{\"url\":\"/shared/" + cat + "/" + jsonEsc(encodePathSegments(name)) + "\"}");
  if (e == "zip") {
    String id = md5hex(cat + "/" + name);
    String dest = String(PLAY_ROOT "/") + id;
    if (!store->exists(dest)) {
      mkdirs(dest);
      if (!unzipTo(path, dest)) { removeAll(dest); return sendErr(r, 500, "Could not open this zip file."); }
    }
    String entry = findEntryHtml(dest);
    if (!entry.length()) return sendErr(r, 400, "Couldn't find an index.html inside this zip.");
    String rel = entry.substring(dest.length() + 1);
    return sendJson(r, 200, "{\"url\":\"/play/" + id + "/" + jsonEsc(encodePathSegments(rel)) + "\"}");
  }
  sendErr(r, 400, "This file type can't be run in the app \xE2\x80\x94 try downloading it instead.");
}

// ------------------------------------------------------------ router
// url: /api/files..., /api/categories, /api/upload, /api/play-info
bool handleFilesApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m) {
  if (url == "/api/categories" && m == HTTP_GET) {
    String s = "[";
    for (int i = 0; i < 6; i++) { if (i) s += ","; s += String("{\"name\":\"") + CATEGORIES[i] + "\",\"count\":" + countCategory(CATEGORIES[i]) + "}"; }
    sendJson(r, 200, s + "]"); return true;
  }
  if (url == "/api/files" && m == HTTP_GET) {
    String cat = r->hasParam("category") ? r->getParam("category")->value() : "";
    if (!isCategory(cat)) { sendErr(r, 400, "Invalid category"); return true; }
    listFiles(r, cat); return true;
  }
  if (url == "/api/upload" && m == HTTP_POST) { filesUploadDone(r); return true; }
  if (url == "/api/play-info" && m == HTTP_GET) { playInfo(r); return true; }
  if (url == "/api/files/move" && m == HTTP_POST) {
    JsonDocument b(&psAlloc);
    if (!parseBody(r, b)) { sendErr(r, 400, "Invalid request"); return true; }
    String fc = b["fromCategory"] | "", tc = b["toCategory"] | "", nm = b["name"] | "";
    if (!isCategory(fc) || !isCategory(tc)) { sendErr(r, 400, "Invalid category"); return true; }
    if (fc == tc) { sendJson(r, 200, "{\"moved\":\"" + jsonEsc(nm) + "\"}"); return true; }
    String safe = sanitizeFilename(nm);
    if (!store->exists(catDir(fc) + "/" + safe)) { sendErr(r, 404, "File not found"); return true; }
    mkdirs(catDir(tc));
    String fin = uniqueName(catDir(tc), safe);
    if (!moveFile(catDir(fc) + "/" + safe, catDir(tc) + "/" + fin)) { sendErr(r, 500, "Could not move file"); return true; }
    migrateExtras(fc, safe, tc, fin);
    sendJson(r, 200, "{\"moved\":\"" + jsonEsc(fin) + "\",\"category\":\"" + tc + "\"}");
    return true;
  }
  if (!url.startsWith("/api/files/")) return false;

  // /api/files/:category/...
  String rest = url.substring(11);
  int s1 = rest.indexOf('/');
  String cat = s1 < 0 ? rest : rest.substring(0, s1);
  String tail = s1 < 0 ? "" : rest.substring(s1 + 1);
  if (!isCategory(cat)) { sendErr(r, 400, "Invalid category"); return true; }

  // whole-shelf actions
  if (tail == "video-thumbs" && m == HTTP_DELETE && cat == "Videos") {
    int n = 0; File d = store->open(DATA_ROOT "/video-thumbs");
    std::vector<String> v; if (d) { File c = d.openNextFile(); while (c) { v.push_back(c.path()); c.close(); c = d.openNextFile(); } d.close(); }
    for (auto& p : v) if (store->remove(p)) n++;
    sendJson(r, 200, String("{\"cleared\":") + n + "}"); return true;
  }
  if (tail == "file-thumbs" && m == HTTP_DELETE) {
    if (!thumbable(cat)) { sendErr(r, 400, "This shelf does not support thumbnails."); return true; }
    int n = 0; File d = store->open(catDir(cat));
    std::vector<String> v; if (d) { File c = d.openNextFile(); while (c) { v.push_back(c.name()); c.close(); c = d.openNextFile(); } d.close(); }
    for (auto& nm : v) if (store->remove(thumbPath("file-thumbs", cat, nm))) n++;
    sendJson(r, 200, String("{\"cleared\":") + n + "}"); return true;
  }
  if (tail == "album-art" && m == HTTP_DELETE) {
    sendErr(r, 501, "Album art comes from your songs' own tags on the board, so there's nothing to clear."); return true;
  }
  if (tail == "fetch-album-art" || tail == "remove-duplicates" || tail == "standardize-names" || tail == "export" || tail == "download-all") {
    sendErr(r, 501, "That tool isn't available on the Heltec board. Use The Shelf on a computer for it."); return true;
  }

  // per-file actions: :name or :name/<action>
  String name, action;
  int s2 = tail.lastIndexOf('/');
  static const char* const ACTIONS[] = {"play", "edit", "thumbnail", "tags", "fetch-art", nullptr};
  if (s2 > 0) {
    String a = tail.substring(s2 + 1);
    for (int i = 0; ACTIONS[i]; i++) if (a == ACTIONS[i]) { action = a; name = tail.substring(0, s2); }
  }
  if (!action.length()) name = tail;
  String safe = sanitizeFilename(name);
  String path = catDir(cat) + "/" + safe;

  if (!action.length() && m == HTTP_DELETE) {
    if (!store->exists(path) || !store->remove(path)) { sendErr(r, 404, "File not found"); return true; }
    migrateExtras(cat, safe, "", "");
    sendJson(r, 200, "{\"deleted\":\"" + jsonEsc(safe) + "\"}");
    return true;
  }
  if (!action.length() && m == HTTP_PUT) {
    if (!store->exists(path)) { sendErr(r, 404, "File not found"); return true; }
    JsonDocument b(&psAlloc);
    String nn = parseBody(r, b) ? String(b["newName"] | "") : String("");
    nn.trim();
    if (!nn.length()) { sendErr(r, 400, "A new name is required."); return true; }
    String clean = sanitizeFilename(nn);
    String oe = extOf(safe);
    if (!extOf(clean).length() && oe.length()) clean += "." + oe;
    if (clean == safe) { sendJson(r, 200, "{\"renamed\":\"" + jsonEsc(safe) + "\"}"); return true; }
    String np = catDir(cat) + "/" + clean;
    String a = clean, bb = safe; a.toLowerCase(); bb.toLowerCase();
    if (store->exists(np) && a != bb) { sendErr(r, 409, "\"" + clean + "\" already exists in this shelf."); return true; }
    if (!moveFile(path, np)) { sendErr(r, 500, "Could not rename the file."); return true; }
    migrateExtras(cat, safe, cat, clean);
    sendJson(r, 200, "{\"renamed\":\"" + jsonEsc(clean) + "\"}");
    return true;
  }
  if (action == "play" && m == HTTP_POST) {
    if (!store->exists(path)) { sendErr(r, 404, "File not found"); return true; }
    sendJson(r, 200, String("{\"plays\":") + bumpPlay(cat, safe) + "}");
    return true;
  }
  if (action == "edit" && m == HTTP_POST) {
    if (cat != "Pictures") { sendErr(r, 400, "Only pictures can be edited this way."); return true; }
    if (!store->exists(path)) { sendErr(r, 404, "File not found"); return true; }
    JsonDocument f(&psAlloc); f["saveAs"] = true;
    JsonDocument b(&psAlloc); parseBody(r, b, &f);
    bool copy = String(b["saveAs"] | "") == "copy";
    String tmp = String(TMP_ROOT "/edit-") + esp_random() + ".img";
    if (!bodyDataUrlToFile(r, "imageData", tmp, 20u * 1024 * 1024)) { sendErr(r, 400, "Invalid image data"); return true; }
    String fin = safe;
    if (copy) {
      int dot = safe.lastIndexOf('.');
      String base = dot > 0 ? safe.substring(0, dot) : safe, ext = dot > 0 ? safe.substring(dot) : "";
      fin = base + " (edited)" + ext;
      for (int i = 1; store->exists(catDir(cat) + "/" + fin); i++) fin = base + " (edited " + i + ")" + ext;
    }
    if (!moveFile(tmp, catDir(cat) + "/" + fin)) { store->remove(tmp); sendErr(r, 500, "Could not save the picture."); return true; }
    invalidateCounts();
    sendJson(r, 200, "{\"saved\":\"" + jsonEsc(fin) + "\",\"mode\":\"" + (copy ? "copy" : "overwrite") + "\"}");
    return true;
  }
  if (action == "thumbnail") {
    bool video = cat == "Videos";
    if (!video && !thumbable(cat)) { sendErr(r, 400, "This shelf does not support thumbnails."); return true; }
    const char* kind = video ? "video-thumbs" : "file-thumbs";
    String tp = thumbPath(kind, cat, safe);
    if (m == HTTP_DELETE) {
      bool had = store->exists(tp); if (had) store->remove(tp);
      sendJson(r, 200, String("{\"removed\":") + (had ? "true" : "false") + "}"); return true;
    }
    if (m == HTTP_POST) {
      if (!store->exists(path)) { sendErr(r, 404, "File not found"); return true; }
      mkdirs(String(DATA_ROOT "/") + kind);
      String tmp = tp + ".part";
      if (!bodyDataUrlToFile(r, "imageData", tmp, 10u * 1024 * 1024)) { sendErr(r, 400, "Invalid image data"); return true; }
      moveFile(tmp, tp);
      sendJson(r, 200, String("{\"saved\":true,\"thumbUrl\":\"/data-files/") + kind + "/" + tp.substring(tp.lastIndexOf('/') + 1) + "?v=" + (uint32_t)(nowMs() / 1000) + "\"}");
      return true;
    }
  }
  if (action == "tags" && m == HTTP_GET && cat == "Music") {
    JsonDocument meta(&psAlloc); loadJsonFile(META_FILE, meta);
    const char* a = meta[safe]["a"] | "";
    const char* t = meta[safe]["t"] | "";
    sendJson(r, 200, String("{\"title\":\"") + jsonEsc(t) + "\",\"artist\":\"" + jsonEsc(a) + "\",\"album\":\"\",\"year\":\"\",\"track\":\"\",\"genre\":\"\",\"readOnly\":true}");
    return true;
  }
  if (action == "tags" || action == "fetch-art") {
    sendErr(r, 501, "Editing tags and looking up album art need The Shelf on a computer. The board shows the art and artist already inside your songs.");
    return true;
  }
  return false;
}

// ------------------------------------------------------------ serving content
static const char* const DATA_DIRS[] = {"album-art", "video-thumbs", "file-thumbs", "drawings", "portal-media", nullptr};

bool serveContent(AsyncWebServerRequest* r, const String& url) {
  if (url.indexOf("..") >= 0) { r->send(400, "text/plain", "Bad path"); return true; }
  if (url.startsWith("/shared/")) {
    String rest = url.substring(8);
    int s = rest.indexOf('/');
    String cat = s < 0 ? rest : rest.substring(0, s);
    String name = s < 0 ? "" : rest.substring(s + 1);
    if (!isCategory(cat) || !name.length() || name.startsWith(".") || name.indexOf("/.") >= 0) { r->send(404, "text/plain", "Not found"); return true; }
    sendFileRanged(r, catDir(cat) + "/" + name);
    return true;
  }
  if (url.startsWith("/play/")) {
    String rest = url.substring(6);
    if (rest.indexOf("/.") >= 0 || rest.startsWith(".")) { r->send(404, "text/plain", "Not found"); return true; }
    sendFileRanged(r, String(PLAY_ROOT "/") + rest);
    return true;
  }
  if (url.startsWith("/data-files/")) {
    String rest = url.substring(12);
    int s = rest.indexOf('/');
    String dir = s < 0 ? "" : rest.substring(0, s), file = s < 0 ? "" : rest.substring(s + 1);
    bool okDir = false; for (int i = 0; DATA_DIRS[i]; i++) if (dir == DATA_DIRS[i]) okDir = true;
    String k = fileKind(file);
    if (!okDir || file.indexOf('/') >= 0 || file.startsWith(".") || extOf(file) == "svg" || (k != "image" && k != "video" && k != "audio")) {
      r->send(404, "text/plain", "Not found"); return true;
    }
    sendFileRanged(r, String(DATA_ROOT "/") + dir + "/" + file);
    return true;
  }
  return false;
}
