// The Shelf for Heltec V4 - admin password, read-only mode, and the keys used to
// sign remote-admin commands sent over 900MHz Coms.
//
// - The admin password is never stored. The board keeps a key made from it
//   (PBKDF2-HMAC-SHA256, salted with the board's mesh id), the same key a remote
//   board derives from the password you type, so it can check signed commands.
// - Read-only mode: everyone can look and play, nobody can change anything, until
//   a browser unlocks write mode with the admin password (a cookie for 8 hours).
// - Wrong passwords: after 5 in a row, nothing is accepted for 5 minutes.
#include "shelf.h"
#include <Preferences.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

static Preferences aprefs;
static bool haveKey = false, readOnly = false;
static uint8_t boardKey[32];
static uint32_t nodeId = 0;

struct WriteToken { char tok[33]; uint32_t at; };
static std::vector<WriteToken> tokens;
static const uint32_t TOKEN_MS = 8UL * 3600 * 1000;

static int fails = 0; static uint32_t lockedUntil = 0;

uint32_t meshNodeId() {
  if (!nodeId) { uint64_t mac = ESP.getEfuseMac(); nodeId = (uint32_t)(mac >> 16) ^ (uint32_t)mac; }
  return nodeId;
}

void adminDeriveKey(const String& password, uint32_t node, uint8_t out[32]) {
  char salt[24]; snprintf(salt, sizeof salt, "shelf-admin:%08x", (unsigned)node);
  mbedtls_md_context_t ctx; mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_pkcs5_pbkdf2_hmac(&ctx, (const uint8_t*)password.c_str(), password.length(), (const uint8_t*)salt, strlen(salt), 4096, 32, out);
  mbedtls_md_free(&ctx);
}

void hmac8(const uint8_t key[32], const uint8_t* data, size_t len, uint8_t out[8]) {
  uint8_t full[32];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, 32, data, len, full);
  memcpy(out, full, 8);
}

static bool sameBytes(const uint8_t* a, const uint8_t* b, size_t n) { uint8_t d = 0; for (size_t i = 0; i < n; i++) d |= a[i] ^ b[i]; return d == 0; }

bool adminIsSet() { return haveKey; }
bool readOnlyOn() { return haveKey && readOnly; }
const uint8_t* adminKey() { return haveKey ? boardKey : nullptr; }

bool adminLockedOut() { return lockedUntil && (int32_t)(lockedUntil - millis()) > 0; }
void adminNoteFail() { if (++fails >= 5) { fails = 0; lockedUntil = millis() + 5UL * 60 * 1000; } }
void adminNoteOk() { fails = 0; }

static bool checkPassword(const String& pw) {
  if (!haveKey || adminLockedOut()) return false;
  uint8_t k[32]; adminDeriveKey(pw, meshNodeId(), k);
  bool ok = sameBytes(k, boardKey, 32);
  if (ok) adminNoteOk(); else adminNoteFail();
  return ok;
}

void setReadOnly(bool on) { readOnly = on && haveKey; aprefs.putBool("ro", readOnly); }

void adminClear() {                         // the PRG button's "reset admin password"
  aprefs.remove("key"); aprefs.putBool("ro", false);
  haveKey = false; readOnly = false; tokens.clear();
}

void adminBegin() {
  aprefs.begin("admin", false);
  meshNodeId();
  haveKey = aprefs.getBytes("key", boardKey, 32) == 32;
  readOnly = haveKey && aprefs.getBool("ro", false);
}

// ---- remote-admin replay protection: the newest command counter seen from each board
uint32_t adminLastCounter(uint32_t from) { char k[12]; snprintf(k, sizeof k, "c%08x", (unsigned)from); return aprefs.getUInt(k, 0); }
void adminSetCounter(uint32_t from, uint32_t c) { char k[12]; snprintf(k, sizeof k, "c%08x", (unsigned)from); aprefs.putUInt(k, c); }
uint32_t adminNextOwnCounter(uint32_t atLeast) {
  uint32_t c = aprefs.getUInt("mine", 0) + 1;
  uint32_t t = (uint32_t)time(nullptr);
  if (t > 1700000000 && t > c) c = t;      // a set clock keeps counters rising even after a reflash
  if (atLeast > c) c = atLeast;
  aprefs.putUInt("mine", c);
  return c;
}

// ---- browser write tokens (cookie "shelf_w")
static String cookieToken(AsyncWebServerRequest* r) {
  const AsyncWebHeader* h = r->getHeader("Cookie");
  if (!h) return "";
  const String& v = h->value();
  int p = v.indexOf("shelf_w=");
  if (p < 0) return "";
  int e = v.indexOf(';', p);
  return v.substring(p + 8, e < 0 ? v.length() : e);
}
static bool tokenValid(const String& t) {
  if (t.length() != 32) return false;
  for (auto it = tokens.begin(); it != tokens.end();) {
    if (millis() - it->at > TOKEN_MS) { it = tokens.erase(it); continue; }
    if (t == it->tok) return true;
    ++it;
  }
  return false;
}
static bool unlocked(AsyncWebServerRequest* r) { return tokenValid(cookieToken(r)); }
bool canWrite(AsyncWebServerRequest* r) { return !readOnlyOn() || unlocked(r); }

// Requests that still go through in read-only mode: looking, playing, chatting.
bool allowedWhenReadOnly(const String& url, WebRequestMethod m) {
  if (m == HTTP_GET || m == HTTP_HEAD || m == HTTP_OPTIONS) return true;
  if (url.startsWith("/api/admin/") || url.startsWith("/api/auth/")) return true;
  if (url.startsWith("/api/messaging/")) return true;                 // Messages and other Shelves saying hello
  if (url == "/api/coms/send" || url.startsWith("/api/coms/remote/")) return true;
  if (url == "/api/board/time") return true;
  if (url.startsWith("/api/bt/") && url != "/api/bt/power" && url != "/api/bt/forget") return true;   // playing to a headset
  if (url.startsWith("/api/files/") && (url.endsWith("/play") || url.endsWith("/thumbnail"))) return true;   // play counts, auto thumbnails
  return false;
}

