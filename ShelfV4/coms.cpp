// The Shelf for Heltec V4 - 900MHz Coms: a simple LoRa text mesh between Shelf boards.
//
// Every board with Coms switched on is also a relay: anything new it hears is
// re-sent once (after a short random wait) until the message's hop limit runs out.
// Message ids are remembered so nothing loops. Not Meshtastic - its own channel.
//
// Air settings: 915.0 MHz, 125 kHz, SF9, CR 4/5, sync word 0x12, 16-symbol preamble.
#include "shelf.h"
#include <SPI.h>
#include <RadioLib.h>
#include <Preferences.h>

// Heltec V4 radio wiring (from Heltec's datasheet / Meshtastic's heltec_v4 variant)
#define LORA_NSS   8
#define LORA_SCK   9
#define LORA_MOSI  10
#define LORA_MISO  11
#define LORA_RST   12
#define LORA_BUSY  13
#define LORA_DIO1  14
// Front-end module (GC1109 on V4.2, KCT8103L on V4.3): its LDO, enable, and mode pins.
#define FEM_POWER  7    // HIGH = power the FEM
#define FEM_EN     2    // HIGH = chip enabled
#define FEM_CPS    46   // V4.2 (GC1109): HIGH = full PA while sending (free pin on V4.3)
#define FEM_CTX    5    // V4.3 (KCT8103L): HIGH while sending, LOW = receive through the LNA (free on V4.2)
// Both are switched around every send, as Meshtastic does, so either board revision works.

static const float FREQ = 915.0;
static const uint8_t MAGIC0 = 'S', MAGIC1 = '9';
static const uint8_t T_TEXT = 1, T_SYNC_REQ = 2, T_REPLAY = 3, T_RECEIPT = 4;   // packet types (byte 2)
static const uint32_t SYNC_WINDOW_S = 12 * 3600;                  // catch-up covers the last 12 hours
static const int SYNC_MAX = 60;
// No practical hop limit: every board relays a message once and remembers its id, so a
// message travels as far as there are boards to carry it and can't loop.
static const uint8_t HOPS = 255;
static const size_t MAX_TEXT = 180, MAX_NAME = 16;
static const char* LOG_FILE = DATA_ROOT "/coms-log.jsonl";
static const char* RECEIPT_FILE = DATA_ROOT "/coms-receipts.jsonl";

static SPIClass loraSPI(HSPI);              // the SD card has FSPI
static SX1262* radio = nullptr;
static Preferences cprefs;
static SemaphoreHandle_t comsLock;

static bool enabled = false, radioUp = false;
// "Received" receipts: when on, this board answers each message it hears with a short
// receipt that floods back to the sender, so the sender sees who got it.
static bool receiptsOn = true;
static std::vector<uint32_t> myIds;                       // my recent message ids
struct Rcpt { uint32_t msg; uint32_t node; String name; };
static std::vector<Rcpt> receipts;
static String myName;
static uint32_t myNode = 0;
static int level = 1;                       // 0 low, 1 normal, 2 high
static const int8_t CHIP_DBM[3] = {0, 9, 16}; // + ~11 dB from the FEM: about 11 / 20 / 27 dBm out
static uint32_t heardCount = 0, relayedCount = 0, sentCount = 0;
static String lastError;

struct Entry { String json; };
static std::vector<String> logRam;          // newest last, capped
static const size_t LOG_KEEP = 300;

struct Neighbor { uint32_t node; String name; float rssi, snr; uint64_t at; };
static std::vector<Neighbor> neighbors;

static uint32_t seen[1024]; static int seenPos = 0;
static bool wasSeen(uint32_t id) { for (uint32_t s : seen) if (s == id) return true; return false; }
static void markSeen(uint32_t id) { seen[seenPos] = id; seenPos = (seenPos + 1) % 1024; }

struct Tx { uint8_t buf[256]; size_t len; uint32_t due; };
static std::vector<Tx> txq;

