// The Shelf for Heltec V4 - shared declarations
#pragma once
#include <Arduino.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <vector>
#include "board.h"
#include <functional>

// ---------------------------------------------------------------- storage
extern fs::FS* store;          // microSD card, or the board's own flash
extern bool onSD;
extern volatile bool statsDirty;
extern uint64_t storeTotal, storeUsed;

// Shelves live at the root of the card (so the card reads like a normal
// drive in a PC). Everything else The Shelf keeps is under /.shelf.
#define NUM_CATEGORIES 7
extern const char* const CATEGORIES[NUM_CATEGORIES];
bool isCategory(const String& c);
#define DATA_ROOT   "/.shelf/data"
#define PLAY_ROOT   "/.shelf/play"
#define TMP_ROOT    "/.shelf/tmp"

String sanitizeFilename(String name);
String extOf(const String& name);
String fileKind(const String& name);
String humanSize(uint64_t bytes);
String jsonEsc(const String& s);
String uuid4();
String md5hex(const String& s);
String fnvHex(const String& s);
bool isValidId(const String& id);
uint64_t nowMs();
bool mkdirs(const String& path);
bool removeAll(const String& path);
bool copyFile(const String& from, const String& to);
bool moveFile(const String& from, const String& to);
String uniqueName(const String& dir, const String& name);
// One pass over a folder straight from the FAT driver: names, sizes and dates without opening
// each file (opening each one searches the folder again, which crawls with hundreds of songs).
struct DirEnt { String name; uint64_t size; uint64_t mtimeMs; bool dir; };
bool listDir(const String& path, std::vector<DirEnt>& out, size_t max = 20000);
// Just the names in a folder, sorted, for quick "does this thumbnail exist" checks.
std::vector<String> dirNames(const String& path);
std::vector<uint64_t> dirHashes(const String& path);          // art/thumbnail folders, as numbers
void forEachEntry(const String& path, const std::function<bool(const char*, uint64_t, bool)>& fn);
bool hasHash(const std::vector<uint64_t>& h, const String& hex16);
bool hasName(const std::vector<String>& sortedNames, const String& name);
const char* mimeFor(const String& path);

// ---------------------------------------------------------------- PSRAM-first JSON
struct SpiRamAllocator : ArduinoJson::Allocator {
  void* allocate(size_t n) override;
  void deallocate(void* p) override;
  void* reallocate(void* p, size_t n) override;
};
extern SpiRamAllocator psAlloc;
bool loadJsonFile(const String& path, JsonDocument& doc);
bool saveJsonFile(const String& path, JsonDocument& doc);

// ---------------------------------------------------------------- request bodies
// JSON bodies are kept in RAM when small and spooled to a temp file when big
// (pasted pictures, docs with images, drawings, chat pictures).
struct Body {                 // malloc'd as one block (the request frees it with free())
  uint32_t magic;
  bool inFile, tooBig;
  size_t len;
  char path[48];
  size_t fill;                // big bodies: bytes waiting in buf for the card
  uint32_t synced;            // big bodies: bytes safely on the card
  char buf[1];                // small bodies live here; big ones use it as a 16 KB write buffer
};
#define BODY_MAGIC   0x5348424fu
#define UPLOAD_MAGIC 0x5348554cu
#define BODY_RAM_MAX (16 * 1024)
#define BODY_MAX     (30u * 1024u * 1024u)
Body* bodyOf(AsyncWebServerRequest* r);
bool parseBody(AsyncWebServerRequest* r, JsonDocument& doc, JsonDocument* filter = nullptr);
bool bodyValidObject(AsyncWebServerRequest* r);
bool bodyContains(AsyncWebServerRequest* r, const char* needle);
size_t bodyLen(AsyncWebServerRequest* r);
// Decodes a "data:image/...;base64,..." string value of `key` straight to a file.
bool bodyDataUrlToFile(AsyncWebServerRequest* r, const char* key, const String& outPath,
                       size_t maxBytes, String* mimeOut = nullptr);
// Writes the JSON object body to `outPath` with extra "key":value pairs appended
// at the end (later keys win when the browser reads it back).
bool bodyWriteWithTail(AsyncWebServerRequest* r, const String& outPath, const String& tail);
// Same, as raw bytes from another JSON file that ends with '}'.
bool fileWriteWithTail(const String& inPath, const String& outPath, const String& tail);

struct UploadCtx {           // multipart uploads (also freed with free())
  uint32_t magic;
  uint8_t err;               // 1 bad category, 2 write failed, 3 not allowed
  bool media;                // a picture/video pasted onto the Home board
  char cat[16];
  char saved[160];
  char mediaType[8];
  int count;
  char full[224];            // full path of the file being written
  uint32_t synced;           // bytes safely on the card so far
  uint8_t retries;           // blocks that needed a second try
  size_t fill;               // bytes waiting in buf (written to the card in big blocks)
  uint8_t buf[1];            // UPLOAD_BUF bytes (flexible size, same block)
};
#define UPLOAD_BUF (16 * 1024)
// Writes one block to a file being saved and makes it stick; if the card refuses it, reopens
// the file and writes the block again where it belongs (up to 3 more times).
bool writeBlockRetry(File& f, const char* path, const uint8_t* buf, size_t n, uint32_t& synced);
extern volatile uint32_t sdRetries, sdFailures;   // shown on the settings page

