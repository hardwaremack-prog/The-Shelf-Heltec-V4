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
#include <sys/time.h>
#include <algorithm>
#include <esp_netif.h>
#include <dhcpserver/dhcpserver.h>

// ------------------------------------------------------------ globals
fs::FS* store = nullptr;
bool onSD = false;
volatile bool statsDirty = true;
uint64_t storeTotal = 0, storeUsed = 0;
SemaphoreHandle_t fsLock;

String hostName, apName, wifiSSID;
static String wifiPass;
// "Own Wi-Fi": the board runs its own network and The Shelf works on it (car, camping...).
static bool hotspotMode = false;          // chosen in settings
static bool firstSetup = false;           // nothing set up yet: setup page first
static String ownSSID, ownPass;
bool apActive = false, staOK = false;
static bool mdnsOK = false, ntpStarted = false;
static uint32_t apOffAt = 0;
volatile bool rebootSoon = false;
static uint32_t rebootAt = 0;
static const char* AP_PASS = "theshelf";

static AsyncWebServer server80(80), server8080(8080);
static DNSServer dns;
static Preferences prefs;
#if CONFIG_IDF_TARGET_ESP32
static SPIClass sdSPI(VSPI);       // plain ESP32: FSPI is the flash chip's own bus
#else
static SPIClass sdSPI(FSPI);
#endif
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);
static bool oledOK = false, screenOn = true;
static void oledPower(int save) { if (oledOK) oled.setPowerSave(save); }
static uint32_t lastActivity = 0;
static int onlineShelves = 0;

String currentIP() { return staOK ? WiFi.localIP().toString() : WiFi.softAPIP().toString(); }

// ------------------------------------------------------------ OLED
static bool probeOLED() {
  if (OLED_RST != 255) { pinMode(OLED_RST, OUTPUT); digitalWrite(OLED_RST, LOW); delay(20); digitalWrite(OLED_RST, HIGH); delay(20); }
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.beginTransmission(OLED_ADDR);
  bool ok = Wire.endTransmission() == 0;
  Wire.end();
  return ok;
}

// The V4 powers its screen through a "Vext" switch: GPIO36 on the standard V4,
// GPIO40 on the V4-R8 (whose GPIO33-37 belong to its PSRAM, so they're never touched
// in the R8 build). Heltec's docs disagree on the switch's polarity, so try both.
static bool tryVext(int pin, int level) { pinMode(pin, OUTPUT); digitalWrite(pin, level); delay(60); return probeOLED(); }

static void powerOLED() {
#if defined(SHELF_ESP32)
  oledOK = probeOLED();             // a screen is optional here: no screen, no drawing
  if (!oledOK) return;
  oled.begin();
  oled.setContrast(200);
  return;
#endif
#if defined(SHELF_V3)
  bool ok = tryVext(PIN_VEXT, LOW) || tryVext(PIN_VEXT, HIGH);   // V3: GPIO36, LOW = on
#elif CONFIG_SPIRAM_MODE_OCT
  bool ok = tryVext(PIN_VEXT_R8, LOW) || tryVext(PIN_VEXT_R8, HIGH);
#else
  // (no fallback to GPIO40 here: on the standard V4 that pin is the microSD clock)
  bool ok = tryVext(PIN_VEXT, LOW) || tryVext(PIN_VEXT, HIGH);
#endif
  (void)ok;
  oledOK = true;
  oled.begin();
  oled.setContrast(200);
}

// ------------------------------------------------------------ battery
// VBAT is read on GPIO1 through a 390k/100k divider (x4.9) while ADC_CTRL (GPIO37)
// is held high. The R8 build doesn't touch GPIO37 (its PSRAM) and just reads GPIO1.
static float battV = -1;
static void readBattery() {
#if defined(SHELF_ESP32)
  return;                           // no battery divider on a dev board
#endif
  uint32_t mv = 0;
#if defined(SHELF_V3)
  // V3.0/3.1 connect the divider with ADC_CTRL LOW, V3.2 with HIGH. Unconnected, GPIO1
  // reads about 0 V, so read both ways and keep the higher one.
  for (int lvl = 0; lvl < 2; lvl++) {
    pinMode(PIN_ADC_CTRL, OUTPUT); digitalWrite(PIN_ADC_CTRL, lvl ? HIGH : LOW); delay(5);
    uint32_t s = 0;
    for (int i = 0; i < 8; i++) s += analogReadMilliVolts(PIN_VBAT);
    if (s > mv) mv = s;
  }
  pinMode(PIN_ADC_CTRL, INPUT);
#else
#if !CONFIG_SPIRAM_MODE_OCT
  pinMode(PIN_ADC_CTRL, OUTPUT); digitalWrite(PIN_ADC_CTRL, HIGH); delay(5);
#endif
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_VBAT);
#if !CONFIG_SPIRAM_MODE_OCT
  digitalWrite(PIN_ADC_CTRL, LOW);