// Catch-up: a board that just came on asks its neighbors for the last 12 hours.
struct SyncJob { uint32_t requester = 0; uint32_t due = 0; uint32_t created = 0; std::vector<uint32_t> have; bool active = false; };
static SyncJob syncJob;
static uint32_t lastReplayHeardAt = 0;       // someone else already answering a request
static uint32_t lastServed[8] = {0}, lastServedAt[8] = {0};
static uint32_t syncAskAt = 0, syncAsks = 0, syncedCount = 0;

static volatile bool rxFlag = false;
static void IRAM_ATTR onRx() { rxFlag = true; }

// For the OLED
String comsLastLine; uint32_t comsLastAt = 0;
bool comsOn() { return enabled && radioUp; }

static String hex8(uint32_t v) { char b[9]; snprintf(b, sizeof b, "%08x", v); return b; }

static void femOn(bool on) {
  pinMode(FEM_POWER, OUTPUT); pinMode(FEM_EN, OUTPUT);
  pinMode(FEM_CPS, OUTPUT); pinMode(FEM_CTX, OUTPUT);
  digitalWrite(FEM_POWER, on ? HIGH : LOW);
  digitalWrite(FEM_EN, on ? HIGH : LOW);
  digitalWrite(FEM_CPS, LOW);
  digitalWrite(FEM_CTX, LOW);
  if (on) delay(5);
}
static void femTx(bool tx) {
  digitalWrite(FEM_CPS, tx ? HIGH : LOW);
  digitalWrite(FEM_CTX, tx ? HIGH : LOW);
}

static void addLog(const String& json) {
  {
    xSemaphoreTake(comsLock, portMAX_DELAY);
    logRam.push_back(json);
    if (logRam.size() > LOG_KEEP) logRam.erase(logRam.begin(), logRam.begin() + (logRam.size() - LOG_KEEP));
    xSemaphoreGive(comsLock);
  }
  File f = store->open(LOG_FILE, FILE_APPEND);
  if (f) { f.println(json); f.close(); }
}

static void loadLog() {
  File f = store->open(LOG_FILE, FILE_READ);
  if (!f) return;
  size_t sz = f.size();
  if (sz > 96 * 1024) {                     // keep the file from growing forever
    f.seek(sz - 64 * 1024); f.readStringUntil('\n');
  }
  std::vector<String> lines;
  while (f.available()) { String l = f.readStringUntil('\n'); l.trim(); if (l.startsWith("{")) lines.push_back(l); }
  f.close();
  if (lines.size() > LOG_KEEP) lines.erase(lines.begin(), lines.begin() + (lines.size() - LOG_KEEP));
  logRam = lines;
  if (sz > 96 * 1024) {                     // rewrite the trimmed log
    File o = store->open(LOG_FILE, FILE_WRITE);
    if (o) { for (auto& l : logRam) o.println(l); o.close(); }
  }
}

static bool radioStart() {
  femOn(true);
  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  if (!radio) radio = new SX1262(new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI));
  int st = radio->begin(FREQ, 125.0, 9, 5, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, CHIP_DBM[level], 16, 1.8);
  if (st != RADIOLIB_ERR_NONE) { lastError = String("Radio didn't start (code ") + st + ")"; femOn(false); return false; }
  radio->setDio2AsRfSwitch(true);
  radio->setCRC(2);
  radio->setCurrentLimit(140);
  radio->setPacketReceivedAction(onRx);
  radio->startReceive();
  lastError = "";
  return true;
}

static void radioStop() {
  if (radio && radioUp) { radio->clearPacketReceivedAction(); radio->sleep(); }
  femOn(false);
  radioUp = false;
}

