// The Shelf for Heltec V4 (and V3) - board pin map
// Heltec WiFi LoRa 32 V4 (ESP32-S3R2, 2 MB PSRAM), V4-R8 (ESP32-S3R8, 8 MB PSRAM),
// and, built with -DSHELF_V3, the V3 (ESP32-S3FN8: 8 MB flash, no PSRAM).
// Same microSD wiring as Heltec Shelf Lite, so a board wired for that works as-is.
#pragma once

#define FW_VERSION "1.6.6"
#ifdef SHELF_V3
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
