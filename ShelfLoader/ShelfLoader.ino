// The Shelf for a plain ESP32 - update loader.
//
// A plain ESP32 has 4 MB of flash: room for The Shelf (with Bluetooth) and its web app, but not
// for a second copy of the firmware. So an update over Wi-Fi saves the new firmware on the
// microSD card and restarts into this small loader, which lives in the second app slot. It:
//   1. reads /.shelf/update-app.bin from the card,
//   2. writes it over The Shelf in the first app slot (checking it as it goes),
//   3. deletes the file and starts The Shelf again.
// If there's no file, or anything goes wrong, it just starts The Shelf as it was. It never
// touches Wi-Fi, the web app or your settings.
#include <SPI.h>
#include <SD.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#define SD_CS    5
#define SD_MOSI  23
#define SD_SCK   18
#define SD_MISO  19
#define UPDATE_FILE "/.shelf/update-app.bin"

static SPIClass sdSPI(VSPI);

void startShelf() {
  const esp_partition_t* app = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
  if (app) esp_ota_set_boot_partition(app);
  Serial.println("loader: starting The Shelf");
  delay(100);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nThe Shelf update loader");
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  for (int pin : {SD_MISO, SD_MOSI, SD_CS}) gpio_pullup_en((gpio_num_t)pin);
  if (!SD.begin(SD_CS, sdSPI, 10000000) && !SD.begin(SD_CS, sdSPI, 4000000)) { Serial.println("loader: no card"); startShelf(); }
  File f = SD.open(UPDATE_FILE, FILE_READ);
  if (!f) { Serial.println("loader: nothing to install"); startShelf(); }
  size_t size = f.size();
  const esp_partition_t* app = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
  uint8_t magic = 0; f.read(&magic, 1); f.seek(0);
  if (!app || size < 4096 || size > app->size || magic != 0xE9) { Serial.println("loader: the file isn't firmware"); f.close(); SD.remove(UPDATE_FILE); startShelf(); }
  // Update writes the "next" slot: from here (slot 2) that's slot 1, The Shelf's
  if (!Update.begin(size, U_FLASH)) { Serial.printf("loader: begin failed: %s\n", Update.errorString()); f.close(); startShelf(); }
  static uint8_t buf[4096];
  size_t done = 0; bool ok = true;
  while (done < size) {
    int n = f.read(buf, std::min(sizeof buf, size - done));
    if (n <= 0 || Update.write(buf, n) != (size_t)n) { ok = false; break; }
    done += n;
    if ((done & 0x3FFFF) < sizeof buf) Serial.printf("loader: %u%%\n", (unsigned)(done * 100 / size));
  }
  f.close();
  if (ok && Update.end(true)) {                       // checks the image and makes it the one that starts
    SD.remove(UPDATE_FILE);
    Serial.println("loader: installed");
    delay(100);
    ESP.restart();
  }
  Serial.printf("loader: failed (%s); keeping the old firmware\n", Update.errorString());
  Update.abort();
  SD.remove(UPDATE_FILE);
  startShelf();
}

void loop() {}