#endif
#endif
  float v = (mv / 8.0f) * 4.9f * 1.045f / 1000.0f;   // divider x4.9, calibration as in Meshtastic's heltec_v3/v4
  battV = (battV < 0 || fabsf(v - battV) > 0.5f) ? v : battV * 0.7f + v * 0.3f;
}
// -1 = no battery (running on USB)
static int batteryPercent() {
  if (battV < 2.8f || battV > 4.45f) return -1;
  static const float V[] = {4.20f, 4.10f, 4.00f, 3.90f, 3.80f, 3.70f, 3.60f, 3.50f, 3.40f, 3.30f};
  static const int P[] = {100, 90, 80, 65, 50, 35, 20, 10, 5, 0};
  if (battV >= V[0]) return 100;
  for (int i = 1; i < 10; i++)
    if (battV >= V[i]) return P[i] + (int)((battV - V[i]) / (V[i - 1] - V[i]) * (P[i - 1] - P[i]));
  return 0;
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

// Draws a line, scrolling it sideways if it's wider than the screen.
static bool scrolling = false;
static void drawLine(int y, const String& t) {
  int tw = oled.getStrWidth(t.c_str());
  if (tw <= 128) { oled.drawStr(0, y, t.c_str()); return; }
  scrolling = true;
  int span = tw + 30, off = (int)((millis() / 200) * 3 % span);
  oled.drawStr(-off, y, t.c_str());
  oled.drawStr(span - off, y, t.c_str());
}

static void drawStatus() {
  scrolling = false;
  if (!oledOK || !screenOn) return;
  oled.clearBuffer();
  // top bar: name, storage, battery
  oled.setFont(u8g2_font_helvB08_tf); oled.drawStr(0, 9, "THE SHELF");
  int x = oled.getStrWidth("THE SHELF") + 3;
  oled.setFont(u8g2_font_5x7_tf); oled.drawStr(x, 9, BOARD_SHORT);
  x += oled.getStrWidth(BOARD_SHORT) + 4;
  const char* st = onSD ? "SD" : (comsOn() ? "FL" : "FLASH");
  oled.drawStr(x, 9, st);
  if (comsOn()) {                                   // tiny antenna: 900MHz Coms is on
    x += oled.getStrWidth(st) + 4;
    oled.drawVLine(x + 3, 2, 7);
    oled.drawPixel(x + 1, 3); oled.drawPixel(x, 4); oled.drawPixel(x + 1, 5);
    oled.drawPixel(x + 5, 3); oled.drawPixel(x + 6, 4); oled.drawPixel(x + 5, 5);
  }
  int pct = batteryPercent();
  oled.setFont(u8g2_font_4x6_tf);
  if (pct < 0) {
    oled.drawStr(128 - oled.getStrWidth("USB"), 8, "USB");
  } else {
    String t = String(pct) + "%";
    int tw = oled.getStrWidth(t.c_str());
    oled.drawStr(128 - tw, 8, t.c_str());
    int bx = 128 - tw - 16;                         // battery icon: 12x7 body + nub
    oled.drawFrame(bx, 2, 12, 7);
    oled.drawBox(bx + 12, 4, 2, 3);
    oled.drawBox(bx + 1, 3, (pct * 10 + 50) / 100, 5);
  }
  oled.drawHLine(0, 12, 128);

  oled.setFont(u8g2_font_6x10_tf);
  String bottom;
  if (staOK) {
    oled.drawStr(0, 24, (hostName + ".local").c_str());
    String l2 = onlineShelves > 0 ? String(onlineShelves) + (onlineShelves == 1 ? " Shelf nearby" : " Shelves nearby")
                                  : String("Wi-Fi ") + WiFi.RSSI() + " dBm";
    if (comsLastAt && millis() - comsLastAt < 15000) l2 = comsLastLine;
    drawLine(35, l2);
    bottom = "IP " + WiFi.localIP().toString();
  } else if (apActive) {
    oled.drawStr(0, 24, ((firstSetup ? "Join " : "Wi-Fi ") + ownSSID).c_str());
    String l2 = ownPass.length() >= 8 ? "Password: " + ownPass : String("Open network");
    if (!firstSetup && comsLastAt && millis() - comsLastAt < 15000) l2 = comsLastLine;
    drawLine(35, l2);
    bottom = (firstSetup ? "Setup " : "IP ") + WiFi.softAPIP().toString();
  } else {
    oled.drawStr(0, 24, "Connecting Wi-Fi...");
    oled.drawStr(0, 35, wifiSSID.c_str());
    bottom = BOARD_NAME " v" FW_VERSION;
  }
  if (storeTotal) {
    int w = (int)((storeUsed * 62ULL) / storeTotal);
    oled.drawFrame(0, 39, 64, 7); oled.drawBox(1, 40, w, 5);
    oled.setFont(u8g2_font_5x7_tf);
    String fr = fmtShort(storeTotal - storeUsed) + " free";
    oled.drawStr(128 - oled.getStrWidth(fr.c_str()), 46, fr.c_str());
  }
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 62, bottom.c_str());
  oled.sendBuffer();
}

