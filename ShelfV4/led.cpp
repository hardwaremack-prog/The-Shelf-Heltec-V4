// The Shelf for a plain ESP32 - the blue LED (GPIO 2 on NodeMCU-32S / DevKitC boards) as a status light.
// The red LED next to it is the power light and can't be switched.
//   starting up           slow blink (on half a second, off half a second)
//   web page ready        breathing: a slow, smooth fade in and out
//   microSD card busy     a quick flicker for as long as the card is being read or written
//   updating              fast flash (firmware or web app being written)
#include "shelf.h"

#if defined(SHELF_ESP32)
#define LED_PIN 2
#define LED_CH  6                 // an LEDC channel nothing else uses
extern "C" volatile uint32_t sdActivityMs;   // libraries/SD/src/sd_diskio.cpp
static volatile bool webUp = false;

static void ledTask(void*) {
  for (;;) {
    uint32_t now = millis();
    uint32_t duty;
    if (otaPercent >= 0) duty = (now / 70) % 2 ? 255 : 0;                         // updating: fast flash
    else if (!webUp) duty = (now / 500) % 2 ? 255 : 0;                            // starting: slow blink
    else if (sdActivityMs && now - sdActivityMs < 90) duty = (now / 35) % 2 ? 255 : 0;   // card busy: flicker
    else {                                                                         // ready: breathing, ~4 s a breath
      float t = (now % 4000) / 4000.0f;
      float v = 0.5f - 0.5f * cosf(t * 2 * PI);
      duty = (uint32_t)(4 + v * v * 251);                                          // squared: looks even to the eye
    }
    ledcWrite(LED_CH, duty);
    vTaskDelay(pdMS_TO_TICKS(15));
  }
}

void ledBegin() {
  ledcSetup(LED_CH, 5000, 8);
  ledcAttachPin(LED_PIN, LED_CH);
  xTaskCreatePinnedToCore(ledTask, "led", 2048, nullptr, 1, nullptr, 1);
}
void ledWebUp() { webUp = true; }
#else
void ledBegin() {}
void ledWebUp() {}
#endif