// ------------------------------------------------------------ packets
// [0]'S' [1]'9' [2]proto [3]hopsLeft [4]hopsTaken [5..8]msgId [9..12]origin [13..16]sender
// [17]nameLen [18..]name [..]text
// Replay packets (type 3) carry the message's age in seconds at [17..20], before the name.
static size_t buildPacket(uint8_t* b, uint32_t id, uint32_t origin, uint8_t hopsLeft, uint8_t hopsTaken, const String& name, const String& text,
                          uint8_t type = T_TEXT, uint32_t ageS = 0) {
  size_t n = 0;
  b[n++] = MAGIC0; b[n++] = MAGIC1; b[n++] = type; b[n++] = hopsLeft; b[n++] = hopsTaken;
  memcpy(b + n, &id, 4); n += 4; memcpy(b + n, &origin, 4); n += 4; memcpy(b + n, &myNode, 4); n += 4;
  if (type == T_REPLAY) { memcpy(b + n, &ageS, 4); n += 4; }
  size_t nl = std::min(name.length(), MAX_NAME); b[n++] = nl; memcpy(b + n, name.c_str(), nl); n += nl;
  size_t tl = std::min(text.length(), MAX_TEXT); memcpy(b + n, text.c_str(), tl); n += tl;
  return n;
}

static void queueTx(const uint8_t* buf, size_t len, uint32_t delayMs) {
  Tx t; memcpy(t.buf, buf, len); t.len = len; t.due = millis() + delayMs;
  xSemaphoreTake(comsLock, portMAX_DELAY);
  if (txq.size() < 80) txq.push_back(t);
  xSemaphoreGive(comsLock);
}

static String entryJson(uint32_t id, uint32_t origin, const String& name, const String& text, int hops, bool mine, float rssi, float snr,
                        uint64_t t = 0, bool synced = false) {
  String j = "{\"id\":\"" + hex8(id) + "\",\"node\":\"" + hex8(origin) + "\",\"from\":\"" + jsonEsc(name) + "\",\"text\":\"" + jsonEsc(text) +
             "\",\"t\":" + String((double)(t ? t : nowMs()), 0) + ",\"hops\":" + hops + ",\"mine\":" + (mine ? "true" : "false");
  if (synced) j += ",\"synced\":true";
  if (!mine) j += ",\"rssi\":" + String(rssi, 0) + ",\"snr\":" + String(snr, 1);
  return j + "}";
}

static void handleSyncRequest(const uint8_t* b, size_t len) {
  // [2]=2 [3..6] requester [7] k [8..] k message ids it already has
  if (len < 8) return;
  uint32_t req; memcpy(&req, b + 3, 4);
  if (req == myNode) return;
  for (int i = 0; i < 8; i++) if (lastServed[i] == req && millis() - lastServedAt[i] < 120000) return;  // served it just now
  size_t k = std::min((size_t)b[7], (len - 8) / 4);
  xSemaphoreTake(comsLock, portMAX_DELAY);
  syncJob.active = true; syncJob.requester = req; syncJob.created = millis();
  syncJob.due = millis() + 500 + esp_random() % 2500;   // random wait: whoever goes first answers
  syncJob.have.clear();
  for (size_t i = 0; i < k; i++) { uint32_t id; memcpy(&id, b + 8 + i * 4, 4); syncJob.have.push_back(id); }
  xSemaphoreGive(comsLock);
}

static bool isMine(uint32_t msg) { return std::find(myIds.begin(), myIds.end(), msg) != myIds.end(); }

static void noteReceipt(uint32_t msg, uint32_t node, const String& name, bool save) {
  xSemaphoreTake(comsLock, portMAX_DELAY);
  for (auto& r : receipts) if (r.msg == msg && r.node == node) { xSemaphoreGive(comsLock); return; }
  receipts.push_back({msg, node, name});
  if (receipts.size() > 2000) receipts.erase(receipts.begin(), receipts.begin() + 500);
  xSemaphoreGive(comsLock);
  if (save) {
    File f = store->open(RECEIPT_FILE, FILE_APPEND);
    if (f) { f.printf("{\"m\":\"%s\",\"n\":\"%s\",\"by\":\"%s\"}\n", hex8(msg).c_str(), hex8(node).c_str(), jsonEsc(name).c_str()); f.close(); }
  }
}