// ------------------------------------------------------------ storage
static void setupStorage() {
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  // Bare microSD breakouts have no pull-up resistors. Without one the card's data-out line
  // floats while it's busy, and the board can misread "busy" or "write accepted", so writes
  // fail now and then. The ESP32's own pull-ups fix that (as a 10k resistor would).
  for (int pin : {SD_MISO, SD_MOSI, SD_CS}) gpio_pullup_en((gpio_num_t)pin);
  // 10 MHz: plenty for Wi-Fi speeds, and more margin than 20 MHz on jumpers and headers.
  if (SD.begin(SD_CS, sdSPI, 10000000, "/sd", 10)) onSD = true;
  else if (SD.begin(SD_CS, sdSPI, 4000000, "/sd", 10)) onSD = true;   // slower, for long wires
  if (onSD && SD.cardType() == CARD_NONE) { SD.end(); onSD = false; }
  if (onSD) store = &SD;
  else {
    oledMsg("THE SHELF " BOARD_SHORT, "No microSD found", "Using board flash", "(first boot formats)");
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
static void startMDNS() {
  if (mdnsOK || !MDNS.begin(hostName.c_str())) return;
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("http", "tcp", 8080);
  MDNS.addServiceTxt("http", "tcp", "app", "the-shelf");
  mdnsOK = true;
}
static void startAP() {
  if (apActive) return;
  WiFi.mode((wifiSSID.length() && !hotspotMode) ? WIFI_AP_STA : WIFI_AP);
  // channel 1, visible, up to 8 phones/laptops at once
  WiFi.softAP(ownSSID.c_str(), ownPass.length() >= 8 ? ownPass.c_str() : nullptr, 1, 0, 8);
#ifdef AP_IP
  // this board's own Wi-Fi has a fixed address (phones get 10.1.1.100 and up)
  WiFi.softAPConfig(IPAddress(AP_IP), IPAddress(AP_IP), IPAddress(255, 255, 255, 0), IPAddress(AP_DHCP_START));
#endif
  if (firstSetup) {
    // First setup: act as a captive portal so the phone pops up the setup page.
    dns.start(53, "*", WiFi.softAPIP());
  } else {
    // Own Wi-Fi: hand out NO gateway and NO DNS server. Phones then mark the network
    // "No Internet Connection", stay joined for The Shelf (192.168.4.1 / name.local),
    // and keep using cellular data for everything else - texts, iMessage, apps.
    esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap) {
      esp_netif_dhcps_stop(ap);
      dhcps_offer_t none = 0;
      esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS, &none, sizeof(none));
      esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &none, sizeof(none));
      esp_netif_dhcps_start(ap);
    }
  }
  apActive = true;
  startMDNS();
}
static void stopAP() {
  if (!apActive) return;
  dns.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); apActive = false;
}
static void onJoined() {
  staOK = true;
#if !defined(SHELF_ESP32)
  WiFi.setSleep(false);       // (a plain ESP32 with Bluetooth on must keep Wi-Fi power saving, or it aborts)
#endif
  startMDNS();
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
      ",\"ap\":" + (apActive ? "true" : "false") + ",\"apName\":\"" + jsonEsc(ownSSID) + "\",\"hotspot\":" + (hotspotMode ? "true" : "false") +
      ",\"setup\":" + (firstSetup ? "true" : "false") + ",\"homeSsid\":\"" + jsonEsc(wifiSSID) + "\",\"apOpen\":" + (ownPass.length() >= 8 ? "false" : "true") + ",\"apPass\":\"" + jsonEsc(ownPass) + "\"" +
      ",\"clients\":" + (apActive ? WiFi.softAPgetStationNum() : 0) + ",\"timeSet\":" + (time(nullptr) > 1700000000 ? "true" : "false") +
      ",\"uptime\":" + (millis() / 1000) +
      ",\"sdPins\":\"" SD_PINS "\",\"sdRetries\":" + String(sdRetries) + ",\"sdFailures\":" + String(sdFailures) + ",\"heap\":" + ESP.getFreeHeap() + ",\"psram\":" + ESP.getPsramSize() + "}";
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
    prefs.putString("ssid", ssid); prefs.putString("pass", pass); prefs.putString("mode", "home");
    sendJson(r, 200, "{\"ok\":true}"); rebootSoon = true; return true;
  }
  if (url == "/api/board/wifi/forget") { prefs.remove("ssid"); prefs.remove("pass"); prefs.remove("mode"); sendJson(r, 200, "{\"ok\":true}"); rebootSoon = true; return true; }
  if (url == "/api/board/hotspot") {          // run on the board's own Wi-Fi
    String ss = b["ssid"] | ""; ss.trim();
    String pw = b["pass"] | "";
    if (ss.length() < 1 || ss.length() > 32) { sendErr(r, 400, "The network name needs 1 to 32 characters."); return true; }
    if (pw.length() && (pw.length() < 8 || pw.length() > 63)) { sendErr(r, 400, "The password needs 8 to 63 characters (or leave it blank for an open network)."); return true; }
    prefs.putString("apssid", ss); prefs.putString("appass", pw); prefs.putString("mode", "hotspot");
    sendJson(r, 200, "{\"ok\":true}"); rebootSoon = true; return true;
  }
  if (url == "/api/board/home") {             // back to joining the home Wi-Fi
    if (!wifiSSID.length()) { sendErr(r, 400, "Pick your home Wi-Fi first with Find networks."); return true; }
    prefs.putString("mode", "home");
    sendJson(r, 200, "{\"ok\":true}"); rebootSoon = true; return true;
  }
  if (url == "/api/board/time") {             // no internet on its own Wi-Fi: take the time from the browser
    uint64_t ms = b["ms"] | 0ULL;
    if (time(nullptr) < 1700000000 && ms > 1700000000000ULL) {
      struct timeval tv = {(time_t)(ms / 1000), (suseconds_t)((ms % 1000) * 1000)};
      settimeofday(&tv, nullptr);
    }
    sendJson(r, 200, String("{\"ok\":true,\"timeSet\":") + (time(nullptr) > 1700000000 ? "true" : "false") + "}"); return true;
  }
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
  if (!LittleFS.exists(gz)) {
    // The web app's flash area is empty or unreadable (usually an install that stopped early).
    // The firmware still runs, and its built-in /update page can put the web app back.
    r->send(500, "text/html; charset=utf-8",
      "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
      "<body style='font:16px/1.5 system-ui,sans-serif;background:#241B2F;color:#F0E9E3;max-width:560px;margin:40px auto;padding:0 16px'>"
      "<h2 style='font-family:Georgia,serif'>The Shelf's web app isn't on this board</h2>"
      "<p>The firmware is running, but the part of the flash that holds the web app is empty. That usually means an install stopped before it finished.</p>"
      "<p><a style='color:#F2B441;font-weight:700' href='/update'>Open the update page</a> and use <b>Check for update</b> (needs internet) or "
      "<b>Update from a file</b> with this board's <code>-main.bin</code> from the installer page. Or reinstall over USB from the installer page.</p>");
    return;
  }
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
           u == "/" || u == "/index.html" || u == "/update";
  }
  bool isRequestHandlerTrivial() const override { return false; }

  void handleBody(AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total) override {
    if (isOtaUpload(r->url())) { otaBody(r, data, len, index, total); return; }   // firmware updates go straight to flash
    if (index == 0) {
      if (r->_tempObject) return;
      if (r->client() && total > 64 * 1024) r->client()->setRxTimeout(30);   // big pasted pictures / docs: don't drop on a slow card
      bool ram = total <= BODY_RAM_MAX;
      Body* b = (Body*)malloc(sizeof(Body) + (ram ? total : UPLOAD_BUF));
      if (!b) return;
      memset(b, 0, sizeof(Body));
      b->magic = BODY_MAGIC; b->len = total; b->tooBig = total > BODY_MAX && !isCardFileUpload(r->url());   // card restores can be big
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
    size_t n0 = len;
    if (!b->inFile) { if (index + len <= b->len) memcpy(b->buf + index, data, len); }
    else if (r->_tempFile) {
      // collect 16 KB at a time (far faster on the card than ~1.4 KB pieces), retrying a refused block
      while (len) {
        size_t n = std::min(len, (size_t)UPLOAD_BUF - b->fill);
        memcpy(b->buf + b->fill, data, n); b->fill += n; data += n; len -= n;
        if (b->fill == UPLOAD_BUF) {
          if (!writeBlockRetry(r->_tempFile, b->path, (uint8_t*)b->buf, b->fill, b->synced)) { b->tooBig = true; r->_tempFile.close(); return; }
          b->fill = 0;
        }
      }
      if (index + n0 >= total) {
        if (!writeBlockRetry(r->_tempFile, b->path, (uint8_t*)b->buf, b->fill, b->synced)) b->tooBig = true;
        b->fill = 0;
        r->_tempFile.close();
      }
    }
  }

  void handleUpload(AsyncWebServerRequest* r, const String& fn, size_t index, uint8_t* data, size_t len, bool final) override {
    if (!canWrite(r)) return;               // read-only: handleRequest answers 403
    handleUploadPart(r, fn, index, data, len, final);
  }

  void handleRequest(AsyncWebServerRequest* r) override {
    // (web requests no longer keep the screen awake: an open page polls all night)
    const String& url = r->url();
    WebRequestMethod m = (WebRequestMethod)r->method();
    if (m == HTTP_OPTIONS) { r->send(204); return; }
    Body* b = bodyOf(r);
    if (b && b->tooBig) { sendErr(r, 413, "That's too big for the board to take in one go."); cleanup(r); return; }
    if (handleAdminApi(r, url, m)) { cleanup(r); return; }
    // read-only mode: only looking, playing and chatting get through until write mode is unlocked
    if (!allowedWhenReadOnly(url, m) && !canWrite(r)) { sendReadOnly(r); cleanup(r); return; }

    if (url == "/" || url == "/index.html") {
      if (firstSetup) { r->redirect("/board/"); return; }
      if (r->client()) recordVisitor(r->client()->remoteIP().toString());
      sendWww(r, "/index.html", "text/html; charset=utf-8");
      return;
    }
    try {
    if (serveContent(r, url)) return;
    bool done = handleOtaApi(r, url, m) || handleBtApi(r, url, m) || handleCardApi(r, url, m) || handleBoardApi(r, url, m) || handleComsApi(r, url, m) || handleMessagingApi(r, url, m) || handleFilesApi(r, url, m) || handleToolsApi(r, url, m);
    if (!done) sendErr(r, 404, "Not found");
    } catch (...) {
      // out of memory part-way through (a big list on a board without PSRAM): say so, keep running
      sendErr(r, 503, "The board is short of memory right now. Try again in a moment.");
    }
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
      String host = r->host(), ip = WiFi.softAPIP().toString(), u = r->url();
      // Own Wi-Fi never claims to have internet (no gateway, no DNS), so phones keep
      // their cellular data. Only first setup redirects stray requests to the setup page.
      if (firstSetup && host != ip && host != hostName + ".local") return r->redirect("http://" + ip + "/board/");
    }
    r->send(404, "text/plain", "Not found");
  });
  s.begin();
}