void sendReadOnly(AsyncWebServerRequest* r) {
  AsyncWebServerResponse* res = r->beginResponse(403, "application/json",
    "{\"error\":\"The Shelf is in read-only mode. Unlock write mode with the admin password to make changes.\",\"readOnly\":true}");
  res->addHeader("X-Shelf-ReadOnly", "1");
  res->addHeader("Cache-Control", "no-store");
  r->send(res);
}

static String statusJson(AsyncWebServerRequest* r) {
  bool u = unlocked(r);
  return String("{\"adminSet\":") + (haveKey ? "true" : "false") + ",\"readOnly\":" + (readOnlyOn() ? "true" : "false") +
         ",\"unlocked\":" + (u ? "true" : "false") + ",\"canWrite\":" + (canWrite(r) ? "true" : "false") +
         ",\"lockedOut\":" + (adminLockedOut() ? "true" : "false") + "}";
}

static void sendWithCookie(AsyncWebServerRequest* r, const String& body, const String& cookie) {
  AsyncWebServerResponse* res = r->beginResponse(200, "application/json", body);
  res->addHeader("Set-Cookie", cookie);
  res->addHeader("Cache-Control", "no-store");
  r->send(res);
}

static const char* WRONG = "That's not the admin password.";
static const char* LOCKED = "Too many wrong passwords. Try again in 5 minutes.";

bool handleAdminApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m) {
  if (url == "/api/auth/status") {            // what The Shelf's page reads
    bool w = canWrite(r);
    sendJson(r, 200, String("{\"isLocal\":true,\"protectionEnabled\":false,\"secureMode\":false,\"loggedIn\":false,\"username\":null,\"role\":\"") +
                     (w ? "write" : "read") + "\",\"readOnly\":" + (readOnlyOn() ? "true" : "false") + "}");
    return true;
  }
  if (!url.startsWith("/api/admin/")) return false;
  if (url == "/api/admin/status") { sendJson(r, 200, statusJson(r)); return true; }
  if (m != HTTP_POST) { sendErr(r, 405, "Use POST"); return true; }
  JsonDocument b(&psAlloc); parseBody(r, b);
  String pw = b["password"] | "";

  if (url == "/api/admin/unlock") {
    if (!haveKey) { sendErr(r, 400, "Set an admin password first, on the board's settings page."); return true; }
    if (adminLockedOut()) { sendErr(r, 429, LOCKED); return true; }
    if (!checkPassword(pw)) { sendErr(r, 403, adminLockedOut() ? LOCKED : WRONG); return true; }
    WriteToken t; String s = uuid4(); s.replace("-", ""); strlcpy(t.tok, s.c_str(), sizeof t.tok); t.at = millis();
    if (tokens.size() >= 12) tokens.erase(tokens.begin());
    tokens.push_back(t);
    sendWithCookie(r, "{\"ok\":true,\"unlocked\":true}", String("shelf_w=") + t.tok + "; Path=/; Max-Age=28800; SameSite=Lax; HttpOnly");
    return true;
  }
  if (url == "/api/admin/lock") {
    String t = cookieToken(r);
    for (auto it = tokens.begin(); it != tokens.end(); ++it) if (t == it->tok) { tokens.erase(it); break; }
    sendWithCookie(r, "{\"ok\":true,\"unlocked\":false}", "shelf_w=; Path=/; Max-Age=0; SameSite=Lax; HttpOnly");
    return true;
  }
  if (url == "/api/admin/password") {         // set the first time, or change it (needs the old one)
    String nw = b["newPassword"] | "";
    if (nw.length() < 6 || nw.length() > 64) { sendErr(r, 400, "The admin password needs 6 to 64 characters."); return true; }
    if (haveKey) {
      if (adminLockedOut()) { sendErr(r, 429, LOCKED); return true; }
      if (!checkPassword(pw)) { sendErr(r, 403, adminLockedOut() ? LOCKED : "The current admin password isn't right."); return true; }
    } else if (!canWrite(r)) { sendReadOnly(r); return true; }
    adminDeriveKey(nw, meshNodeId(), boardKey);
    aprefs.putBytes("key", boardKey, 32);
    haveKey = true;
    sendJson(r, 200, statusJson(r));
    return true;
  }
  if (url == "/api/admin/readonly") {
    bool on = b["on"] | false;
    if (!haveKey) { sendErr(r, 400, "Set an admin password first, so you can unlock it again."); return true; }
    // Turning read-only on is fine for anyone who can write; turning it off needs the password
    // (or a browser that's already unlocked).
    if (!(on ? canWrite(r) || checkPassword(pw) : unlocked(r) || checkPassword(pw))) {
      sendErr(r, 403, adminLockedOut() ? LOCKED : WRONG); return true;
    }
    setReadOnly(on);
    sendJson(r, 200, statusJson(r));
    return true;
  }
  if (url == "/api/admin/remove") {           // take the admin password off (needs it)
    if (!haveKey) { sendJson(r, 200, statusJson(r)); return true; }
    if (!checkPassword(pw)) { sendErr(r, 403, adminLockedOut() ? LOCKED : WRONG); return true; }
    adminClear();
    sendJson(r, 200, statusJson(r));
    return true;
  }
  sendErr(r, 404, "Not found");
  return true;
}