// [2]=4 [3]hopsLeft [4]hopsTaken [5..8]receipt id [9..12]who received it [13..16]sender of this copy
// [17..20]message id it's for [21]nameLen [22..]name
static size_t buildReceipt(uint8_t* b, uint32_t rid, uint32_t receiver, uint8_t hopsLeft, uint8_t hopsTaken, uint32_t msg, const String& name) {
  size_t n = 0;
  b[n++] = MAGIC0; b[n++] = MAGIC1; b[n++] = T_RECEIPT; b[n++] = hopsLeft; b[n++] = hopsTaken;
  memcpy(b + n, &rid, 4); n += 4; memcpy(b + n, &receiver, 4); n += 4; memcpy(b + n, &myNode, 4); n += 4;
  memcpy(b + n, &msg, 4); n += 4;
  size_t nl = std::min(name.length(), MAX_NAME); b[n++] = nl; memcpy(b + n, name.c_str(), nl); n += nl;
  return n;
}

static void handleReceipt(const uint8_t* b, size_t len) {
  if (len < 22) return;
  uint8_t hopsLeft = b[3], hopsTaken = b[4];
  uint32_t rid, receiver, msg;
  memcpy(&rid, b + 5, 4); memcpy(&receiver, b + 9, 4); memcpy(&msg, b + 17, 4);
  size_t nl = b[21];
  if (22 + nl > len || nl > MAX_NAME) return;
  String name; for (size_t i = 0; i < nl; i++) name += (char)b[22 + i];
  if (receiver == myNode || wasSeen(rid)) return;
  markSeen(rid);
  if (isMine(msg)) { noteReceipt(msg, receiver, name, true); return; }   // it's for me: done
  if (hopsLeft > 0) {                                                      // someone else's: pass it on
    uint8_t out[64];
    size_t n = buildReceipt(out, rid, receiver, hopsLeft - 1, hopsTaken + 1, msg, name);
    queueTx(out, n, 600 + esp_random() % 2000);
  }
}

static void handlePacket(const uint8_t* b, size_t len, float rssi, float snr) {
  if (len < 8 || b[0] != MAGIC0 || b[1] != MAGIC1) return;
  uint8_t type = b[2];
  if (type == T_SYNC_REQ) { handleSyncRequest(b, len); return; }
  if (type == T_RECEIPT) {
    if (len >= 17) {                       // still note who we heard
      uint32_t sender; memcpy(&sender, b + 13, 4);
      xSemaphoreTake(comsLock, portMAX_DELAY);
      Neighbor* nb = nullptr; for (auto& x : neighbors) if (x.node == sender) nb = &x;
      if (!nb) { neighbors.push_back({sender, "", 0, 0, 0}); nb = &neighbors.back(); }
      nb->rssi = rssi; nb->snr = snr; nb->at = nowMs();
      xSemaphoreGive(comsLock);
    }
    handleReceipt(b, len); return;
  }
  if (type != T_TEXT && type != T_REPLAY) return;
  size_t hdr = type == T_REPLAY ? 22 : 18;
  if (len < hdr) return;
  uint8_t hopsLeft = b[3], hopsTaken = b[4];
  uint32_t id, origin, sender, ageS = 0;
  memcpy(&id, b + 5, 4); memcpy(&origin, b + 9, 4); memcpy(&sender, b + 13, 4);
  if (type == T_REPLAY) { memcpy(&ageS, b + 17, 4); if (sender != myNode) lastReplayHeardAt = millis(); }
  size_t nl = b[hdr - 1];
  if (hdr + nl > len || nl > MAX_NAME) return;
  String name; for (size_t i = 0; i < nl; i++) name += (char)b[hdr + i];
  String text; for (size_t i = hdr + nl; i < len; i++) text += (char)b[i];
  // who we heard directly (the sender of this copy)
  {
    xSemaphoreTake(comsLock, portMAX_DELAY);
    Neighbor* nb = nullptr; for (auto& x : neighbors) if (x.node == sender) nb = &x;
    if (!nb) { neighbors.push_back({sender, "", 0, 0, 0}); nb = &neighbors.back(); }
    if (sender == origin) nb->name = name;
    nb->rssi = rssi; nb->snr = snr; nb->at = nowMs();
    xSemaphoreGive(comsLock);
  }
  if (origin == myNode || wasSeen(id)) return;
  markSeen(id);
  if (type == T_REPLAY) {                    // catch-up copy: log it with its real age, never relay it
    if (ageS > SYNC_WINDOW_S) return;
    uint64_t t = nowMs() > (uint64_t)ageS * 1000 ? nowMs() - (uint64_t)ageS * 1000 : 0;
    addLog(entryJson(id, origin, name, text, hopsTaken, false, rssi, snr, t, true));
    syncedCount++;
    return;
  }
  heardCount++;
  addLog(entryJson(id, origin, name, text, hopsTaken, false, rssi, snr));
  comsLastLine = name + ": " + text; comsLastAt = millis();
  // relay
  if (hopsLeft > 0) {
    uint8_t out[256];
    size_t n = buildPacket(out, id, origin, hopsLeft - 1, hopsTaken + 1, name, text);
    queueTx(out, n, 400 + esp_random() % 1600);
    relayedCount++;
  }
  // say "received" back to the sender (after the relay has gone out)
  if (receiptsOn) {
    uint8_t out[64];
    size_t n = buildReceipt(out, esp_random(), myNode, HOPS, 0, id, myName);
    queueTx(out, n, 2200 + esp_random() % 2500);
  }
}

