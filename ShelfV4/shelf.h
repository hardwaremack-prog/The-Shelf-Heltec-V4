// The Shelf for Heltec V4 - shared declarations
#pragma once
#include <Arduino.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <vector>
#include "board.h"

// ---------------------------------------------------------------- storage
extern fs::FS* store;          // microSD card, or the board's own flash
extern bool onSD;
extern volatile bool statsDirty;
extern uint64_t storeTotal, storeUsed;

// Shelves live at the root of the card (so the card reads like a normal
// drive in a PC). Everything else The Shelf keeps is under /.shelf.
extern const char* const CATEGORIES[6];
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
  char buf[1];                // small bodies live here (flexible size)
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
};

// ---------------------------------------------------------------- responses
void sendJson(AsyncWebServerRequest* r, int code, const String& body);
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
void handleUploadPart(AsyncWebServerRequest* r, const String& filename, size_t index, uint8_t* data, size_t len, bool final);
void filesUploadDone(AsyncWebServerRequest* r);
void portalMediaDone(AsyncWebServerRequest* r);
bool serveContent(AsyncWebServerRequest* r, const String& url);   // /shared /play /data-files

void messagingBegin();
void messagingLoop();
void musicMetaLoop();
void markMusicDirty();
void recordVisitor(const String& ip);
int  countCategory(const String& cat);
void invalidateCounts();

// ---------------------------------------------------------------- device / Wi-Fi
extern String hostName, apName, wifiSSID;
extern bool apActive, staOK;
extern volatile bool rebootSoon;
String currentIP();
extern SemaphoreHandle_t fsLock;     // background tasks vs. web requests
struct FsGuard { FsGuard() { xSemaphoreTakeRecursive(fsLock, portMAX_DELAY); } ~FsGuard() { xSemaphoreGiveRecursive(fsLock); } };
