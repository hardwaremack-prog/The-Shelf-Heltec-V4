// The Shelf for a plain ESP32 - Bluetooth headsets.
//
// The ESP32 (not the S3 on the Heltec boards) has Classic Bluetooth, so it can send music
// straight to a headset or speaker (A2DP). The board plays songs from its microSD card:
// a player task decodes MP3 or WAV into a small ring of samples, and the Bluetooth stack
// takes them from there. The Bluetooth tab drives it:
//
//   GET  /api/bt                 state: on, headset, found list, now playing
//   POST /api/bt/power   {on}    turn Bluetooth on or off (restarts the board: memory)
//   POST /api/bt/scan            forget the found list and look again
//   POST /api/bt/connect {mac}   connect this headset (it's remembered and reconnects)
//   POST /api/bt/disconnect, /api/bt/forget
//   POST /api/bt/play    {paths:[...], index, shuffle}   play these songs on the headset
//   POST /api/bt/control {cmd: pause|resume|next|prev|stop} or {volume: 0-100}
//
// With Bluetooth off the controller's memory is handed back at boot, so nothing is lost
// by having the feature.
#include "shelf.h"

#if defined(SHELF_ESP32)
#include <BluetoothA2DPSource.h>
#include <AudioFileSourceSD.h>
#include <AudioFileSourceID3.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#include <AudioOutput.h>
#include <Preferences.h>
#include <esp_bt.h>
#include <algorithm>

#define BT_NAME "TheShelf32c"
static BluetoothA2DPSource a2dp;
static Preferences bprefs;
static SemaphoreHandle_t btLock;
struct BtLock { BtLock() { xSemaphoreTakeRecursive(btLock, portMAX_DELAY); } ~BtLock() { xSemaphoreGiveRecursive(btLock); } };

static bool btOn = false, btRunning = false;
static String savedMac, savedName, targetMac, connMac, connName, btError;
struct Found { String mac, name; int rssi; uint32_t at; };
static std::vector<Found> found;
static volatile bool connected = false;
static bool stayOff = false;                            // "Disconnect": don't connect again until asked

// ---- the sample ring: player task writes, Bluetooth reads (44.1 kHz stereo)
static const uint32_t RING = 8192;                      // ~186 ms
static int16_t* ring = nullptr;                         // L,R pairs
static volatile uint32_t rW = 0, rR = 0;
static inline uint32_t ringUsed() { return rW - rR; }
static inline uint32_t ringFree() { return RING - (rW - rR); }

static volatile int volumePct = 70;
static volatile uint64_t framesOut = 0;                 // frames handed to the headset this song

static int32_t onFrames(Frame* f, int32_t n) {
  uint32_t have = ringUsed(), k = std::min<uint32_t>(have, (uint32_t)n);
  for (uint32_t i = 0; i < k; i++) { uint32_t p = ((rR + i) % RING) * 2; f[i].channel1 = ring[p]; f[i].channel2 = ring[p + 1]; }
  rR += k; framesOut += k;
  for (int32_t i = k; i < n; i++) { f[i].channel1 = 0; f[i].channel2 = 0; }   // nothing yet: quiet
  return n;
}

// ESP8266Audio output that resamples to 44.1 kHz and fills the ring
class RingOut : public AudioOutput {
 public:
  bool begin() override { acc = 0; havePrev = false; return true; }
  bool stop() override { return true; }
  bool ConsumeSample(int16_t s[2]) override {
    if (ringFree() < 4) return false;                    // full: the decoder tries again later
    MakeSampleStereo16(s);
    int32_t g = volumePct;
    int16_t cur[2] = { (int16_t)(s[0] * g / 100), (int16_t)(s[1] * g / 100) };
    if (hertz == 44100 || hertz <= 0) { push(cur[0], cur[1]); return true; }
    if (!havePrev) { prev[0] = cur[0]; prev[1] = cur[1]; havePrev = true; acc = 0; }
    float step = hertz / 44100.0f;                      // input samples per output sample
    while (acc < 1.0f) {                                 // at most a few per input sample
      push((int16_t)(prev[0] + (cur[0] - prev[0]) * acc), (int16_t)(prev[1] + (cur[1] - prev[1]) * acc));
      acc += step;
    }
    acc -= 1.0f; prev[0] = cur[0]; prev[1] = cur[1];
    return true;
  }
 private:
  void push(int16_t l, int16_t r) { uint32_t p = (rW % RING) * 2; ring[p] = l; ring[p + 1] = r; rW++; }
  float acc = 0; bool havePrev = false; int16_t prev[2] = {0, 0};
};

