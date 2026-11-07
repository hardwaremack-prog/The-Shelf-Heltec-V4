// =====================================================================
//  The Shelf for Heltec V4
//  Runs The Shelf's own web app on a Heltec WiFi LoRa 32 V4, with your
//  files on a microSD card. Other Shelves on the network see it in Messages.
// =====================================================================
#include "shelf.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <FFat.h>
#include <LittleFS.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <esp_heap_caps.h>

// ------------------------------------------------------------ globals
fs::FS* store = nullptr;
bool onSD = false;
volatile bool statsDirty = true;
uint64_t storeTotal = 0, storeUsed = 0;
SemaphoreHandle_t fsLock;

String hostName, apName, wifiSSID;
static String wifiPass;
bool apActive = false, staOK = false;
static bool mdnsOK = false, ntpStarted = false;
static uint32_t apOffAt = 0;
volatile bool rebootSoon = false;
static uint32_t rebootAt = 0;
static const char* AP_PASS = "theshelf";

static AsyncWebServer server80(80), server8080(8080);
static DNSServer dns;
static Preferences prefs;
static SPIClass sdSPI(FSPI);
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);
static bool oledOK = false, screenOn = true;
static uint32_t lastActivity = 0;
static int onlineShelves = 0;

String currentIP() { return staOK ? WiFi.localIP().toString() : WiFi.softAPIP().toString(); }

// ------------------------------------------------------------ OLED
static bool probeOLED() {
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW); delay(20); digitalWrite(OLED_RST, HIGH); delay(20);
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.beginTransmission(OLED_ADDR);
  bool ok = Wire.endTransmission() == 0;
  Wire.end();
  return ok;
}

static void powerOLED() {
  pinMode(PIN_VEXT, OUTPUT); digitalWrite(PIN_VEXT, LOW); delay(60);
  if (!probeOLED()) {          // V4-R8: Vext is on GPIO40
    pinMode(PIN_VEXT, INPUT);
    pinMode(PIN_VEXT_R8, OUTPUT); digitalWrite(PIN_VEXT_R8, LOW); delay(60);
    probeOLED();
  }
  oledOK = true;
  oled.begin();
  oled.setContrast(200);
}

static void oledMsg(const char* a, const char* b = "", const char* c = "", const char* d = "") {
  if (!oledOK) return;
  oled.clearBuffer();
  oled.setFont(u8g2_font_helvB10_tf); oled.drawStr(0, 12, a);
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 30, b); oled.drawStr(0, 44, c); oled.drawStr(0, 58, d);
  oled.sendBuffer();
}

static String fmtShort(uint64_t b) {
  char buf[16];
  if (b >= 1000000000ULL) snprintf(buf, sizeof buf, "%.1fGB", b / 1e9);
  else if (b >= 1000000ULL) snprintf(buf, sizeof buf, "%.1fMB", b / 1e6);
  else snprintf(buf, sizeof buf, "%.0fKB", b / 1e3);
  return buf;
}

static void drawStatus() {
  if (!oledOK || !screenOn) return;
  oled.clearBuffer();
  oled.setFont(u8g2_font_helvB08_tf); oled.drawStr(0, 9, "THE SHELF");
  oled.setFont(u8g2_font_5x7_tf); oled.drawStr(58, 9, "V4");
  oled.drawStr(onSD ? 116 : 104, 9, onSD ? "SD" : "FLASH");
  oled.drawHLine(0, 12, 128);
  oled.setFont(u8g2_font_6x10_tf);
  if (staOK) {
    oled.drawStr(0, 24, WiFi.localIP().toString().c_str());
    oled.drawStr(0, 35, (hostName + ".local").c_str());
  } else if (apActive) {
    oled.drawStr(0, 24, ("Join " + apName).c_str());
    oled.drawStr(0, 35, "pw theshelf > 4.1");
  } else {
    oled.drawStr(0, 24, "Connecting Wi-Fi...");
    oled.drawStr(0, 35, wifiSSID.c_str());
  }
  if (storeTotal) {
    int w = (int)((storeUsed * 76ULL) / storeTotal);
    oled.drawFrame(0, 39, 78, 7); oled.drawBox(1, 40, w, 5);
    oled.setFont(u8g2_font_5x7_tf);
    oled.drawStr(82, 46, (fmtShort(storeTotal - storeUsed) + " free").c_str());
  }
  oled.setFont(u8g2_font_6x10_tf);
  String bottom = staOK ? String("Wi-Fi ") + WiFi.RSSI() + "dBm" : String(BOARD_NAME " v" FW_VERSION);
  if (staOK && onlineShelves > 0) bottom = String(onlineShelves) + (onlineShelves == 1 ? " Shelf nearby" : " Shelves nearby");
  oled.drawStr(0, 62, bottom.c_str());
  oled.sendBuffer();
}