static void comsTask(void*) {
  for (;;) {
    if (!enabled) {
      if (radioUp) radioStop();
      vTaskDelay(pdMS_TO_TICKS(200)); continue;
    }
    if (!radioUp) {
      radioUp = radioStart();
      if (!radioUp) { vTaskDelay(pdMS_TO_TICKS(5000)); continue; }
      syncAsks = 0; syncAskAt = millis() + 3000 + esp_random() % 2000;   // ask for the last 12 hours shortly after starting
    }
    if (rxFlag) {
      rxFlag = false;
      uint8_t buf[256];
      size_t len = radio->getPacketLength();
      if (len > 0 && len <= sizeof buf && radio->readData(buf, len) == RADIOLIB_ERR_NONE)
        handlePacket(buf, len, radio->getRSSI(), radio->getSNR());
      radio->startReceive();
    }
    // just came on: ask the neighbors for the last 12 hours (again after 2 minutes, in case nobody heard)
    if (syncAsks < 2 && (int32_t)(millis() - syncAskAt) >= 0) {
      uint8_t q[256]; size_t n = 0;
      q[n++] = MAGIC0; q[n++] = MAGIC1; q[n++] = T_SYNC_REQ; memcpy(q + n, &myNode, 4); n += 4;
      std::vector<uint32_t> have;
      xSemaphoreTake(comsLock, portMAX_DELAY);
      for (int i = (int)logRam.size() - 1; i >= 0 && have.size() < 40; i--) {
        int p = logRam[i].indexOf("\"id\":\""); if (p >= 0) have.push_back(strtoul(logRam[i].substring(p + 6, p + 14).c_str(), nullptr, 16));
      }
      xSemaphoreGive(comsLock);
      q[n++] = have.size();
      for (uint32_t id : have) { memcpy(q + n, &id, 4); n += 4; }
      queueTx(q, n, 0);
      syncAsks++; syncAskAt = millis() + 120000;
    }
    // answer someone's catch-up request, unless another board already started answering
    bool serve = false; SyncJob job;
    xSemaphoreTake(comsLock, portMAX_DELAY);
    if (syncJob.active && (int32_t)(millis() - syncJob.due) >= 0) { job = syncJob; syncJob.active = false; serve = true; }
    xSemaphoreGive(comsLock);
    if (serve && !(lastReplayHeardAt && (int32_t)(lastReplayHeardAt - job.created) > 0)) {
      for (int i = 7; i > 0; i--) { lastServed[i] = lastServed[i - 1]; lastServedAt[i] = lastServedAt[i - 1]; }
      lastServed[0] = job.requester; lastServedAt[0] = millis();
      std::vector<String> pick;
      uint64_t now = nowMs(), cutoff = now > SYNC_WINDOW_S * 1000ULL ? now - SYNC_WINDOW_S * 1000ULL : 0;
      xSemaphoreTake(comsLock, portMAX_DELAY);
      for (int i = (int)logRam.size() - 1; i >= 0 && (int)pick.size() < SYNC_MAX; i--) pick.push_back(logRam[i]);
      xSemaphoreGive(comsLock);
      uint32_t spacing = 0;
      for (int i = (int)pick.size() - 1; i >= 0; i--) {            // oldest first
        JsonDocument e(&psAlloc);
        if (deserializeJson(e, pick[i])) continue;
        uint64_t t = e["t"] | 0ULL;
        if (t < cutoff || t > now + 60000) continue;
        uint32_t id = strtoul(e["id"] | "0", nullptr, 16), origin = strtoul(e["node"] | "0", nullptr, 16);
        if (origin == job.requester) continue;
        if (std::find(job.have.begin(), job.have.end(), id) != job.have.end()) continue;
        uint8_t out[256];
        size_t n = buildPacket(out, id, origin, 0, e["hops"] | 0, String(e["from"] | ""), String(e["text"] | ""), T_REPLAY, (uint32_t)((now - t) / 1000));
        queueTx(out, n, spacing); spacing += 1200;
      }
    }
    // send anything that's due, listening first so boards don't talk over each other
    Tx t; bool have = false;
    xSemaphoreTake(comsLock, portMAX_DELAY);
    for (size_t i = 0; i < txq.size(); i++) if ((int32_t)(millis() - txq[i].due) >= 0) { t = txq[i]; txq.erase(txq.begin() + i); have = true; break; }
    xSemaphoreGive(comsLock);
    if (have) {
      int cad = radio->scanChannel();
      if (cad == RADIOLIB_LORA_DETECTED) {
        t.due = millis() + 300 + esp_random() % 900;
        xSemaphoreTake(comsLock, portMAX_DELAY); txq.push_back(t); xSemaphoreGive(comsLock);
      } else {
        radio->setOutputPower(CHIP_DBM[level]);
        femTx(true);
        int st = radio->transmit(t.buf, t.len);
        femTx(false);
        if (st != RADIOLIB_ERR_NONE) lastError = String("Send failed (code ") + st + ")";
      }
      rxFlag = false;
      radio->startReceive();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void comsBegin() {
  comsLock = xSemaphoreCreateMutex();
  cprefs.begin("coms", false);
  uint64_t mac = ESP.getEfuseMac();
  myNode = (uint32_t)(mac >> 16) ^ (uint32_t)mac;
  myName = cprefs.getString("name", hostName);
  level = constrain(cprefs.getInt("level", 1), 0, 2);
  enabled = cprefs.getBool("on", false);
  receiptsOn = cprefs.getBool("receipts", true);
  loadLog();
  for (auto& l : logRam) if (l.indexOf("\"mine\":true") >= 0) {     // my messages, so receipts match
    int p = l.indexOf("\"id\":\""); if (p >= 0) myIds.push_back(strtoul(l.substring(p + 6, p + 14).c_str(), nullptr, 16));
  }
  File rf = store->open(RECEIPT_FILE, FILE_READ);
  if (rf) {
    while (rf.available()) {
      String l = rf.readStringUntil('\n');
      JsonDocument d(&psAlloc);
      if (!deserializeJson(d, l)) noteReceipt(strtoul(d["m"] | "0", nullptr, 16), strtoul(d["n"] | "0", nullptr, 16), String(d["by"] | ""), false);
    }
    rf.close();
  }
  xTaskCreatePinnedToCore(comsTask, "coms", 8192, nullptr, 2, nullptr, 1);
}

// ------------------------------------------------------------ HTTP
bool handleComsApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m) {
  if (!url.startsWith("/api/coms")) return false;
  if (url == "/api/coms" && m == HTTP_GET) {
    String s = String("{\"enabled\":") + (enabled ? "true" : "false") + ",\"radio\":" + (radioUp ? "true" : "false") +
      ",\"name\":\"" + jsonEsc(myName) + "\",\"node\":\"" + hex8(myNode) + "\",\"level\":" + level +
      ",\"receipts\":" + (receiptsOn ? "true" : "false") + ",\"freq\":915.0,\"heard\":" + heardCount + ",\"relayed\":" + relayedCount + ",\"sent\":" + sentCount + ",\"synced\":" + syncedCount +
      ",\"error\":\"" + jsonEsc(lastError) + "\",\"neighbors\":[";
    xSemaphoreTake(comsLock, portMAX_DELAY);
    bool first = true;
    for (auto& n : neighbors) {
      if (nowMs() - n.at > 30ULL * 60 * 1000) continue;
      if (!first) s += ","; first = false;
      s += "{\"node\":\"" + hex8(n.node) + "\",\"name\":\"" + jsonEsc(n.name) + "\",\"rssi\":" + String(n.rssi, 0) + ",\"snr\":" + String(n.snr, 1) +
           ",\"at\":" + String((double)n.at, 0) + "}";
    }
    s += "],\"log\":[";
    for (size_t i = 0; i < logRam.size(); i++) { if (i) s += ","; s += logRam[i]; }
    s += "],\"receivedBy\":{";           // message id -> who said "received"
    std::vector<uint32_t> done;
    for (auto& rc : receipts) {
      if (std::find(done.begin(), done.end(), rc.msg) != done.end()) continue;
      done.push_back(rc.msg);
      if (done.size() > 1) s += ",";
      s += "\"" + hex8(rc.msg) + "\":[";
      bool f2 = true;
      for (auto& x : receipts) if (x.msg == rc.msg) { if (!f2) s += ","; s += "\"" + jsonEsc(x.name.length() ? x.name : hex8(x.node)) + "\""; f2 = false; }
      s += "]";
    }
    xSemaphoreGive(comsLock);
    sendJson(r, 200, s + "}}");
    return true;
  }
  if (m != HTTP_POST) { sendErr(r, 405, "Use POST"); return true; }
  JsonDocument b(&psAlloc); parseBody(r, b);
  if (url == "/api/coms/toggle") {
    enabled = b["on"] | false;
    cprefs.putBool("on", enabled);
    sendJson(r, 200, String("{\"enabled\":") + (enabled ? "true" : "false") + "}");
    return true;
  }
  if (url == "/api/coms/send") {
    if (!enabled) { sendErr(r, 409, "Turn 900MHz Coms on first."); return true; }
    String text = b["text"] | ""; text.trim();
    if (!text.length()) { sendErr(r, 400, "Type a message first."); return true; }
    if (text.length() > MAX_TEXT) { sendErr(r, 400, "That's too long for one radio message (180 characters max)."); return true; }
    uint32_t id = esp_random();
    markSeen(id);
    myIds.push_back(id); if (myIds.size() > 400) myIds.erase(myIds.begin());
    uint8_t out[256];
    size_t n = buildPacket(out, id, myNode, HOPS, 0, myName, text);
    queueTx(out, n, 0);
    sentCount++;
    String e = entryJson(id, myNode, myName, text, 0, true, 0, 0);
    addLog(e);
    sendJson(r, 200, e);
    return true;
  }
  if (url == "/api/coms/settings") {
    String nm = b["name"] | ""; nm.trim();
    if (nm.length()) { myName = nm.substring(0, MAX_NAME); cprefs.putString("name", myName); }
    if (b["level"].is<int>()) { level = constrain(b["level"].as<int>(), 0, 2); cprefs.putInt("level", level); }
    if (b["receipts"].is<bool>()) { receiptsOn = b["receipts"].as<bool>(); cprefs.putBool("receipts", receiptsOn); }
    sendJson(r, 200, "{\"ok\":true,\"name\":\"" + jsonEsc(myName) + "\",\"level\":" + level + "}");
    return true;
  }
  if (url == "/api/coms/clear") {
    xSemaphoreTake(comsLock, portMAX_DELAY); logRam.clear(); receipts.clear(); xSemaphoreGive(comsLock);
    store->remove(LOG_FILE); store->remove(RECEIPT_FILE);
    sendJson(r, 200, "{\"ok\":true}");
    return true;
  }
  return false;
}