// ---- the player
static RingOut out;
static AudioGenerator* gen = nullptr;
static AudioFileSourceSD* file = nullptr;
static AudioFileSourceID3* id3 = nullptr;
static std::vector<String> queue;
static int qIndex = -1;
static bool paused = false, shuffleOn = false;
static volatile int pendingCmd = 0;                      // 1 next, 2 prev, 3 stop, 4 start qIndex
static uint32_t songStarted = 0;
static size_t songSize = 0;

static void closeSong() {
  if (gen) { gen->stop(); delete gen; gen = nullptr; }
  if (id3) { delete id3; id3 = nullptr; }
  if (file) { delete file; file = nullptr; }
  rR = rW;                                              // drop what's left of the last song
}

static bool openSong(const String& path) {
  closeSong();
  String lower = path; lower.toLowerCase();
  bool mp3 = lower.endsWith(".mp3"), wav = lower.endsWith(".wav");
  if (!mp3 && !wav) return false;
  file = new AudioFileSourceSD(path.c_str());
  if (!file || !file->isOpen()) { closeSong(); return false; }
  songSize = file->getSize();
  if (mp3) { id3 = new AudioFileSourceID3(file); gen = new AudioGeneratorMP3(); if (!gen->begin(id3, &out)) { closeSong(); return false; } }
  else { gen = new AudioGeneratorWAV(); if (!gen->begin(file, &out)) { closeSong(); return false; } }
  framesOut = 0; songStarted = millis();
  return true;
}

// start queue[qIndex], skipping songs that won't open (other formats, unreadable)
static void startAt(int i, int dir) {
  for (int tries = 0; tries < (int)queue.size(); tries++) {
    if (i < 0 || i >= (int)queue.size()) break;
    qIndex = i;
    if (openSong(queue[i])) { paused = false; return; }
    i += dir;
  }
  closeSong(); qIndex = -1;
}

static void playerTask(void*) {
  for (;;) {
    int cmd = pendingCmd;
    if (cmd) {
      BtLock l; FsGuard g;
      pendingCmd = 0;
      if (cmd == 3) { closeSong(); qIndex = -1; }
      else if (cmd == 4) startAt(qIndex, 1);
      else if (cmd == 1) { if (qIndex + 1 < (int)queue.size()) startAt(qIndex + 1, 1); else { closeSong(); qIndex = -1; } }
      else if (cmd == 2) {
        if (gen && millis() - songStarted > 4000) startAt(qIndex, 1);   // more than a few seconds in: restart the song
        else startAt(std::max(0, qIndex - 1), -1);
      }
    }
    bool working = false;
    if (gen && !paused && connected) {
      BtLock l;
      if (gen && gen->isRunning()) {
        if (!gen->loop()) { gen->stop(); pendingCmd = 1; }             // song over: next one
        working = ringFree() > RING / 4;                                // still room: keep going
      }
    }
    vTaskDelay(pdMS_TO_TICKS(working ? 1 : 8));
  }
}

// ---- Bluetooth
static String macStr(const uint8_t* a) { char b[18]; snprintf(b, sizeof b, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]); return b; }

static bool onFound(const char* name, esp_bd_addr_t addr, int rssi) {
  String mac = macStr(addr), nm = name && *name ? String(name) : mac;
  BtLock l;
  bool have = false;
  for (auto& f : found) if (f.mac == mac) { f.name = nm; f.rssi = rssi; f.at = millis(); have = true; }
  if (!have && found.size() < 20) found.push_back({mac, nm, rssi, millis()});
  if (stayOff) return false;
  String want = targetMac.length() ? targetMac : savedMac;
  if (want.length() && mac == want) { connName = nm; return true; }
  return false;
}