// ------------------------------------------------------------ button
// short press: wake the screen (it sleeps after 5 minutes).
// hold 6 s, then let go: forget Wi-Fi.  keep holding to 12 s: clear the admin password.
static uint32_t btnDownAt = 0; static bool btnDown = false;
static void handleButton() {
  bool down = digitalRead(PIN_BUTTON) == LOW; uint32_t now = millis();
  if (down && !btnDown) { btnDown = true; btnDownAt = now; }
  uint32_t held = now - btnDownAt;
  if (down && held > 1500) {
    if (!screenOn) { screenOn = true; oledPower(0); }
    char b[32];
    if (held < 6000) {
      snprintf(b, sizeof b, "Keep holding %d s", 6 - (int)(held / 1000));
      oledMsg("Forget Wi-Fi?", b, "Let go to cancel");
    } else if (held < 12000) {
      snprintf(b, sizeof b, "Hold %d s: clear admin", 12 - (int)(held / 1000));
      oledMsg("Let go: forget Wi-Fi", b, "password + read-only");
    } else {
      oledMsg("Admin reset", "Admin password cleared", "Read-only is off", "Restarting...");
      adminClear(); delay(1500); ESP.restart();
    }
  }
  if (!down && btnDown) {
    btnDown = false; lastActivity = now;
    if (held >= 6000 && held < 12000) {
      oledMsg("Wi-Fi reset", "Forgetting network...", "Restarting in setup", "mode");
      prefs.remove("ssid"); prefs.remove("pass"); prefs.remove("mode");
#if defined(SHELF_ESP32)
      prefs.remove("apssid"); prefs.remove("appass");   // no screen: back to TheShelf32c / theshelf, so you can always get in
      { Preferences b; b.begin("bt", false); b.putBool("on", false); b.end(); }   // and Bluetooth off
#endif
      delay(1200); ESP.restart();
    }
    if (held < 800 && !screenOn) { screenOn = true; oledPower(0); }
  }
}