// ------------------------------------------------------------ storage
static void setupStorage() {
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (SD.begin(SD_CS, sdSPI, 20000000, "/sd", 10)) onSD = true;
  else if (SD.begin(SD_CS, sdSPI, 4000000, "/sd", 10)) onSD = true;   // slower, for long wires
  if (onSD && SD.cardType() == CARD_NONE) { SD.end(); onSD = false; }
  if (onSD) store = &SD;
  else {
    oledMsg("THE SHELF V4", "No microSD found", "Using board flash", "(first boot formats)");
    if (!FFat.begin(true, "/ffat", 10, "ffat")) oledMsg("Storage error", "Flash format failed");
    store = &FFat;
  }
  for (auto c : CATEGORIES) mkdirs(String("/") + c);
  for (const char* d : {DATA_ROOT, DATA_ROOT "/drawings", DATA_ROOT "/portal-media", DATA_ROOT "/video-thumbs",
                        DATA_ROOT "/file-thumbs", DATA_ROOT "/album-art", PLAY_ROOT}) mkdirs(d);
  if (store->exists(TMP_ROOT)) removeAll(TMP_ROOT);
  mkdirs(TMP_ROOT);
}

static void refreshStats() {
  if (onSD) { storeTotal = SD.totalBytes(); storeUsed = SD.usedBytes(); }
  else { storeTotal = FFat.totalBytes(); storeUsed = FFat.usedBytes(); }
  statsDirty = false;
}

// ------------------------------------------------------------ Wi-Fi
static void startAP() {
  if (apActive) return;
  WiFi.mode(wifiSSID.length() ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(apName.c_str(), AP_PASS);
  dns.start(53, "*", WiFi.softAPIP());
  apActive = true;
}
static void stopAP() {
  if (!apActive) return;
  dns.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); apActive = false;
}
static void onJoined() {
  staOK = true;
  WiFi.setSleep(false);
  if (!mdnsOK && MDNS.begin(hostName.c_str())) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("http", "tcp", 8080);
    MDNS.addServiceTxt("http", "tcp", "app", "the-shelf");
    mdnsOK = true;
  }
  if (!ntpStarted) { configTime(0, 0, "pool.ntp.org", "time.google.com"); ntpStarted = true; }
}
static String sanitizeHost(String h) {
  h.toLowerCase(); String o;
  for (size_t i = 0; i < h.length() && o.length() < 24; i++) {
    char c = h[i];
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') o += c;
    else if (c == ' ' || c == '_') o += '-';
  }
  while (o.startsWith("-")) o.remove(0, 1);
  while (o.endsWith("-")) o.remove(o.length() - 1);
  return o;
}

// Board settings for the little setup page at /board/
bool handleBoardApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m) {
  if (!url.startsWith("/api/board/")) return false;
  if (url == "/api/board/info") {
    String s = String("{\"board\":\"") + BOARD_NAME + "\",\"fw\":\"" FW_VERSION "\",\"storage\":\"" + (onSD ? "microSD card" : "board flash") +
      "\",\"total\":" + String((double)storeTotal, 0) + ",\"used\":" + String((double)storeUsed, 0) + ",\"host\":\"" + hostName +
      "\",\"ip\":\"" + currentIP() + "\",\"ssid\":\"" + jsonEsc(staOK ? WiFi.SSID() : String("")) + "\",\"rssi\":" + (staOK ? WiFi.RSSI() : 0) +
      ",\"ap\":" + (apActive ? "true" : "false") + ",\"apName\":\"" + apName + "\",\"uptime\":" + (millis() / 1000) +
      ",\"heap\":" + ESP.getFreeHeap() + ",\"psram\":" + ESP.getPsramSize() + "}";
    sendJson(r, 200, s); return true;
  }
  if (url == "/api/board/wifi/scan") {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) {
      if (!(WiFi.getMode() & WIFI_STA)) WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
      WiFi.scanNetworks(true);
      sendJson(r, 200, "{\"scanning\":true}"); return true;
    }
    if (n == WIFI_SCAN_RUNNING) { sendJson(r, 200, "{\"scanning\":true}"); return true; }
    String s = "{\"scanning\":false,\"nets\":[";
    for (int i = 0; i < n; i++) {
      if (i) s += ",";
      s += "{\"ssid\":\"" + jsonEsc(WiFi.SSID(i)) + "\",\"rssi\":" + WiFi.RSSI(i) + ",\"lock\":" + (WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? 1 : 0) + "}";
    }
    WiFi.scanDelete();
    sendJson(r, 200, s + "]}"); return true;
  }
  if (m != HTTP_POST) { sendErr(r, 405, "Use POST"); return true; }
  JsonDocument b(&psAlloc); parseBody(r, b);
  if (url == "/api/board/wifi") {
    String ssid = b["ssid"] | "", pass = b["pass"] | "";
    if (!ssid.length()) { sendErr(r, 400, "Pick a network"); return true; }
    prefs.putString("ssid", ssid); prefs.putString("pass", pass);
    sendJson(r, 200, "{\"ok\":true}"); rebootSoon = true; return true;
  }
  if (url == "/api/board/wifi/forget") { prefs.remove("ssid"); prefs.remove("pass"); sendJson(r, 200, "{\"ok\":true}"); rebootSoon = true; return true; }
  if (url == "/api/board/settings") {
    String h = sanitizeHost(b["host"] | "");
    if (h.length() < 2) { sendErr(r, 400, "The name needs letters or numbers"); return true; }
    bool restart = h != hostName;
    if (restart) prefs.putString("host", h);
    sendJson(r, 200, String("{\"ok\":true,\"restart\":") + (restart ? "true" : "false") + "}");
    if (restart) rebootSoon = true;
    return true;
  }
  if (url == "/api/board/reboot") { sendJson(r, 200, "{\"ok\":true}"); rebootSoon = true; return true; }
  return false;
}