static void onState(esp_a2d_connection_state_t st, void*) {
  BtLock l;
  connected = st == ESP_A2D_CONNECTION_STATE_CONNECTED;
  if (connected) {
    connMac = macStr(*a2dp.get_last_peer_address());
    if (!connName.length()) for (auto& f : found) if (f.mac == connMac) connName = f.name;
    if (connMac != savedMac || connName != savedName) {
      savedMac = connMac; if (connName.length()) savedName = connName;
      bprefs.putString("mac", savedMac); bprefs.putString("name", savedName);
    }
    targetMac = ""; btError = "";
    a2dp.set_auto_reconnect(true);
    a2dp.set_volume(127);                                // our own volume does the work
  } else if (st == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
    connMac = "";
  }
}

void btBegin() {
  btLock = xSemaphoreCreateRecursiveMutex();
  bprefs.begin("bt", false);
  btOn = bprefs.getBool("on", false);
  savedMac = bprefs.getString("mac", ""); savedName = bprefs.getString("name", "");
  volumePct = bprefs.getInt("vol", 70);
  if (!btOn) { esp_bt_controller_mem_release(ESP_BT_MODE_BTDM); return; }   // off: give its memory to the web server
  ring = (int16_t*)malloc(RING * 4);
  if (!ring) { btError = "Not enough memory for Bluetooth."; btOn = false; return; }
  a2dp.set_local_name(BT_NAME);
  a2dp.set_ssid_callback(onFound);
  a2dp.set_on_connection_state_changed(onState);
  a2dp.set_data_callback_in_frames(onFrames);
  a2dp.set_auto_reconnect(savedMac.length() > 0);
  a2dp.start();
  btRunning = true;
  xTaskCreatePinnedToCore(playerTask, "btplay", 6144, nullptr, 3, nullptr, 1);
}

bool btActive() { return btRunning; }

static String esc(const String& s) { return jsonEsc(s); }

static String stateJson() {
  BtLock l;
  String s = String("{\"on\":") + (btOn ? "true" : "false") + ",\"started\":" + (btRunning ? "true" : "false") +
             ",\"connected\":" + (connected ? "true" : "false") + ",\"headset\":{\"mac\":\"" + esc(connMac) + "\",\"name\":\"" + esc(connName.length() ? connName : savedName) + "\"}" +
             ",\"saved\":{\"mac\":\"" + esc(savedMac) + "\",\"name\":\"" + esc(savedName) + "\"},\"target\":\"" + esc(targetMac) + "\",\"held\":" + (stayOff ? "true" : "false") +
             ",\"searching\":" + (btRunning && !connected && a2dp.is_discovery_active() ? "true" : "false") +
             ",\"error\":\"" + esc(btError) + "\",\"found\":[";
  std::vector<Found> f = found;
  std::sort(f.begin(), f.end(), [](const Found& a, const Found& b) { return a.rssi > b.rssi; });
  for (size_t i = 0; i < f.size(); i++) s += String(i ? "," : "") + "{\"mac\":\"" + f[i].mac + "\",\"name\":\"" + esc(f[i].name) + "\",\"rssi\":" + f[i].rssi + "}";
  String track = (qIndex >= 0 && qIndex < (int)queue.size()) ? queue[qIndex] : String("");
  float secs = framesOut / 44100.0f, frac = 0;
  if (id3 || file) { size_t pos = file ? file->getPos() : 0; if (songSize) frac = (float)pos / songSize; }
  float dur = frac > 0.02f ? secs / frac : 0;
  s += String("],\"player\":{\"track\":\"") + esc(track) + "\",\"index\":" + qIndex + ",\"count\":" + queue.size() +
       ",\"playing\":" + (gen && !paused ? "true" : "false") + ",\"paused\":" + (paused ? "true" : "false") +
       ",\"shuffle\":" + (shuffleOn ? "true" : "false") + ",\"pos\":" + String(secs, 1) + ",\"dur\":" + String(dur, 0) + ",\"volume\":" + (int)volumePct + "}}";
  return s;
}

