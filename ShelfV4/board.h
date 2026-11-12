// The Shelf for Heltec V4 (and V3) - board pin map
// Heltec WiFi LoRa 32 V4 (ESP32-S3R2, 2 MB PSRAM), V4-R8 (ESP32-S3R8, 8 MB PSRAM),
// and, built with -DSHELF_V3, the V3 (ESP32-S3FN8: 8 MB flash, no PSRAM).
// Same microSD wiring as Heltec Shelf Lite, so a board wired for that works as-is.
#pragma once

#define FW_VERSION "1.9.2"
// SHELF_ESP32: a plain ESP32 dev board (NodeMCU-32S / ESP32-DevKitC, ESP32-D0WD, 4 MB flash):
// no LoRa radio, no battery, and a screen only if you add one (SSD1306 on GPIO 21/22).
#if defined(SHELF_ESP32)
#define SHELF_NO_RADIO 1
#define BOARD_NAME  "ESP32"
#define BOARD_SHORT "ESP32"
#define HOST_PREFIX "shelf-esp32-"
#define COMS_LOG_KEEP 100
#define COMS_RCPT_KEEP 300
// No screen, so it starts on its own Wi-Fi at a fixed address you can bookmark:
// "TheShelf32c", password theshelf, http://10.1.1.250 (home Wi-Fi can still be set in settings)
#define DEFAULT_MODE    "hotspot"
#define DEFAULT_AP_SSID "TheShelf32c"
#define AP_IP           10, 1, 1, 250
#define AP_DHCP_START   10, 1, 1, 100
#elif defined(SHELF_V3)
#define BOARD_NAME  "Heltec V3"
#define BOARD_SHORT "V3"
#define HOST_PREFIX "shelf-v3-"
#define COMS_LOG_KEEP 150     // no PSRAM: keep less of the Coms log in RAM
#define COMS_RCPT_KEEP 600
#else
#define BOARD_NAME  "Heltec V4"
#define BOARD_SHORT "V4"
#define HOST_PREFIX "shelf-v4-"
#define COMS_LOG_KEEP 300
#define COMS_RCPT_KEEP 2000
#endif
#ifndef DEFAULT_MODE
#define DEFAULT_MODE    ""        // first boot: the setup page
#define DEFAULT_AP_SSID ""        // "The-Shelf-XXXX"
#endif

#if defined(SHELF_ESP32)
// --- ESP32 dev board: an optional SSD1306 128x64 screen on the usual I2C pins (no reset pin)
#define OLED_SDA   21
#define OLED_SCL   22
#define OLED_RST   255    // U8X8_PIN_NONE
#define OLED_ADDR  0x3C
#define PIN_BUTTON 0      // BOOT button
#define PIN_VEXT   255    // none of these exist here; the code that uses them never runs on this board
#define PIN_VEXT_R8 255
#define PIN_VBAT   255
#define PIN_ADC_CTRL 255
// microSD module on the ESP32's VSPI pins
#define SD_CS    5
#define SD_MOSI  23
#define SD_SCK   18
#define SD_MISO  19
#define SD_PINS  "CS 5, MOSI 23, CLK 18, MISO 19"
#else
// --- Built in to the board ---
#define OLED_SDA   17
#define OLED_SCL   18
#define OLED_RST   21
#define OLED_ADDR  0x3C
#define PIN_BUTTON 0      // PRG button
#define PIN_VEXT   36     // LOW = OLED/Ve power on (V4 / ESP32-S3R2)
#define PIN_VEXT_R8 40    // Vext moved here on the V4-R8; GPIO36 belongs to its PSRAM
#define PIN_VBAT   1      // battery voltage (390k/100k divider)
#define PIN_ADC_CTRL 37   // connects the divider: HIGH on the V4; on the V3 it depends on the revision

// --- microSD module, on its own SPI bus.
// Standard V4 and V3: the "inline" layout (same as Heltec V4 MP3 + BT v2.0.1+). CS 42, MOSI 41,
// CLK 40, MISO 39 are four neighbouring J3 pins in the module's own order, so it can plug
// straight into the header. On the V4 they're shared with the GNSS connector: leave that empty.
// V4-R8: GPIO 40 switches its screen power, so it keeps the older layout (CS 4, MISO 6, CLK 47, MOSI 48).
#if defined(CONFIG_SPIRAM_MODE_OCT) && CONFIG_SPIRAM_MODE_OCT
#define SD_CS    4
#define SD_MISO  6
#define SD_SCK   47
#define SD_MOSI  48
#define SD_PINS  "CS 4, MISO 6, CLK 47, MOSI 48"
#else
#define SD_CS    42
#define SD_MOSI  41
#define SD_SCK   40
#define SD_MISO  39
#define SD_PINS  "CS 42, MOSI 41, CLK 40, MISO 39"
#endif
#endif   // SHELF_ESP32