// ------------------------------------------------------------ web
static void sendWww(AsyncWebServerRequest* r, const char* path, const char* type) {
  String gz = String(path) + ".gz";
  if (!LittleFS.exists(gz)) { r->send(500, "text/plain", "The web app is missing from the board. Re-flash the full firmware from the installer page."); return; }
  AsyncWebServerResponse* res = r->beginResponse(LittleFS, gz, type);
  res->addHeader("Content-Encoding", "gzip");
  res->addHeader("Cache-Control", "no-cache");
  r->send(res);
}

class ShelfHandler : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest* r) const override {
    const String& u = r->url();
    return u.startsWith("/api/") || u.startsWith("/shared/") || u.startsWith("/play/") || u.startsWith("/data-files/") ||
           u == "/" || u == "/index.html";
  }
  bool isRequestHandlerTrivial() const override { return false; }

  void handleBody(AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total) override {
    if (index == 0) {
      if (r->_tempObject) return;
      bool ram = total <= BODY_RAM_MAX;
      Body* b = (Body*)malloc(sizeof(Body) + (ram ? total : 0));
      if (!b) return;
      memset(b, 0, sizeof(Body));
      b->magic = BODY_MAGIC; b->len = total; b->tooBig = total > BODY_MAX;
      if (!ram && !b->tooBig) {
        b->inFile = true;
        snprintf(b->path, sizeof b->path, TMP_ROOT "/b%08x.json", (unsigned)esp_random());
        r->_tempFile = store->open(b->path, FILE_WRITE);
        if (!r->_tempFile) b->tooBig = true;
      }
      r->_tempObject = b;
    }
    Body* b = bodyOf(r);
    if (!b || b->tooBig) return;
    if (!b->inFile) { if (index + len <= b->len) memcpy(b->buf + index, data, len); }
    else if (r->_tempFile) {
      if (r->_tempFile.write(data, len) != len) { b->tooBig = true; r->_tempFile.close(); }
      else if (index + len >= total) r->_tempFile.close();
    }
  }

  void handleUpload(AsyncWebServerRequest* r, const String& fn, size_t index, uint8_t* data, size_t len, bool final) override {
    handleUploadPart(r, fn, index, data, len, final);
  }

  void handleRequest(AsyncWebServerRequest* r) override {
    lastActivity = millis();
    const String& url = r->url();
    WebRequestMethod m = (WebRequestMethod)r->method();
    if (m == HTTP_OPTIONS) { r->send(204); return; }
    Body* b = bodyOf(r);
    if (b && b->tooBig) { sendErr(r, 413, "That's too big for the board to take in one go."); cleanup(r); return; }

    if (url == "/" || url == "/index.html") {
      if (apActive && !staOK) { r->redirect("/board/"); return; }
      if (r->client()) recordVisitor(r->client()->remoteIP().toString());
      sendWww(r, "/index.html", "text/html; charset=utf-8");
      return;
    }
    if (serveContent(r, url)) return;
    bool done = handleBoardApi(r, url, m) || handleMessagingApi(r, url, m) || handleFilesApi(r, url, m) || handleToolsApi(r, url, m);
    if (!done) sendErr(r, 404, "Not found");
    cleanup(r);
  }

 private:
  static void cleanup(AsyncWebServerRequest* r) {
    Body* b = bodyOf(r);
    if (b && b->inFile) { if (r->_tempFile) r->_tempFile.close(); store->remove(b->path); }
  }
};