// ---------------------------------------------------------------- responses
void sendJson(AsyncWebServerRequest* r, int code, const String& body);
// Sends a finished body straight out of one buffer. (The library's own stream response sends
// by removing one byte at a time from the front of its buffer, which took minutes for a long
// song list and froze the board.)
void sendBody(AsyncWebServerRequest* r, int code, const char* type, String&& body);
// A Print that appends to a String, for building big replies with printf.
struct StrOut : public Print {
  String& s;
  explicit StrOut(String& str) : s(str) {}
  size_t write(uint8_t c) override { s += (char)c; return 1; }
  size_t write(const uint8_t* b, size_t n) override { s.concat((const char*)b, n); return n; }
};
void sendErr(AsyncWebServerRequest* r, int code, const String& msg);
void sendDoc(AsyncWebServerRequest* r, JsonDocument& doc, int code = 200);
void sendFileRanged(AsyncWebServerRequest* r, const String& path, bool download = false);
void sendJsonFile(AsyncWebServerRequest* r, const String& path);
// Streams "[" a "," b "," ... "]" from files; each file optionally gets its final
// byte replaced by `tails[i]` (used to add "read":true to chat messages).
void sendJsonArrayOfFiles(AsyncWebServerRequest* r, std::vector<String>* paths, std::vector<String>* tails = nullptr);

// ---------------------------------------------------------------- feature modules
bool handleFilesApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m);
bool handleToolsApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m);
bool handleMessagingApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m);
bool handleBoardApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m);
bool handleCardApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m);   // microSD backup/restore
bool isCardFileUpload(const String& url);
// bt.cpp: Bluetooth headsets (plain ESP32 only; empty elsewhere)
void btBegin();
extern volatile int musicReaders;   // music_meta.cpp
extern volatile int streamsActive;  // util.cpp: big files (videos, songs) being sent right now
void btTick();
bool btActive();
bool handleBtApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m);
// ota.cpp: firmware updates over Wi-Fi
bool isOtaUpload(const String& url);
void otaBody(AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total);
bool handleOtaApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m);
bool handleThumbImportApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m);   // thumb_import.cpp
extern volatile int otaPercent;
void ledBegin();   // led.cpp: plain ESP32 blue LED as a status light
void ledWebUp();
extern volatile char otaWhat;
void handleUploadPart(AsyncWebServerRequest* r, const String& filename, size_t index, uint8_t* data, size_t len, bool final);
void filesUploadDone(AsyncWebServerRequest* r);
void portalMediaDone(AsyncWebServerRequest* r);
bool serveContent(AsyncWebServerRequest* r, const String& url);   // /shared /play /data-files

void messagingBegin();
void comsBegin();
bool handleComsApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m);
bool comsOn();
extern String comsLastLine; extern uint32_t comsLastAt;
void messagingLoop();
void musicMetaLoop();
void markMusicDirty();
void recordVisitor(const String& ip);
int  countCategory(const String& cat);
void invalidateCounts();

// ---------------------------------------------------------------- admin password, read-only, remote admin
void adminBegin();
bool handleAdminApi(AsyncWebServerRequest* r, const String& url, WebRequestMethod m);
bool adminIsSet();
bool readOnlyOn();
void setReadOnly(bool on);
void adminClear();
bool canWrite(AsyncWebServerRequest* r);
bool allowedWhenReadOnly(const String& url, WebRequestMethod m);
void sendReadOnly(AsyncWebServerRequest* r);
uint32_t meshNodeId();
void adminDeriveKey(const String& password, uint32_t node, uint8_t out[32]);
void hmac8(const uint8_t key[32], const uint8_t* data, size_t len, uint8_t out[8]);
const uint8_t* adminKey();
bool adminLockedOut();
void adminNoteFail();
void adminNoteOk();
uint32_t adminLastCounter(uint32_t from);
void adminSetCounter(uint32_t from, uint32_t c);
uint32_t adminNextOwnCounter(uint32_t atLeast);
// main.cpp: what a remote admin sees and can do
String remoteStatusJson();
bool remoteBoardCommand(uint8_t cmd, const String& arg, String& out);
int batteryPercentNow();
void screenWake();
void screenSleep();
bool screenIsOn();

// ---------------------------------------------------------------- device / Wi-Fi
extern String hostName, apName, wifiSSID;
extern bool apActive, staOK;
extern volatile bool rebootSoon;
String currentIP();
extern SemaphoreHandle_t fsLock;     // background tasks vs. web requests
struct FsGuard { FsGuard() { xSemaphoreTakeRecursive(fsLock, portMAX_DELAY); } ~FsGuard() { xSemaphoreGiveRecursive(fsLock); } };