bool handleBtApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m) {
  if (!url.startsWith("/api/bt")) return false;
  if (url == "/api/bt" && m == HTTP_GET) { sendJson(r, 200, stateJson()); return true; }
  if (m != HTTP_POST) { sendErr(r, 405, "Use POST"); return true; }
  JsonDocument b(&psAlloc); parseBody(r, b);
  if (url == "/api/bt/power") {
    bool on = b["on"] | false;
    if (on != btOn) { bprefs.putBool("on", on); sendJson(r, 200, "{\"ok\":true,\"restart\":true}"); rebootSoon = true; }
    else sendJson(r, 200, "{\"ok\":true,\"restart\":false}");
    return true;
  }
  if (!btRunning) { sendErr(r, 400, "Turn Bluetooth on first."); return true; }
  if (url == "/api/bt/scan") { BtLock l; found.clear(); sendJson(r, 200, stateJson()); return true; }
  if (url == "/api/bt/connect") {
    String mac = b["mac"] | "";
    if (mac.length() != 17) { sendErr(r, 400, "Pick a headset."); return true; }
    { BtLock l; targetMac = mac; stayOff = false; connName = ""; for (auto& f : found) if (f.mac == mac) connName = f.name; }
    a2dp.set_auto_reconnect(false);
    if (connected) a2dp.disconnect();                   // the search starts again and finds it
    sendJson(r, 200, stateJson());
    return true;
  }
  if (url == "/api/bt/disconnect") { a2dp.set_auto_reconnect(false); { BtLock l; targetMac = ""; stayOff = true; } if (connected) a2dp.disconnect(); sendJson(r, 200, stateJson()); return true; }
  if (url == "/api/bt/forget") {
    { BtLock l; savedMac = ""; savedName = ""; targetMac = ""; bprefs.remove("mac"); bprefs.remove("name"); }
    a2dp.set_auto_reconnect(false); a2dp.clean_last_connection();
    if (connected) a2dp.disconnect();
    sendJson(r, 200, stateJson());
    return true;
  }
  if (url == "/api/bt/play") {
    std::vector<String> paths;
    for (JsonVariant v : b["paths"].as<JsonArray>()) { String p = v.as<String>(); if (p.startsWith("/") && p.indexOf("..") < 0) paths.push_back(p); }
    if (paths.empty()) { sendErr(r, 400, "No songs to play."); return true; }
    int idx = b["index"] | 0;
    bool shuf = b["shuffle"] | false;
    { BtLock l;
      if (shuf) { for (size_t i = paths.size() - 1; i > 0; i--) std::swap(paths[i], paths[esp_random() % (i + 1)]); idx = 0; }
      queue = paths; shuffleOn = shuf; qIndex = constrain(idx, 0, (int)queue.size() - 1); paused = false; pendingCmd = 4; }
    sendJson(r, 200, "{\"ok\":true}");
    return true;
  }
  if (url == "/api/bt/control") {
    if (b["volume"].is<int>()) { volumePct = constrain((int)b["volume"], 0, 100); bprefs.putInt("vol", volumePct); }
    String c = b["cmd"] | "";
    { BtLock l;
      if (c == "pause") paused = true;
      else if (c == "resume") { if (gen) paused = false; else if (!queue.empty()) { if (qIndex < 0) qIndex = 0; pendingCmd = 4; } }
      else if (c == "next") pendingCmd = 1;
      else if (c == "prev") pendingCmd = 2;
      else if (c == "stop") pendingCmd = 3; }
    sendJson(r, 200, stateJson());
    return true;
  }
  sendErr(r, 404, "Not found");
  return true;
}

#else
void btBegin() {}
bool btActive() { return false; }
bool handleBtApi(AsyncWebServerRequest*, const String&, WebRequestMethod) { return false; }
#endif