// ------------------------------------------------------------ remote admin (900MHz Coms)
void screenWake() { lastActivity = millis(); if (!screenOn) { screenOn = true; oledPower(0); } }
void screenSleep() { if (screenOn) { screenOn = false; oledPower(1); } }
bool screenIsOn() { return screenOn; }
int batteryPercentNow() { return batteryPercent(); }
static uint32_t rebootLaterAt = 0;

String remoteStatusJson() {
  String w = staOK ? "home: " + WiFi.SSID() : apActive ? (firstSetup ? String("setup: ") : String("own: ")) + ownSSID : String("connecting");
  // has to fit in one radio packet with the Coms fields (220 bytes), so names are cut short
  return String("{\"n\":\"") + jsonEsc(hostName.substring(0, 20)) + "\",\"b\":\"" BOARD_SHORT "\",\"fw\":\"" FW_VERSION "\",\"bat\":" + batteryPercent() +
         ",\"up\":" + (millis() / 1000) + ",\"ro\":" + (readOnlyOn() ? 1 : 0) + ",\"w\":\"" + jsonEsc(w.substring(0, 24)) + "\",\"ip\":\"" + currentIP() +
         "\",\"cl\":" + (apActive ? WiFi.softAPgetStationNum() : 0) + ",\"st\":\"" + (onSD ? "SD" : "FL") + "\",\"fm\":" + (uint32_t)((storeTotal - storeUsed) / 1000000ULL) +
         ",\"scr\":" + (screenOn ? 1 : 0) + "}";
}