static void setupServer(AsyncWebServer& s) {
  s.addHandler(new ShelfHandler());
  s.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("max-age=600");
  s.onNotFound([](AsyncWebServerRequest* r) {
    if (apActive) {
      String host = r->host(), ip = WiFi.softAPIP().toString();
      if (host != ip && host != hostName + ".local") return r->redirect("http://" + ip + "/board/");
    }
    r->send(404, "text/plain", "Not found");
  });
  s.begin();
}

// ------------------------------------------------------------ button
// short press: wake the screen. hold 6 s: forget Wi-Fi.
static uint32_t btnDownAt = 0; static bool btnDown = false;
static void handleButton() {
  bool down = digitalRead(PIN_BUTTON) == LOW; uint32_t now = millis();
  if (down && !btnDown) { btnDown = true; btnDownAt = now; }
  if (down && now - btnDownAt > 1500) {
    if (!screenOn) { screenOn = true; oled.setPowerSave(0); }
    int left = 6 - (int)((now - btnDownAt) / 1000);
    if (left <= 0) {
      oledMsg("Wi-Fi reset", "Forgetting network...", "Restarting in setup", "mode");
      prefs.remove("ssid"); prefs.remove("pass"); delay(1200); ESP.restart();
    }
    char b[32]; snprintf(b, sizeof b, "Keep holding %d s", left);
    oledMsg("Forget Wi-Fi?", b, "Let go to cancel");
  }
  if (!down && btnDown) {
    btnDown = false; lastActivity = now;
    if (now - btnDownAt < 800 && !screenOn) { screenOn = true; oled.setPowerSave(0); }
  }
}

// ------------------------------------------------------------ setup / loop
void setup() {
  Serial.begin(115200);
  // Unzipping a big game or listing a huge shelf can keep the web task busy for a while;
  // don't let core 0's idle watchdog reset the board over it.
  disableCore0WDT();
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  fsLock = xSemaphoreCreateRecursiveMutex();
  powerOLED();
  oledMsg("THE SHELF", BOARD_NAME, "Starting...");

  prefs.begin("shelf", false);
  uint64_t mac = ESP.getEfuseMac();
  char tag[5]; snprintf(tag, sizeof tag, "%02x%02x", (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  String up = tag; up.toUpperCase();
  apName = "The-Shelf-" + up;
  hostName = prefs.getString("host", String("shelf-v4-") + tag);
  wifiSSID = prefs.getString("ssid", "");
  wifiPass = prefs.getString("pass", "");

  if (!LittleFS.begin(false, "/www", 10, "www")) oledMsg("Web app missing", "Re-flash from the", "installer page");
  setupStorage();
  refreshStats();

  WiFi.persistent(false);
  WiFi.setHostname(hostName.c_str());
  if (wifiSSID.length()) {
    WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
    WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
      char b[16]; snprintf(b, sizeof b, "%lus", (unsigned long)((millis() - t0) / 1000));
      oledMsg("Connecting", wifiSSID.c_str(), b); delay(400);
    }
    if (WiFi.status() == WL_CONNECTED) onJoined();
  }
  if (!staOK) startAP();

  setupServer(server80);
  setupServer(server8080);
  messagingBegin();
  markMusicDirty();
  lastActivity = millis();
}

int onlinePeerCount();

void loop() {
  uint32_t now = millis();
  if (apActive) dns.processNextRequest();
  if (!staOK && WiFi.status() == WL_CONNECTED) { onJoined(); apOffAt = now + 90000; }
  if (staOK && WiFi.status() != WL_CONNECTED) staOK = false;
  if (apActive && apOffAt && now > apOffAt && staOK) { stopAP(); apOffAt = 0; }
  if (rebootSoon) { rebootSoon = false; rebootAt = now + 1500; }
  if (rebootAt && now > rebootAt) { oledMsg("Restarting..."); delay(200); ESP.restart(); }

  handleButton();
  static uint32_t lastStats = 0;
  if (statsDirty || now - lastStats > 60000) { refreshStats(); lastStats = now; }
  musicMetaLoop();

  if (screenOn && now - lastActivity > 300000 && !btnDown) { screenOn = false; oled.setPowerSave(1); }
  static uint32_t lastDraw = 0;
  if (!btnDown && now - lastDraw > 1000) { onlineShelves = onlinePeerCount(); drawStatus(); lastDraw = now; }
  delay(5);
}
