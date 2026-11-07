// The Shelf for Heltec V4 - artist, title and cover art straight from MP3 (ID3v2) tags.
// The full Shelf reads tags with TagLib and fetches art online; the board reads what's
// already inside each song, in the background, and caches it on the card.
#include "shelf.h"

static volatile bool dirty = true;
static volatile uint32_t dirtyAt = 0;
void markMusicDirty() { dirty = true; dirtyAt = millis(); }

static uint32_t syncsafe(const uint8_t* b) { return ((b[0] & 0x7f) << 21) | ((b[1] & 0x7f) << 14) | ((b[2] & 0x7f) << 7) | (b[3] & 0x7f); }
static uint32_t be32(const uint8_t* b) { return ((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]; }

static void putUtf8(String& o, uint32_t cp) {
  if (cp < 0x80) o += (char)cp;
  else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
  else { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
}

// Decodes an ID3 text frame body (encoding byte + text) to UTF-8.
static String id3Text(const uint8_t* p, size_t n) {
  if (!n) return "";
  uint8_t enc = p[0]; p++; n--;
  String o;
  if (enc == 0 || enc == 3) {                       // Latin-1 / UTF-8
    for (size_t i = 0; i < n && p[i]; i++) { if (enc == 0 && p[i] >= 0x80) putUtf8(o, p[i]); else o += (char)p[i]; }
  } else {                                          // UTF-16 with BOM, or UTF-16BE
    bool le = false; size_t i = 0;
    if (enc == 1 && n >= 2) { if (p[0] == 0xFF && p[1] == 0xFE) { le = true; i = 2; } else if (p[0] == 0xFE && p[1] == 0xFF) i = 2; }
    for (; i + 1 < n; i += 2) {
      uint16_t u = le ? (p[i] | (p[i + 1] << 8)) : ((p[i] << 8) | p[i + 1]);
      if (!u) break;
      if (u >= 0xD800 && u < 0xE000) continue;      // skip surrogates (emoji in tags are rare)
      putUtf8(o, u);
    }
  }
  o.trim();
  return o;
}

// Reads artist/title and saves embedded cover art. Returns false if there's no ID3v2 tag.
static bool readId3(const String& path, String& artist, String& title, const String& artOut) {
  File f = store->open(path, FILE_READ);
  if (!f) return false;
  uint8_t h[10];
  if (f.read(h, 10) != 10 || memcmp(h, "ID3", 3) != 0 || h[3] < 2 || h[3] > 4) { f.close(); return false; }
  uint8_t ver = h[3];
  uint32_t tagSize = syncsafe(h + 6);
  if (h[5] & 0x40 && ver >= 3) {                     // skip extended header
    uint8_t eh[4]; f.read(eh, 4);
    uint32_t es = ver == 4 ? syncsafe(eh) : be32(eh) ;
    f.seek(f.position() + (ver == 4 ? es - 4 : es));
  }
  uint32_t end = 10 + std::min<uint32_t>(tagSize, f.size());
  bool gotArt = false;
  while (f.position() + (ver == 2 ? 6 : 10) < end) {
    uint8_t fh[10];
    char id[5] = {0};
    uint32_t sz;
    if (ver == 2) {
      if (f.read(fh, 6) != 6) break;
      memcpy(id, fh, 3); sz = (fh[3] << 16) | (fh[4] << 8) | fh[5];
    } else {
      if (f.read(fh, 10) != 10) break;
      memcpy(id, fh, 4); sz = ver == 4 ? syncsafe(fh + 4) : be32(fh + 4);
    }
    if (!id[0] || sz == 0 || f.position() + sz > end) break;
    bool isArtist = !strcmp(id, "TPE1") || !strcmp(id, "TP1");
    bool isTitle = !strcmp(id, "TIT2") || !strcmp(id, "TT2");
    bool isPic = !strcmp(id, "APIC") || !strcmp(id, "PIC");
    if ((isArtist || isTitle) && sz < 1024) {
      uint8_t buf[1024]; f.read(buf, sz);
      String t = id3Text(buf, sz);
      if (isArtist && !artist.length()) artist = t;
      if (isTitle && !title.length()) title = t;
    } else if (isPic && !gotArt && artOut.length() && sz < 3u * 1024 * 1024) {
      uint32_t start = f.position();
      uint8_t hb[256]; size_t hn = f.read(hb, std::min<uint32_t>(sz, sizeof hb));
      size_t i = 1;                                  // after encoding byte
      uint8_t enc = hb[0];
      if (ver == 2) i += 3;                          // 3-char image format
      else { while (i < hn && hb[i]) i++; i++; }     // mime string
      i++;                                           // picture type
      if (enc == 1 || enc == 2) { while (i + 1 < hn && (hb[i] || hb[i + 1])) i += 2; i += 2; }
      else { while (i < hn && hb[i]) i++; i++; }     // description
      if (i < hn) {
        File out = store->open(artOut, FILE_WRITE);
        if (out) {
          f.seek(start + i);
          uint32_t left = sz - i; uint8_t cb[2048]; bool ok = true;
          while (left) { int k = f.read(cb, std::min<uint32_t>(left, sizeof cb)); if (k <= 0 || out.write(cb, k) != (size_t)k) { ok = false; break; } left -= k; }
          out.close();
          if (ok) gotArt = true; else store->remove(artOut);
        }
      }
      f.seek(start + sz);
    } else {
      f.seek(f.position() + sz);
    }
  }
  f.close();
  return true;
}

void musicMetaLoop() {
  if (!dirty || millis() - dirtyAt < 4000) return;
  dirty = false;
  const char* META = DATA_ROOT "/music-meta.json";
  JsonDocument meta(&psAlloc);
  if (!loadJsonFile(META, meta) || !meta.is<JsonObject>()) meta.to<JsonObject>();
  mkdirs(DATA_ROOT "/album-art");

  std::vector<std::pair<String, uint32_t>> files;
  File d = store->open("/Music");
  if (d && d.isDirectory()) {
    File c = d.openNextFile();
    while (c) { String n = c.name(); if (!c.isDirectory() && !n.startsWith(".")) files.push_back({n, (uint32_t)c.size()}); c.close(); c = d.openNextFile(); }
    d.close();
  }
  bool changed = false;
  // forget songs that are gone
  std::vector<String> gone;
  for (JsonPair kv : meta.as<JsonObject>()) {
    bool found = false; for (auto& f : files) if (f.first == kv.key().c_str()) { found = true; break; }
    if (!found) gone.push_back(kv.key().c_str());
  }
  for (auto& g : gone) { meta.remove(g); changed = true; }

  int done = 0;
  for (auto& f : files) {
    if (dirty) break;                               // new uploads arrived; start over shortly
    JsonVariant e = meta[f.first];
    if (!e.isNull() && (e["s"] | 0u) == f.second) continue;
    String artist, title;
    String art = String(DATA_ROOT "/album-art/") + fnvHex(String("Music/") + f.first) + ".jpg";
    if (extOf(f.first) == "mp3") readId3("/Music/" + f.first, artist, title, store->exists(art) ? String("") : art);
    JsonObject o = meta[f.first].to<JsonObject>();
    o["s"] = f.second;
    if (artist.length()) o["a"] = artist;
    if (title.length()) o["t"] = title;
    changed = true;
    if (++done % 10 == 0) { saveJsonFile(META, meta); changed = false; }
    delay(2);
  }
  if (changed) saveJsonFile(META, meta);
}