// cmd: 2 read-only, 3 restart, 4 screen, 7 own Wi-Fi, 8 home Wi-Fi, 11 list a shelf
bool remoteBoardCommand(uint8_t cmd, const String& arg, String& out) {
  switch (cmd) {
    case 2: setReadOnly(arg == "1"); out = remoteStatusJson(); return true;
    case 3: out = "Restarting now. It's back on the mesh in about 20 seconds."; rebootLaterAt = millis() + 10000; return true;
    case 4: if (arg == "1") screenWake(); else screenSleep(); out = remoteStatusJson(); return true;
    case 7: prefs.putString("mode", "hotspot"); out = "Switching to its own Wi-Fi \"" + ownSSID + "\". Restarting."; rebootLaterAt = millis() + 10000; return true;
    case 8:
      if (!wifiSSID.length()) { out = "It has no home Wi-Fi saved. Pick one on its settings page."; return true; }
      prefs.putString("mode", "home"); out = "Joining \"" + wifiSSID + "\". Restarting."; rebootLaterAt = millis() + 10000; return true;
    case 11: {
      if (!isCategory(arg)) { out = "Pick a shelf."; return true; }
      std::vector<DirEnt> all; listDir("/" + arg, all);
      std::vector<String> names; int n = 0;
      for (auto& e : all) if (!e.dir && !e.name.startsWith(".")) { n++; if (names.size() < 60) names.push_back(e.name); }
      std::sort(names.begin(), names.end());
      out = String(n) + (n == 1 ? " file" : " files");
      for (size_t i = 0; i < names.size(); i++) {
        String nm = names[i]; int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
        if (out.length() + nm.length() + 3 > 200) { out += ", ..."; break; }
        out += (i ? ", " : ": ") + nm;
      }
      return true;
    }
  }
  return false;
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
  hostName = prefs.getString("host", String(HOST_PREFIX) + tag);
  wifiSSID = prefs.getString("ssid", "");
  wifiPass = prefs.getString("pass", "");
  hotspotMode = prefs.getString("mode", DEFAULT_MODE) == "hotspot";
  ownSSID = prefs.getString("apssid", strlen(DEFAULT_AP_SSID) ? String(DEFAULT_AP_SSID) : apName);
  ownPass = prefs.getString("appass", AP_PASS);
  firstSetup = !hotspotMode && !wifiSSID.length();
  adminBegin();

  if (!LittleFS.begin(false, "/www", 10, "www")) oledMsg("Web app missing", "Re-flash from the", "installer page");
  setupStorage();
  refreshStats();

  WiFi.persistent(false);
  WiFi.setHostname(hostName.c_str());
  if (wifiSSID.length() && !hotspotMode) {
    WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
    WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
      char b[16]; snprintf(b, sizeof b, "%lus", (unsigned long)((millis() - t0) / 1000));
      oledMsg("Connecting", wifiSSID.c_str(), b); delay(400);
    }
    if (WiFi.status() == WL_CONNECTED) onJoined();
  }
  if (!staOK) startAP();   // own Wi-Fi, first setup, or home Wi-Fi not found (keeps looking for it)

  setupServer(server80);
  setupServer(server8080);
  // Bluetooth comes up last (plain ESP32 only, bt.cpp): Wi-Fi and the web page get their memory
  // first, so the board can always be reached to switch Bluetooth off again.
  btBegin();
  messagingBegin();
  comsBegin();
  markMusicDirty();
  lastActivity = millis();
}

int onlinePeerCount();

void loop() {
  uint32_t now = millis();
  if (apActive) dns.processNextRequest();
  if (!staOK && WiFi.status() == WL_CONNECTED) { onJoined(); apOffAt = now + 90000; }
  if (staOK && WiFi.status() != WL_CONNECTED) staOK = false;
  if (apActive && apOffAt && now > apOffAt && staOK && !hotspotMode) { stopAP(); apOffAt = 0; }
  if (rebootSoon) { rebootSoon = false; rebootAt = now + 1500; }
  if (rebootLaterAt && (int32_t)(now - rebootLaterAt) >= 0) { rebootLaterAt = 0; rebootAt = now; }
  if (rebootAt && now > rebootAt) { oledMsg("Restarting..."); delay(200); ESP.restart(); }

  handleButton();
  static uint32_t lastStats = 0;
  if (statsDirty || now - lastStats > 60000) { refreshStats(); lastStats = now; }
  musicMetaLoop();
  btTick();
#if defined(SHELF_ESP32)
  static uint32_t lastHeapLog = 0;                // for the serial log: how much room is left
  if (now - lastHeapLog > 15000) { lastHeapLog = now; Serial.printf("[shelf] heap free %u, largest block %u, lowest %u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap()); }
#endif

  if (screenOn && now - lastActivity > 300000 && !btnDown) { screenOn = false; oledPower(1); }
  static uint32_t lastDraw = 0;
  static uint32_t lastBatt = 0;
  if (lastBatt == 0 || now - lastBatt > 10000) { readBattery(); lastBatt = now; }
  static int otaShown = -1;
  if (otaPercent >= 0) {                                   // an update is being written: say so, don't unplug
    if (otaPercent != otaShown && now - lastDraw > 250) {
      char b[24]; snprintf(b, sizeof b, "%s %d%%", otaWhat == 'w' ? "Web app" : "Firmware", otaPercent);
      oledMsg("UPDATING", b, "Don't unplug"); otaShown = otaPercent; lastDraw = now;
    }
  } else if (!btnDown && now - lastDraw > (scrolling ? 200u : 1000u)) { otaShown = -1; onlineShelves = onlinePeerCount(); drawStatus(); lastDraw = now; }
  delay(5);
}
